// The lazy DFA's work off the hot path: stepping sets of NFA states, adding
// and evicting states, and waiting for the lock.
#include "lazy_dfa.hpp"

#include <thread>

namespace nlu {
namespace detail {

// --- PartStepper ------------------------------------------------------------

std::vector<int> PartStepper::step(const std::vector<int>& set, char32_t c) {
  if (states_.size() > kMaxStates || next_.size() > kMaxTransitions) forget();
  // Usually `set` is what the last step returned, and its pieces are known.
  if (set != last_) {
    last_.clear();
    last_pieces_.clear();
    if (!worth_splitting(set)) return closure_.step(set, c);
    last_pieces_ = pieces(set);
    last_ = set;
  }
  // Kept parts are looked up; the states of the others step together.
  std::vector<Piece> stepped;
  std::vector<int> out;
  direct_.clear();
  std::size_t at = 0;  // where the piece's states are in last_
  for (const Piece& p : last_pieces_) {
    if (p.part < 0) {
      direct_.insert(direct_.end(), last_.begin() + at, last_.begin() + at + p.size);
    } else if (const int q = next(p.part, c); q >= 0) {
      const auto from = states_.begin() + static_cast<std::ptrdiff_t>(parts_[q].begin);
      out.insert(out.end(), from, from + static_cast<std::ptrdiff_t>(parts_[q].size));
      stepped.push_back({q, *from, parts_[q].size});
    }
    at += p.size;
  }
  if (!direct_.empty()) {
    // Both are sorted, and hold different rules' states.
    const std::vector<int> direct = closure_.step(direct_, c);
    std::vector<int> all(out.size() + direct.size());
    std::merge(out.begin(), out.end(), direct.begin(), direct.end(), all.begin());
    out = std::move(all);
    const std::vector<Piece> more = pieces(direct);
    std::vector<Piece> merged(stepped.size() + more.size());
    std::merge(stepped.begin(), stepped.end(), more.begin(), more.end(), merged.begin(),
               [](const Piece& a, const Piece& b) { return a.first < b.first; });
    stepped = std::move(merged);
  }
  last_ = out;
  last_pieces_ = std::move(stepped);
  return out;
}

void PartStepper::forget() {
  states_.clear();
  parts_.clear();
  index_.clear();
  next_.clear();
  last_.clear();
  last_pieces_.clear();
}

bool PartStepper::worth_splitting(const std::vector<int>& set) const {
  if (set.size() < kMinSplit) return false;
  const auto rules = static_cast<std::size_t>(rule_of_[set.back()] - rule_of_[set.front()] + 1);
  return set.size() >= kMinKept * rules;
}

std::vector<PartStepper::Piece> PartStepper::pieces(const std::vector<int>& set) {
  std::vector<Piece> out;
  for (std::size_t i = 0, j; i < set.size(); i = j) {
    for (j = i + 1; j < set.size() && rule_of_[set[j]] == rule_of_[set[i]];) ++j;
    const int part = j - i < kMinKept ? -1 : intern(set.data() + i, set.data() + j);
    out.push_back({part, set[i], j - i});
  }
  return out;
}

int PartStepper::intern(const int* begin, const int* end) {
  const std::size_t size = static_cast<std::size_t>(end - begin);
  std::size_t hash = size;
  for (const int* s = begin; s != end; ++s) hash = hash * 1000003u ^ static_cast<std::size_t>(*s);
  auto range = index_.equal_range(hash);
  for (auto it = range.first; it != range.second; ++it) {
    const Part& p = parts_[it->second];
    if (p.size == size && std::equal(begin, end, states_.begin() + static_cast<std::ptrdiff_t>(p.begin)))
      return it->second;
  }
  const int id = static_cast<int>(parts_.size());
  parts_.push_back({states_.size(), size});
  states_.insert(states_.end(), begin, end);
  index_.emplace(hash, id);
  return id;
}

int PartStepper::next(int p, char32_t c) {
  const std::uint64_t key = static_cast<std::uint64_t>(p) << 32 | static_cast<std::uint32_t>(c);
  auto it = next_.find(key);
  if (it != next_.end()) return it->second;
  const auto from = states_.begin() + static_cast<std::ptrdiff_t>(parts_[p].begin);
  const std::vector<int> to =
      closure_.step(std::vector<int>(from, from + static_cast<std::ptrdiff_t>(parts_[p].size)), c);
  const int q = to.empty() ? -1 : intern(to.data(), to.data() + to.size());
  next_.emplace(key, q);
  return q;
}

// --- LazyDfa ----------------------------------------------------------------

LazyDfa::LazyDfa(const Nfa& nfa, const std::vector<int>& rule_of, int start,
                 const CacheOptions& options)
    : nfa_(nfa),
      rule_of_(rule_of),
      max_states_(std::max<std::size_t>(options.max_states, 2)),
      max_bytes_(options.max_bytes),
      mutex_(!options.per_thread) {
  auto closure = std::make_unique<PartStepper>(nfa, rule_of_);
  start_ = intern(make_state((*closure)({start}, 0)));
  closures_.push_back(std::move(closure));
}

CacheStats LazyDfa::stats() {
  std::shared_lock<Lock> lock(mutex_);
  return {states_.size(), transitions_, bytes_, evictions_};
}

LazyDfa::NewState LazyDfa::make_state(std::vector<int> set) const {
  const std::size_t h = SetHash()(set);
  std::vector<int> accepts = nfa_.accepted_rules(set);
  return {std::move(set), h, std::move(accepts)};
}

std::size_t LazyDfa::state_bytes(const DState& d) {
  return sizeof(DState) + (d.set.size() + d.accepts.size()) * sizeof(int) + 64;
}

int LazyDfa::find(const std::vector<int>& set, std::size_t hash) const {
  auto range = index_.equal_range(hash);
  for (auto it = range.first; it != range.second; ++it)
    if (states_[it->second].set == set) return it->second;
  return -1;
}

int LazyDfa::intern(NewState s) {
  int id = find(s.set, s.hash);
  if (id >= 0) return id;
  id = static_cast<int>(states_.size());
  index_.emplace(s.hash, id);
  states_.emplace_back();
  static_cast<NewState&>(states_.back()) = std::move(s);
  bytes_ += state_bytes(states_.back());
  used_.emplace_back(round_);
  return id;
}

int LazyDfa::add(int from, char32_t c, NewState next) {
  if (from >= 0) {
    auto it = states_[from].next.find(c);
    if (it != states_[from].next.end()) return it->second;  // another thread added it
  }
  if (states_.size() >= max_states_ || bytes_ >= max_bytes_) {
    evict();
    return intern(std::move(next));
  }
  const int to = intern(std::move(next));
  if (from >= 0) {
    states_[from].next.emplace(c, to);
    ++transitions_;
    bytes_ += kTransitionBytes;
  }
  return to;
}

int LazyDfa::to_shared(int d, std::vector<int>& set, std::unique_lock<Lock>& exclusive,
                       std::shared_lock<Lock>& shared) {
  for (;;) {
    set = states_[d].set;
    const std::size_t hash = states_[d].hash;
    const unsigned epoch = epoch_;
    exclusive.unlock();
    shared.lock();
    if (epoch == epoch_) return d;
    d = find(set, hash);
    if (d >= 0) return d;
    shared.unlock();
    exclusive.lock();
    d = add(-1, 0, {set, hash, nfa_.accepted_rules(set)});
  }
}

void LazyDfa::evict() {
  ++evictions_;
  std::vector<int> renumber(states_.size(), -1);
  std::vector<int> kept;
  std::size_t kept_bytes = 0;
  for (int id = 0; id < static_cast<int>(states_.size()); ++id) {
    const bool wanted = id == start_ || used_[id].load(std::memory_order_relaxed) == round_;
    const std::size_t b = state_bytes(states_[id]);
    if (!wanted || (id != start_ && (kept.size() >= max_states_ / 2 ||
                                     kept_bytes + b > max_bytes_ / 2)))
      continue;
    renumber[id] = static_cast<int>(kept.size());
    kept.push_back(id);
    kept_bytes += b;
  }
  std::vector<DState> states;
  states.reserve(kept.size());
  transitions_ = 0;
  bytes_ = 0;
  for (int id : kept) {
    DState d = std::move(states_[id]);
    std::unordered_map<char32_t, int> next;
    for (const auto& kv : d.next) {
      if (renumber[kv.second] < 0) continue;
      next.emplace(kv.first, renumber[kv.second]);
    }
    d.next = std::move(next);
    transitions_ += d.next.size();
    bytes_ += state_bytes(d) + d.next.size() * kTransitionBytes;
    states.push_back(std::move(d));
  }
  states_ = std::move(states);
  index_.clear();
  used_.clear();
  ++round_;
  for (int id = 0; id < static_cast<int>(states_.size()); ++id) {
    index_.emplace(states_[id].hash, id);
    used_.emplace_back(0);
  }
  start_ = renumber[start_];
  ++epoch_;
}

// --- LazyDfa::Lock ----------------------------------------------------------

template <typename Ready>
void LazyDfa::Lock::wait(int& tries, Ready ready) {
  if (++tries <= kSpins) return;
  if (tries <= kSpins + kYields) {
    std::this_thread::yield();
    return;
  }
  std::unique_lock<std::mutex> lock(sleep_->mutex);
  sleepers_.fetch_add(1);
  while (!ready()) sleep_->woken.wait(lock);
  sleepers_.fetch_sub(1);
  tries = 0;
}

void LazyDfa::Lock::lock_slowly() {
  writers_.fetch_add(1, std::memory_order_relaxed);
  for (int tries = 0;; wait(tries, [&] { return state_.load() == 0; })) {
    int idle = 0;
    if (state_.compare_exchange_weak(idle, -1, std::memory_order_acquire)) break;
  }
  writers_.fetch_sub(1, std::memory_order_relaxed);
}

void LazyDfa::Lock::lock_shared_slowly() {
  for (int tries = 0; !try_lock_shared();)
    wait(tries, [&] { return writers_.load() == 0 && state_.load() >= 0; });
}

}  // namespace detail
}  // namespace nlu

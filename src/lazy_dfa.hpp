// Phase 1 of matching: a lazily built DFA over the union of all rules, which
// finds the rules that accept an input. Its cost per character does not grow
// with the number of rules once the relevant DFA states are cached.
#ifndef NLU_SCRIPT_LAZY_DFA_HPP
#define NLU_SCRIPT_LAZY_DFA_HPP

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>  // std::shared_lock
#include <string>
#include <unordered_map>
#include <vector>

#include "nfa.hpp"
#include "nlu_script/nlu_script.hpp"

namespace nlu {
namespace detail {

// Computes the DFA's new transitions a rule at a time. Rules own consecutive
// ranges of states and their edges stay within them, so each rule's part of
// a set steps on its own. Parts repeat far more than whole sets: while a few
// rules take an input, the others wait in one of a few places (at their
// start, inside a wildcard). So the stepper keeps the parts it has seen, and
// where each goes on each character, and mostly looks them up instead of
// following their edges. Parts of a state or two step together directly,
// which costs less than looking them up (in scripts of many short rules,
// most parts are like that). One run() uses a stepper at a time.
class PartStepper {
 public:
  // `rule_of` gives the rule of each state (-1 for the start state).
  PartStepper(const Nfa& nfa, const std::vector<int>& rule_of) : closure_(nfa), rule_of_(rule_of) {}

  std::vector<int> operator()(std::vector<int> stack, char32_t prev) {
    return closure_(std::move(stack), prev);
  }

  // Closure::step(set, c).
  std::vector<int> step(const std::vector<int>& set, char32_t c);

 private:
  // Parts of fewer states step directly, and so do sets of fewer states.
  static constexpr std::size_t kMinKept = 3;
  static constexpr std::size_t kMinSplit = 32;
  // Plenty for scripts of thousands of rules; past it, the stepper starts over.
  static constexpr std::size_t kMaxStates = std::size_t{1} << 20;
  static constexpr std::size_t kMaxTransitions = std::size_t{1} << 18;

  struct Part {
    std::size_t begin;  // its states are states_[begin, begin + size)
    std::size_t size;
  };
  // A rule's part of a set: one that is kept, or (part -1) one that steps
  // directly.
  struct Piece {
    int part;
    int first;         // its first state
    std::size_t size;  // its number of states
  };

  Closure closure_;
  const std::vector<int>& rule_of_;
  std::vector<int> states_;
  std::vector<Part> parts_;
  std::unordered_multimap<std::size_t, int> index_;  // hash of a part's states -> part
  std::unordered_map<std::uint64_t, int> next_;     // part, character -> part, or -1 for none
  std::vector<int> last_;           // the set step() returned last, if it has kept parts
  std::vector<Piece> last_pieces_;  // its pieces, in order
  std::vector<int> direct_;         // scratch

  void forget();

  // Whether `set` is large, and its rules' parts hold kMinKept states on
  // average, at least: it has that many per rule from its first state's to
  // its last's. Small sets, and sets of thousands of rules a state or two
  // each (in scripts of many short rules), step faster directly.
  bool worth_splitting(const std::vector<int>& set) const;

  // The rules' parts of `set`, in order.
  std::vector<Piece> pieces(const std::vector<int>& set);

  // The part of the states [begin, end).
  int intern(const int* begin, const int* end);

  // The part that part p goes to on c, or -1 if it goes nowhere.
  int next(int p, char32_t c);
};

// Threads share one cache. Following cached transitions takes a shared lock.
// A thread that needs new transitions computes them without the lock and
// takes the exclusive lock only to add them, then goes back to the shared
// lock. When the cache reaches its limits it keeps the states used since the
// last eviction, up to half of each limit, and drops the rest, so states
// every input passes through survive.
//
// run() and what it does for every character stay in this header, so that
// they inline into the callers; computing and adding new states is in
// lazy_dfa.cpp.
class LazyDfa {
 public:
  // `rule_of` gives the rule of each state (-1 for the start state).
  LazyDfa(const Nfa& nfa, const std::vector<int>& rule_of, int start, const CacheOptions& options);

  // Runs the DFA over text[from, text.size()). Calls visit(k, accepts) at the
  // start and after each character, where `accepts` lists the rules accepting
  // text[from, k), ascending. Stops when visit returns false or no rule can
  // match a longer text. visit mustn't use the DFA.
  template <typename Visit>
  void run(const std::u32string& text, std::size_t from, Visit visit) {
    Borrowed closure(*this);
    std::shared_lock<Lock> shared(mutex_);
    std::unique_lock<Lock> exclusive(mutex_, std::defer_lock);
    int d = start_;
    touch(d);
    if (!visit(from, states_[d].accepts)) return;
    // Missing transitions are computed this many at a time, doubling while
    // the state reached has no cached transition either: past one new
    // state, the next is usually new too, and each switch of locks costs.
    std::size_t batch = 1;
    std::vector<NewState> chain;
    std::vector<int> set;  // a copy of the current state's set, to use without the lock
    for (std::size_t k = from; k < text.size() && !states_[d].set.empty();) {
      auto it = states_[d].next.find(text[k]);
      if (it != states_[d].next.end()) {
        d = it->second;
        if (exclusive.owns_lock()) {
          d = to_shared(d, set, exclusive, shared);
          batch = 1;
        }
        touch(d);
        if (!visit(++k, states_[d].accepts)) return;
        continue;
      }
      // Eviction renumbers states, so `d` stays valid only within `epoch`;
      // the set is the state itself, whatever its number.
      set = states_[d].set;
      const unsigned epoch = epoch_;
      if (exclusive.owns_lock()) {
        exclusive.unlock();
      } else {
        shared.unlock();
      }
      const std::size_t first = k;
      bool done = false;
      chain.clear();
      while (!done && chain.size() < batch) {
        const std::vector<int>& last = chain.empty() ? set : chain.back().set;
        chain.push_back(make_state(closure.get().step(last, text[k])));
        ++k;
        done = !visit(k, chain.back().accepts) || chain.back().set.empty() || k == text.size();
      }
      exclusive.lock();
      if (epoch != epoch_) d = -1;
      for (std::size_t i = 0; i < chain.size(); ++i) {
        d = add(d, text[first + i], std::move(chain[i]));
        touch(d);
      }
      if (done) return;
      batch = std::min<std::size_t>(batch * 2, kMaxBatch);
    }
  }

  // Returns the ids of the rules accepting the whole text, ascending.
  std::vector<int> accepted_rules(const std::u32string& text) {
    std::vector<int> out;
    run(text, 0, [&](std::size_t k, const std::vector<int>& accepts) {
      if (k == text.size()) out = accepts;
      return true;
    });
    return out;
  }

  CacheStats stats();

 private:
  static constexpr std::size_t kTransitionBytes = 32;  // an unordered_map node
  static constexpr std::size_t kMaxBatch = 64;

  struct NewState {
    std::vector<int> set;  // NFA states with consuming edges or accepting, sorted
    std::size_t hash;
    std::vector<int> accepts;  // accepted rules
  };

  struct DState : NewState {
    std::unordered_map<char32_t, int> next;
  };

  // A PartStepper lent to one run() while it computes transitions, taken
  // from the pool on first use. Each needs scratch space the size of the NFA,
  // so the pool holds as many as threads have computed transitions at once.
  class Borrowed {
   public:
    explicit Borrowed(LazyDfa& dfa) : dfa_(dfa) {}
    ~Borrowed() {
      if (!closure_) return;
      std::lock_guard<std::mutex> lock(dfa_.closures_mutex_);
      dfa_.closures_.push_back(std::move(closure_));
    }
    PartStepper& get() {
      if (closure_) return *closure_;
      {
        std::lock_guard<std::mutex> lock(dfa_.closures_mutex_);
        if (!dfa_.closures_.empty()) {
          closure_ = std::move(dfa_.closures_.back());
          dfa_.closures_.pop_back();
        }
      }
      if (!closure_) closure_ = std::make_unique<PartStepper>(dfa_.nfa_, dfa_.rule_of_);
      return *closure_;
    }

   private:
    LazyDfa& dfa_;
    std::unique_ptr<PartStepper> closure_;
  };

  // A reader-writer lock for short exclusive sections. A thread waiting for
  // it spins, then yields, then sleeps until a thread releases it, so threads
  // waiting out a long eviction don't keep cores busy. (std::shared_mutex,
  // with libc++, sleeps at once, and readers slept through every state
  // another thread added.) Waiting writers keep new readers out. A
  // per-thread cache, which only its thread uses, skips the lock.
  class Lock {
   public:
    explicit Lock(bool used) : used_(used) {}
    void lock() {
      int idle = 0;
      if (used_ && !state_.compare_exchange_strong(idle, -1, std::memory_order_acquire))
        lock_slowly();
    }
    void unlock() {
      if (!used_) return;
      state_.store(0);
      wake();
    }
    void lock_shared() {
      if (used_ && !try_lock_shared()) lock_shared_slowly();
    }
    void unlock_shared() {
      if (used_ && state_.fetch_sub(1) == 1) wake();
    }

   private:
    static constexpr int kSpins = 64, kYields = 64;

    bool try_lock_shared() {
      int readers = state_.load(std::memory_order_relaxed);
      return writers_.load(std::memory_order_relaxed) == 0 && readers >= 0 &&
             state_.compare_exchange_weak(readers, readers + 1, std::memory_order_acquire);
    }

    void lock_slowly();
    void lock_shared_slowly();

    // Called after each failed attempt; sleeps, once spinning and yielding
    // haven't helped, until `ready` holds. Sleepers count themselves before
    // checking `ready` and releasers change the state before checking the
    // count (both sequentially consistent), so a release either sees the
    // sleeper or happens before its check.
    template <typename Ready>
    void wait(int& tries, Ready ready);

    void wake() {
      if (sleepers_.load() == 0) return;
      std::lock_guard<std::mutex> lock(sleep_->mutex);
      sleep_->woken.notify_all();
    }

    std::atomic<int> state_{0};    // readers holding the lock, or -1 for a writer
    std::atomic<int> writers_{0};  // writers waiting for it
    std::atomic<int> sleepers_{0};
    // Out of line: inside the lock they spread the cache's hot fields over
    // more cache lines, and matching a cached input got about 4% slower.
    struct Sleep {
      std::mutex mutex;
      std::condition_variable woken;
    };
    const std::unique_ptr<Sleep> sleep_ = std::make_unique<Sleep>();
    const bool used_;
  };

  const Nfa& nfa_;
  const std::vector<int>& rule_of_;
  std::mutex closures_mutex_;
  std::vector<std::unique_ptr<PartStepper>> closures_;  // guarded by closures_mutex_
  // Everything below is guarded by mutex_, except that readers holding the
  // shared lock update used_.
  std::size_t max_states_, max_bytes_;
  Lock mutex_;
  int start_ = 0;
  unsigned epoch_ = 0;  // bumped by evict(), which renumbers states
  std::vector<DState> states_;
  std::unordered_multimap<std::size_t, int> index_;  // hash of a state's set -> state
  // The round in which each state was last used; a deque keeps the atomics put.
  std::deque<std::atomic<unsigned>> used_;
  unsigned round_ = 1;  // bumped by each eviction
  std::size_t transitions_ = 0;
  std::size_t bytes_ = 0;
  std::size_t evictions_ = 0;

  // Marks state `d` used in this round. Only the first use in a round writes,
  // so states every input passes through stay quiet.
  void touch(int d) {
    std::atomic<unsigned>& u = used_[d];
    if (u.load(std::memory_order_relaxed) != round_) u.store(round_, std::memory_order_relaxed);
  }

  // Everything about a state that needs no lock to compute.
  NewState make_state(std::vector<int> set) const;

  static std::size_t state_bytes(const DState& d);

  // The state with this set, or -1.
  int find(const std::vector<int>& set, std::size_t hash) const;

  int intern(NewState s);

  // Under the exclusive lock: returns the state `next`, which state `from`
  // (-1 if it was evicted meanwhile) reaches on `c`, and caches the
  // transition. A full cache evicts first.
  int add(int from, char32_t c, NewState next);

  // Trades the exclusive lock for the shared one; returns the number of
  // state `d` then, which is interned again if it was evicted meanwhile.
  // `set` is scratch space.
  int to_shared(int d, std::vector<int>& set, std::unique_lock<Lock>& exclusive,
                std::shared_lock<Lock>& shared);

  // Keeps the start state and the states used in this round, up to half of
  // each limit (in order of creation, so shallow states come first), with
  // the transitions between them; drops the rest.
  void evict();
};

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_LAZY_DFA_HPP

#include "lint.hpp"

#include <algorithm>
#include <iterator>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <utility>

#include "unicode.hpp"

namespace nlu {
namespace detail {

namespace {

// What the input ended with. Normalized input never starts with a space,
// never has two in a row and never ends with one.
enum Prev { kStart, kSpace, kWord };

// Costs order inputs by the number of characters, then by how many of them
// directly follow another character (so a space goes wherever the patterns
// allow one), then by how the characters rank: the placeholder `x` first, so
// wildcards read as `x` rather than borrowing characters from other rules,
// then placeholder syllables, then characters the patterns mention. Inputs
// are far shorter than 2^20 characters.
using Cost = long long;

enum Rank { kPlaceholder, kSyllablePlaceholder, kMentioned };

struct Transition {
  char32_t ch;
  int rank;
  int to;
};

Cost step_cost(const Transition& t, int prev) {
  if (t.ch == U' ') return 0;
  return (Cost{1} << 40) + (prev == kWord ? Cost{1} << 20 : 0) + t.rank;
}

// Subset construction over the whole script, built on demand. Transitions
// are listed per state over its own alphabet: every character its edges
// mention, the space, and one character standing for all the others.
class Determinizer {
 public:
  Determinizer(const Nfa& nfa, std::size_t max_states) : nfa_(nfa), closure_(nfa),
                                                        max_states_(max_states) {}

  // The state the input starts in, from NFA state `start`.
  int start(int start) { return intern(closure_({start}, 0)); }

  const std::vector<int>& accepts(int d) const { return accepts_[d]; }
  bool complete() const { return complete_; }

  const std::vector<Transition>& transitions(int d) {
    if (expanded_[d]) return transitions_[d];
    expanded_[d] = true;
    std::set<char32_t> chars;
    bool any = false;
    for (int s : sets_[d]) {
      for (const Edge& e : nfa_.states[s].edges) {
        if (e.kind == Edge::Kind::Char) chars.insert(e.ch);
        if (e.kind == Edge::Kind::Space) chars.insert(U' ');
        if (e.kind == Edge::Kind::Any) {
          any = true;
          if (e.set >= 0) chars.insert(nfa_.sets[e.set].begin(), nfa_.sets[e.set].end());
        }
      }
    }
    const std::vector<char32_t> mentioned_chars(chars.begin(), chars.end());
    std::vector<char32_t> alphabet = mentioned_chars;
    const std::size_t mentioned = alphabet.size();
    if (any) {
      alphabet.push_back(unmentioned(mentioned_chars, CharClass::All));
      // Guards tell syllables apart by their final consonant, so each kind
      // needs a character of its own.
      if (nfa_.has_guards)
        for (CharClass cls : {CharClass::SyllableNoFinal, CharClass::SyllableRieul,
                              CharClass::SyllableOtherFinal})
          alphabet.push_back(unmentioned(mentioned_chars, cls));
    }
    std::vector<Transition> out;
    for (std::size_t i = 0; i < alphabet.size(); ++i) {
      const char32_t c = alphabet[i];
      int next = intern(closure_.step(sets_[d], c));
      const int rank = i < mentioned ? kMentioned
                       : i == mentioned ? kPlaceholder
                                        : kSyllablePlaceholder;
      if (next >= 0) out.push_back({c, rank, next});
    }
    transitions_[d] = std::move(out);
    return transitions_[d];
  }

 private:
  // Returns the state for `set`, a closure, or -1 for the dead state or when
  // the state limit is reached.
  int intern(std::vector<int> set) {
    if (set.empty()) return -1;
    auto it = index_.find(set);
    if (it != index_.end()) return it->second;
    if (sets_.size() >= max_states_) {
      complete_ = false;
      return -1;
    }
    int id = static_cast<int>(sets_.size());
    index_.emplace(set, id);
    accepts_.push_back(nfa_.accepted_rules(set));
    sets_.push_back(std::move(set));
    transitions_.emplace_back();
    expanded_.push_back(false);
    return id;
  }

  const Nfa& nfa_;
  Closure closure_;
  std::size_t max_states_;
  bool complete_ = true;
  std::map<std::vector<int>, int> index_;
  std::vector<std::vector<int>> sets_;
  std::vector<std::vector<int>> accepts_;
  std::vector<std::vector<Transition>> transitions_;
  std::vector<bool> expanded_;
};

}  // namespace

RuleAnalysis analyze_rules(const Nfa& nfa, int start, const std::vector<RuleSource>& rules,
                           std::size_t max_states) {
  RuleAnalysis out;
  Determinizer dfa(nfa, max_states);
  const int n = static_cast<int>(rules.size());

  // Dijkstra over (DFA state, what the input ended with).
  struct Node {
    int state;
    int prev;
    int parent;  // node reached before this one, -1 at the start
    char32_t ch;  // character consumed from the parent
  };
  std::vector<Node> nodes;
  std::vector<Cost> cost;
  std::map<std::pair<int, int>, int> index;
  using Entry = std::pair<Cost, int>;  // (cost, node)
  std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
  auto reach = [&](int state, int prev, int parent, char32_t ch, Cost c) {
    auto key = std::make_pair(state, prev);
    auto it = index.find(key);
    if (it != index.end() && cost[it->second] <= c) return;
    int id;
    if (it == index.end()) {
      id = static_cast<int>(nodes.size());
      index.emplace(key, id);
      nodes.push_back({state, prev, parent, ch});
      cost.push_back(c);
    } else {
      id = it->second;
      nodes[id] = {state, prev, parent, ch};
      cost[id] = c;
    }
    queue.push({c, id});
  };
  auto example_of = [&](int id) {
    std::u32string text;
    for (; nodes[id].parent >= 0; id = nodes[id].parent) text += nodes[id].ch;
    std::reverse(text.begin(), text.end());
    return to_utf8(text);
  };

  // Overlaps, by label pair: the overlap (as found first) and whether each
  // label always wins match_best() on the inputs seen so far.
  struct Pair {
    std::size_t overlap;
    bool first_wins = true, second_wins = true;
  };
  std::map<std::pair<std::string, std::string>, Pair> pairs;
  // A pair is keyed by its labels in the order of their first rules, so it is
  // the same pair whichever order a state lists them in.
  std::map<std::string, int> rank;
  for (int r = n - 1; r >= 0; --r) rank[rules[r].label] = r;
  std::vector<std::pair<int, int>> order;  // the rules of each overlap
  // Dead rules: whether some input reaches the rule before any earlier rule
  // of its label, and which earlier rules accepted every input it did.
  std::vector<bool> used(n, false), seen(n, false);
  std::vector<std::vector<int>> covered_by(n);

  int first = dfa.start(start);
  if (first >= 0) reach(first, kStart, -1, 0, 0);
  while (!queue.empty()) {
    Entry top = queue.top();
    queue.pop();
    const int id = top.second;
    if (top.first != cost[id]) continue;  // superseded
    const Node node = nodes[id];

    if (node.prev != kSpace) {
      // The rules accepting here, and the labels whose wildcard-free copies do.
      std::vector<int> accepted;
      std::set<std::string> literal_labels;
      for (int rule : dfa.accepts(node.state)) {
        if (rule < n) {
          accepted.push_back(rule);
        } else {
          literal_labels.insert(rules[rule - n].label);
        }
      }
      // The first rule of each label, in script order.
      std::vector<int> firsts;
      std::set<std::string> labels_here;
      for (int rule : accepted)
        if (labels_here.insert(rules[rule].label).second) firsts.push_back(rule);
      auto wins = [&](const std::string& label, const std::string& other) {
        bool literal = false;
        for (int rule : accepted) literal = literal || (rules[rule].label == label && rules[rule].wildcard_free);
        return literal && !literal_labels.count(other);
      };
      for (std::size_t i = 0; i < firsts.size(); ++i) {
        for (std::size_t k = i + 1; k < firsts.size(); ++k) {
          int a = firsts[i], b = firsts[k];
          if (rank[rules[a].label] > rank[rules[b].label]) std::swap(a, b);
          const RuleSource& x = rules[a];
          const RuleSource& y = rules[b];
          auto key = std::make_pair(x.label, y.label);
          auto it = pairs.find(key);
          if (it == pairs.end()) {
            it = pairs.emplace(key, Pair{out.overlaps.size()}).first;
            order.emplace_back(a, b);
            out.overlaps.push_back(
                {x.label, x.line, y.label, y.line, example_of(id), x.file, y.file, ""});
          }
          it->second.first_wins = it->second.first_wins && wins(x.label, y.label);
          it->second.second_wins = it->second.second_wins && wins(y.label, x.label);
        }
      }
      for (int rule : accepted) {
        std::vector<int> earlier;
        for (int other : accepted)
          if (other < rule && rules[other].label == rules[rule].label) earlier.push_back(other);
        if (earlier.empty()) used[rule] = true;
        if (!seen[rule]) {
          covered_by[rule] = earlier;
        } else {
          std::vector<int> both;
          std::set_intersection(covered_by[rule].begin(), covered_by[rule].end(), earlier.begin(),
                                earlier.end(), std::back_inserter(both));
          covered_by[rule] = std::move(both);
        }
        seen[rule] = true;
      }
    }

    for (const Transition& t : dfa.transitions(node.state)) {
      if (t.ch == U' ' && node.prev != kWord) continue;
      reach(t.to, t.ch == U' ' ? kSpace : kWord, id, t.ch,
            top.first + step_cost(t, node.prev));
    }
  }

  for (const auto& kv : pairs) {
    Overlap& o = out.overlaps[kv.second.overlap];
    if (kv.second.first_wins) o.best_label = o.label;
    if (kv.second.second_wins) o.best_label = o.other_label;
  }
  // In script order of the rules (their ids).
  std::vector<std::size_t> by_rules(out.overlaps.size());
  for (std::size_t k = 0; k < by_rules.size(); ++k) by_rules[k] = k;
  std::sort(by_rules.begin(), by_rules.end(),
            [&](std::size_t x, std::size_t y) { return order[x] < order[y]; });
  std::vector<Overlap> sorted;
  for (std::size_t k : by_rules) sorted.push_back(std::move(out.overlaps[k]));
  out.overlaps = std::move(sorted);

  out.complete = dfa.complete();
  if (out.complete) {
    for (int r = 0; r < n; ++r) {
      if (used[r]) continue;
      const RuleSource& rule = rules[r];
      DeadRule d{rule.label, rule.line, rule.file, !seen[r], 0, ""};
      if (seen[r] && !covered_by[r].empty()) {
        d.covered_by_line = rules[covered_by[r].front()].line;
        d.covered_by_file = rules[covered_by[r].front()].file;
      }
      out.dead_rules.push_back(std::move(d));
    }
  }
  return out;
}

}  // namespace detail
}  // namespace nlu

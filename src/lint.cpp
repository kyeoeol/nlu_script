#include "lint.hpp"

#include <algorithm>
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

  // Returns the state for the closure of `targets` after consuming `prev`,
  // -1 for the dead state or when the state limit is reached.
  int intern(std::vector<int> targets, char32_t prev) {
    std::vector<int> set = closure_(std::move(targets), prev);
    if (set.empty()) return -1;
    auto it = index_.find(set);
    if (it != index_.end()) return it->second;
    if (sets_.size() >= max_states_) {
      complete_ = false;
      return -1;
    }
    std::vector<int> accepts;
    for (int s : set)
      if (nfa_.states[s].accept_rule >= 0) accepts.push_back(nfa_.states[s].accept_rule);
    std::sort(accepts.begin(), accepts.end());
    int id = static_cast<int>(sets_.size());
    index_.emplace(set, id);
    sets_.push_back(std::move(set));
    accepts_.push_back(std::move(accepts));
    transitions_.emplace_back();
    expanded_.push_back(false);
    return id;
  }

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
    std::vector<char32_t> alphabet(chars.begin(), chars.end());
    const std::size_t mentioned = alphabet.size();
    if (any) {
      alphabet.push_back(other_than(chars));
      // Guards tell syllables apart by their final consonant, so each kind
      // needs a character of its own.
      if (nfa_.has_guards)
        for (char32_t first : {U'가', U'갈', U'각'})
          alphabet.push_back(syllable_other_than(chars, first));
    }
    std::vector<Transition> out;
    for (std::size_t i = 0; i < alphabet.size(); ++i) {
      const char32_t c = alphabet[i];
      std::vector<int> targets;
      for (int s : sets_[d])
        for (const Edge& e : nfa_.states[s].edges)
          if (e.consumes() && nfa_.matches(e, c)) targets.push_back(e.to);
      int next = intern(std::move(targets), c);
      const int rank = i < mentioned ? kMentioned
                       : i == mentioned ? kPlaceholder
                                        : kSyllablePlaceholder;
      if (next >= 0) out.push_back({c, rank, next});
    }
    transitions_[d] = std::move(out);
    return transitions_[d];
  }

 private:
  // A readable character that no edge of the state mentions, so it stands
  // for every such character.
  static char32_t other_than(const std::set<char32_t>& chars) {
    for (char32_t c : std::u32string(U"xyzqjkvw"))
      if (!chars.count(c)) return c;
    char32_t c = 0x4E00;
    while (chars.count(c)) ++c;
    return c;
  }

  // The first syllable from `first` on, with the same final consonant, that
  // no edge of the state mentions.
  static char32_t syllable_other_than(const std::set<char32_t>& chars, char32_t first) {
    char32_t c = first;
    while (chars.count(c)) c += 28;
    return c;
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

OverlapSearch find_overlaps(const Nfa& nfa, int start, const std::vector<RuleSource>& rules,
                            std::size_t max_states) {
  OverlapSearch out;
  Determinizer dfa(nfa, max_states);

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

  int first = dfa.intern({start}, 0);
  if (first >= 0) reach(first, kStart, -1, 0, 0);
  std::set<std::pair<std::string, std::string>> found;
  std::vector<std::pair<int, int>> order;  // the rules of each overlap
  while (!queue.empty()) {
    Entry top = queue.top();
    queue.pop();
    const int id = top.second;
    if (top.first != cost[id]) continue;  // superseded
    const Node node = nodes[id];

    if (node.prev != kSpace) {
      // The first rule of each label that accepts here, in script order.
      std::vector<int> firsts;
      std::set<std::string> seen;
      for (int rule : dfa.accepts(node.state))
        if (seen.insert(rules[rule].label).second) firsts.push_back(rule);
      for (std::size_t i = 0; i < firsts.size(); ++i) {
        for (std::size_t k = i + 1; k < firsts.size(); ++k) {
          const int a = firsts[i], b = firsts[k];
          const RuleSource& x = rules[a];
          const RuleSource& y = rules[b];
          if (!found.insert({x.label, y.label}).second) continue;
          order.emplace_back(a, b);
          out.overlaps.push_back(
              {x.label, x.line, y.label, y.line, example_of(id), x.file, y.file});
        }
      }
    }

    for (const Transition& t : dfa.transitions(node.state)) {
      if (t.ch == U' ' && node.prev != kWord) continue;
      reach(t.to, t.ch == U' ' ? kSpace : kWord, id, t.ch,
            top.first + step_cost(t, node.prev));
    }
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
  return out;
}

}  // namespace detail
}  // namespace nlu

#include "lint.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <queue>
#include <unordered_map>
#include <utility>

#include "compiler.hpp"
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

Cost step_cost(char32_t ch, int rank, int prev) {
  if (ch == U' ') return 0;
  return (Cost{1} << 40) + (prev == kWord ? Cost{1} << 20 : 0) + rank;
}

// More characters than any input needs: none will do.
constexpr int kFar = 1 << 20;

// An automaton's edges reversed: the states with an edge into each state,
// and how many characters (spaces aside) the edge takes.
struct Reversed {
  std::vector<int> begin;  // the edges into s are from[begin[s]] .. from[begin[s + 1] - 1]
  std::vector<std::pair<int, int>> from;

  explicit Reversed(const Nfa& nfa) : begin(nfa.states.size() + 1, 0) {
    for (const State& s : nfa.states)
      for (const Edge& e : s.edges) ++begin[e.to + 1];
    for (std::size_t s = 1; s < begin.size(); ++s) begin[s] += begin[s - 1];
    from.resize(begin.back());
    std::vector<int> next(begin.begin(), begin.end() - 1);
    for (int s = 0; s < static_cast<int>(nfa.states.size()); ++s)
      for (const Edge& e : nfa.states[s].edges)
        from[next[e.to]++] = {s, e.kind == Edge::Kind::Char || e.kind == Edge::Kind::Any ? 1 : 0};
  }
};

// The fewest characters, spaces aside, from each state to an accepting one
// (kFar if there is none).
std::vector<int> distances(const Nfa& nfa, const Reversed& into) {
  std::vector<int> distance(nfa.states.size(), kFar);
  std::deque<int> queue;
  for (int s = 0; s < static_cast<int>(nfa.states.size()); ++s) {
    if (nfa.states[s].accept_rule < 0) continue;
    distance[s] = 0;
    queue.push_back(s);
  }
  while (!queue.empty()) {
    const int s = queue.front();
    queue.pop_front();
    for (int k = into.begin[s]; k < into.begin[s + 1]; ++k) {
      const auto& from = into.from[k];
      if (distance[s] + from.second >= distance[from.first]) continue;
      distance[from.first] = distance[s] + from.second;
      if (from.second) {
        queue.push_back(from.first);
      } else {
        queue.push_front(from.first);
      }
    }
  }
  return distance;
}

// The post-dominators of the states lo .. lo + m - 1 of a rule, which accepts
// at lo + exit: the dominators of the reversed graph (Cooper, Harvey and
// Kennedy's algorithm). Returns each state's immediate post-dominator, as an
// offset from lo, or -1 if no path goes from it to the accepting state; and
// in `order`, the reversed graph's postorder.
std::vector<int> post_dominators(const Nfa& nfa, const Reversed& into, int lo, int m, int exit,
                                 std::vector<int>& order) {
  std::vector<int> number(m, -1);  // in the postorder
  std::vector<std::pair<int, int>> stack{{exit, into.begin[lo + exit]}};
  number[exit] = 0;
  while (!stack.empty()) {
    const int v = stack.back().first;
    if (stack.back().second < into.begin[lo + v + 1]) {
      const int u = into.from[stack.back().second++].first - lo;
      if (u >= 0 && u < m && number[u] < 0) {
        number[u] = 0;
        stack.push_back({u, into.begin[lo + u]});
      }
    } else {
      number[v] = static_cast<int>(order.size());
      order.push_back(v);
      stack.pop_back();
    }
  }
  std::vector<int> idom(m, -1);
  idom[exit] = exit;
  auto intersect = [&](int a, int b) {
    while (a != b) {
      while (number[a] < number[b]) a = idom[a];
      while (number[b] < number[a]) b = idom[b];
    }
    return a;
  };
  for (bool changed = true; changed;) {
    changed = false;
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
      if (*it == exit) continue;
      int d = -1;
      for (const Edge& e : nfa.states[lo + *it].edges) {
        const int w = e.to - lo;
        if (w < 0 || w >= m || idom[w] < 0) continue;
        d = d < 0 ? w : intersect(d, w);
      }
      if (d != idom[*it]) {
        idom[*it] = d;
        changed = true;
      }
    }
  }
  return idom;
}

// A state of a rule is redundant in a set that holds the word state of a
// plain wildcard that every accepting path from the state goes through: the
// wildcard can take whatever text the state would take on the way to it (a
// space at its end too, since one may follow the wildcard), and then go on
// the same way. So the set accepts the same inputs without the state. This
// keeps the sets of `<?>*? a <?>*? b <?>*?` from remembering how `a` matched
// once the wildcard before `b` has started, and those of a rule that matched
// from remembering anything else.
class Covers {
 public:
  Covers(const Nfa& nfa, const Reversed& into, const std::vector<RuleSource>& rules)
      : up_(nfa.states.size(), -1), word_(nfa.states.size(), -1), mark_(nfa.states.size(), 0) {
    for (int r = 0; r < static_cast<int>(rules.size()); ++r) {
      const int lo = rules[r].first, m = rules[r].last - lo;
      int exit = -1;
      for (int s = 0; s < m; ++s) {
        if (nfa.states[lo + s].accept_rule == r) exit = s;
        word_[lo + s] = wildcard_word(nfa, lo + s);
      }
      if (exit < 0) continue;
      std::vector<int> order;
      const std::vector<int> idom = post_dominators(nfa, into, lo, m, exit, order);
      for (auto it = order.rbegin(); it != order.rend(); ++it) {
        if (*it == exit || idom[*it] < 0) continue;
        const int d = lo + idom[*it];
        up_[lo + *it] = word_[d] >= 0 ? d : up_[d];
      }
    }
  }

  // Removes the redundant states from `set`.
  void prune(std::vector<int>& set) {
    if (++generation_ == 0) {
      std::fill(mark_.begin(), mark_.end(), 0);
      generation_ = 1;
    }
    for (int s : set) mark_[s] = generation_;
    set.erase(std::remove_if(set.begin(), set.end(),
                             [&](int s) {
                               for (int y = up_[s]; y >= 0; y = up_[y])
                                 if (mark_[word_[y]] == generation_) return true;
                               return false;
                             }),
              set.end());
  }

 private:
  // The in-state of the nearest wildcard of word_ that every accepting path
  // from the state goes through, or -1.
  std::vector<int> up_;
  std::vector<int> word_;  // see wildcard_word()
  std::vector<unsigned> mark_;
  unsigned generation_ = 0;
};

// The index in kOtherClasses of the class that an unmentioned character
// falls into: 0 unless `by_class` tells syllables apart.
int class_index(char32_t c, bool by_class) {
  if (!by_class) return 0;
  const CharClass cls = char_class(c);
  for (int k = 1; k < 4; ++k)
    if (kOtherClasses[k] == cls) return k;
  return 0;
}

// Subset construction over the rules of one label, built on demand and shared
// by the searches that need it. A state's moves are listed over its own
// alphabet: every character its edges mention (the space among them) and, if
// a wildcard takes other characters too, one standing for each class of them.
// Every automaton draws its states from one `budget`.
class Determinizer {
 public:
  static constexpr int kDropped = -2;  // a state left out for the budget

  struct Moves {
    std::vector<char32_t> chars;  // sorted
    std::vector<int> to;          // the state after each: -1 if none, or kDropped
    bool any = false;             // whether other characters lead somewhere
    int other[4] = {-1, -1, -1, -1};  // the state after other characters, by class_index()

    // The state after `c`, which isn't among `chars`.
    int unlisted(char32_t c, bool by_class) const {
      return c == U' ' || !any ? -1 : other[class_index(c, by_class)];
    }
  };

  Determinizer(const Nfa& nfa, Closure& closure, Covers& covers, std::size_t& budget,
               std::vector<int> starts)
      : nfa_(nfa), closure_(closure), covers_(covers), budget_(budget) {
    start_ = intern(closure_(std::move(starts), 0));
  }

  // The state the input starts in.
  int start() const { return start_; }
  // Whether guards or class-limited wildcards tell syllables apart.
  bool by_class() const { return nfa_.has_guards; }
  const std::vector<int>& set(int d) const { return sets_[d]; }
  const std::vector<int>& accepts(int d) const { return accepts_[d]; }

  const Moves& moves(int d) {
    if (expanded_[d]) return moves_[d];
    expanded_[d] = true;
    Moves m;
    for (int s : sets_[d]) {
      for (const Edge& e : nfa_.states[s].edges) {
        if (e.kind == Edge::Kind::Char) m.chars.push_back(e.ch);
        if (e.kind == Edge::Kind::Space) m.chars.push_back(U' ');
        if (e.kind == Edge::Kind::Any) {
          m.any = true;
          if (e.set >= 0) m.chars.insert(m.chars.end(), nfa_.sets[e.set].begin(), nfa_.sets[e.set].end());
        }
      }
    }
    std::sort(m.chars.begin(), m.chars.end());
    m.chars.erase(std::unique(m.chars.begin(), m.chars.end()), m.chars.end());
    for (char32_t c : m.chars) m.to.push_back(intern(closure_.step(sets_[d], c)));
    if (m.any)
      for (int k = 0; k < (by_class() ? 4 : 1); ++k)
        m.other[k] = intern(closure_.step(sets_[d], unmentioned(m.chars, kOtherClasses[k])));
    moves_[d] = std::move(m);
    return moves_[d];
  }

 private:
  // Returns the state for `set`, a closure: -1 for the dead state, or
  // kDropped once the budget is spent.
  int intern(std::vector<int> set) {
    if (set.empty()) return -1;
    covers_.prune(set);
    auto it = index_.find(set);
    if (it != index_.end()) return it->second;
    if (budget_ == 0) return kDropped;
    --budget_;
    int id = static_cast<int>(sets_.size());
    index_.emplace(set, id);
    accepts_.push_back(nfa_.accepted_rules(set));
    sets_.push_back(std::move(set));
    moves_.emplace_back();
    expanded_.push_back(false);
    return id;
  }

  const Nfa& nfa_;
  Closure& closure_;
  Covers& covers_;
  std::size_t& budget_;
  int start_;
  std::map<std::vector<int>, int> index_;
  std::vector<std::vector<int>> sets_;
  std::vector<std::vector<int>> accepts_;
  std::vector<Moves> moves_;
  std::vector<bool> expanded_;
};

// A* over the inputs of one label's automaton, or of two labels' at once (the
// product, so an input both match), with what the input ended with: visits
// the states where an input can end, in order of the cost of the cheapest
// input reaching them plus a lower bound on what an input needs from there
// on, `need` characters. The first state with a need of 0 it visits is
// reached by a cheapest input. It doesn't go past states that need kFar.
// Nodes draw from the automata's budget.
class Search {
 public:
  // The NFA states of each automaton's state; the second null for one label.
  using Sets = std::array<const std::vector<int>*, 2>;

  Search(std::array<Determinizer*, 2> parts, std::size_t& budget,
         std::function<int(const Sets&)> need)
      : parts_(parts), count_(parts[1] ? 2 : 1), budget_(budget), need_(std::move(need)) {
    std::array<int, 2> start = {-1, -1};
    for (int k = 0; k < count_; ++k) {
      start[k] = parts_[k]->start();
      if (!live(start[k])) return;
    }
    reach(start, kStart, -1, 0, 0);
  }

  // Moves to the next state an input can end in; false when there is none.
  bool next() {
    if (current_ >= 0) expand(current_);
    current_ = -1;
    while (!queue_.empty()) {
      const Entry top = queue_.top();
      queue_.pop();
      if (top.first != key_[top.second]) continue;  // superseded
      if (nodes_[top.second].prev != kSpace) {
        current_ = top.second;
        return true;
      }
      expand(top.second);
    }
    return false;
  }

  // The rules of automaton `k` that accept the current input, sorted.
  const std::vector<int>& accepts(int k) const {
    return parts_[k]->accepts(nodes_[current_].state[k]);
  }

  // The current input: the cheapest one that reaches its state.
  std::string example() const {
    std::u32string text;
    for (int id = current_; nodes_[id].parent >= 0; id = nodes_[id].parent) text += nodes_[id].ch;
    std::reverse(text.begin(), text.end());
    return to_utf8(text);
  }

  // Whether the search reached every state, or left some out for the budget.
  bool complete() const { return complete_; }

 private:
  struct Node {
    std::array<int, 2> state;  // in each automaton
    int prev;
    int parent;   // node reached before this one, -1 at the start
    char32_t ch;  // character consumed from the parent
  };
  using Entry = std::pair<Cost, int>;  // (cost plus estimate, node)

  std::array<Determinizer*, 2> parts_;
  int count_;
  std::size_t& budget_;
  std::function<int(const Sets&)> need_;
  bool complete_ = true;
  std::vector<Node> nodes_;
  std::vector<Cost> cost_;
  std::vector<Cost> key_;  // as queued
  std::unordered_map<std::uint64_t, int> index_;  // (states, prev) -> node
  std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue_;
  int current_ = -1;

  // Whether an automaton's next state exists; one left out for the budget
  // makes the search incomplete.
  bool live(int state) {
    if (state == Determinizer::kDropped) complete_ = false;
    return state >= 0;
  }

  Sets sets(const Node& node) const {
    Sets out = {nullptr, nullptr};
    for (int k = 0; k < count_; ++k) out[k] = &parts_[k]->set(node.state[k]);
    return out;
  }

  void reach(std::array<int, 2> state, int prev, int parent, char32_t ch, Cost c) {
    const std::uint64_t key = (static_cast<std::uint64_t>(state[0]) * 3 + prev) << 32 |
                              static_cast<std::uint32_t>(state[1] + 1);
    auto it = index_.find(key);
    if (it != index_.end() && cost_[it->second] <= c) return;
    int id;
    if (it == index_.end()) {
      if (budget_ == 0) {
        complete_ = false;
        return;
      }
      --budget_;
      id = static_cast<int>(nodes_.size());
      index_.emplace(key, id);
      nodes_.push_back({state, prev, parent, ch});
      cost_.push_back(c);
      key_.push_back(c + (Cost{need_(sets(nodes_.back()))} << 40));
    } else {
      id = it->second;
      key_[id] += c - cost_[id];
      nodes_[id] = {state, prev, parent, ch};
      cost_[id] = c;
    }
    queue_.push({key_[id], id});
  }

  void expand(int id) {
    const Node node = nodes_[id];
    if (need_(sets(node)) >= kFar) return;
    const bool by_class = parts_[0]->by_class();
    const Determinizer::Moves* moves[2] = {nullptr, nullptr};
    bool any = false;
    for (int k = 0; k < count_; ++k) {
      moves[k] = &parts_[k]->moves(node.state[k]);
      any = any || moves[k]->any;
    }
    auto step = [&](char32_t c, const std::array<int, 2>& to, int rank) {
      if (c == U' ' && node.prev != kWord) return;
      for (int k = 0; k < count_; ++k)
        if (!live(to[k])) return;
      reach(to, c == U' ' ? kSpace : kWord, id, c, cost_[id] + step_cost(c, rank, node.prev));
    };
    // Every character either automaton mentions, in order, then one standing
    // for each class of the others.
    std::vector<char32_t> chars;
    std::size_t at[2] = {0, 0};
    for (;;) {
      char32_t c = 0;
      bool more = false;
      for (int k = 0; k < count_; ++k) {
        if (at[k] == moves[k]->chars.size()) continue;
        if (!more || moves[k]->chars[at[k]] < c) c = moves[k]->chars[at[k]];
        more = true;
      }
      if (!more) break;
      std::array<int, 2> to = {-1, -1};
      for (int k = 0; k < count_; ++k) {
        const Determinizer::Moves& m = *moves[k];
        to[k] = at[k] < m.chars.size() && m.chars[at[k]] == c ? m.to[at[k]++] : m.unlisted(c, by_class);
      }
      chars.push_back(c);
      step(c, to, kMentioned);
    }
    if (!any) return;
    for (int cls = 0; cls < (by_class ? 4 : 1); ++cls) {
      const char32_t c = unmentioned(chars, kOtherClasses[cls]);
      std::array<int, 2> to = {-1, -1};
      for (int k = 0; k < count_; ++k) to[k] = moves[k]->unlisted(c, by_class);
      step(c, to, cls == 0 ? kPlaceholder : kSyllablePlaceholder);
    }
  }
};

}  // namespace

RuleAnalysis analyze_rules(const Nfa& script, const std::vector<RuleSource>& rules,
                           std::size_t max_states) {
  RuleAnalysis out;
  const int n = static_cast<int>(rules.size());

  // The rule each of the script's states belongs to, and the fewest
  // characters from each to its rule's end.
  std::vector<int> owner(script.states.size(), -1);
  for (int r = 0; r < n; ++r)
    for (int s = rules[r].first; s < rules[r].last; ++s) owner[s] = r;
  const Reversed into(script);
  const std::vector<int> distance = distances(script, into);
  // The fewest characters an input needs from `set` to match one of the
  // rules `wanted` picks, or kFar.
  auto nearest = [&](const std::vector<int>& set, const std::function<bool(int)>& wanted) {
    int need = kFar;
    for (int s : set)
      if (s < static_cast<int>(owner.size()) && owner[s] >= 0 && wanted(owner[s]))
        need = std::min(need, distance[s]);
    return need;
  };

  // The script's automaton plus, for each rule, a copy without wildcard
  // edges (accepting as rule n + r), which tells whether an input needs a
  // wildcard.
  Nfa nfa = script;
  std::vector<int> copy_start(n);
  std::vector<bool> wildcard_free(n, true);
  for (int r = 0; r < n; ++r) {
    const RuleSource& rule = rules[r];
    const int base = static_cast<int>(nfa.states.size());
    for (int s = rule.first; s < rule.last; ++s) nfa.add_state();
    for (int s = rule.first; s < rule.last; ++s) {
      State& copy = nfa.states[base + (s - rule.first)];
      for (Edge e : script.states[s].edges) {
        if (e.wildcard) {
          wildcard_free[r] = false;
          continue;
        }
        e.to = base + (e.to - rule.first);
        copy.edges.push_back(e);
      }
      if (script.states[s].accept_rule == r) copy.accept_rule = n + r;
    }
    copy_start[r] = base + (rule.start - rule.first);
  }
  Closure closure(nfa);
  Covers covers(nfa, into, rules);
  std::size_t budget = max_states;

  // The labels in the order of their first rules, each with its rules and,
  // built when a search needs them, the automata of its rules alone and of
  // its rules with their wildcard-free copies.
  struct Label {
    std::vector<int> rules;
    bool literal = false;   // some rule has no wildcards
    bool searched = false;  // for dead rules, fully
    std::unique_ptr<Determinizer> plain, with_copies;
  };
  std::vector<Label> labels;
  std::vector<int> label_of(n);
  {
    std::map<std::string, int> index;
    for (int r = 0; r < n; ++r) {
      auto it = index.emplace(rules[r].label, static_cast<int>(labels.size())).first;
      if (it->second == static_cast<int>(labels.size())) labels.emplace_back();
      labels[it->second].rules.push_back(r);
      labels[it->second].literal = labels[it->second].literal || wildcard_free[r];
      label_of[r] = it->second;
    }
  }
  auto automaton = [&](Label& label, bool copies) {
    std::unique_ptr<Determinizer>& dfa = copies ? label.with_copies : label.plain;
    if (!dfa) {
      std::vector<int> starts;
      for (int r : label.rules) {
        starts.push_back(rules[r].start);
        if (copies) starts.push_back(copy_start[r]);
      }
      dfa = std::make_unique<Determinizer>(nfa, closure, covers, budget, std::move(starts));
    }
    return dfa.get();
  };

  // Dead rules, one label at a time: whether some input reaches a rule before
  // any earlier rule of its label, and which earlier rules accepted every
  // input it did. Once a rule is used, inputs only it can go on to tell
  // nothing more. The order doesn't matter here (dead rules come with no
  // example), so the search heads for the nearest rule not used yet.
  std::vector<bool> used(n, false), seen(n, false);
  std::vector<std::vector<int>> covered_by(n);
  for (Label& label : labels) {
    std::size_t unused = label.rules.size();
    Search search({automaton(label, false), nullptr}, budget, [&](const Search::Sets& sets) {
      return nearest(*sets[0], [&](int r) { return !used[r]; });
    });
    while (unused > 0 && search.next()) {
      const std::vector<int>& accepted = search.accepts(0);
      for (int rule : accepted) {
        const std::vector<int> earlier(accepted.begin(),
                                       std::lower_bound(accepted.begin(), accepted.end(), rule));
        if (earlier.empty() && !used[rule]) {
          used[rule] = true;
          --unused;
        }
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
    label.searched = unused == 0 || search.complete();
    out.complete = out.complete && label.searched;
  }
  for (int r = 0; r < n; ++r) {
    if (used[r] || !labels[label_of[r]].searched) continue;
    const RuleSource& rule = rules[r];
    DeadRule d{rule.label, rule.line, rule.file, !seen[r], 0, ""};
    if (seen[r] && !covered_by[r].empty()) {
      d.covered_by_line = rules[covered_by[r].front()].line;
      d.covered_by_file = rules[covered_by[r].front()].file;
    }
    out.dead_rules.push_back(std::move(d));
  }

  // Overlaps, one pair of labels at a time: the cheapest input both match,
  // and whether one of them always wins match_best() on such inputs. A label
  // wins one if a rule without wildcards matches it and no rule of the other
  // label does without its wildcards, which the wildcard-free copies tell;
  // they are only searched when that can happen. An input needs as many more
  // characters as the label further from matching does.
  std::vector<std::pair<std::pair<int, int>, Overlap>> found;
  for (std::size_t a = 0; a < labels.size(); ++a) {
    for (std::size_t b = a + 1; b < labels.size(); ++b) {
      Label* side[2] = {&labels[a], &labels[b]};
      const bool literal[2] = {side[0]->literal, side[1]->literal};
      auto any_rule = [](int) { return true; };
      Search search({automaton(*side[0], literal[1]), automaton(*side[1], literal[0])}, budget,
                    [&](const Search::Sets& sets) {
                      return std::max(nearest(*sets[0], any_rule), nearest(*sets[1], any_rule));
                    });
      int first[2] = {-1, -1};
      bool wins[2] = {literal[0], literal[1]};
      bool settled = false;
      while (search.next()) {
        int rule_of[2] = {-1, -1};
        bool literal_here[2] = {false, false}, copy_here[2] = {false, false};
        for (int k = 0; k < 2; ++k) {
          for (int rule : search.accepts(k)) {
            if (rule >= n) {
              copy_here[k] = true;
              continue;
            }
            if (rule_of[k] < 0) rule_of[k] = rule;
            literal_here[k] = literal_here[k] || wildcard_free[rule];
          }
        }
        if (rule_of[0] < 0 || rule_of[1] < 0) continue;
        if (first[0] < 0) {
          first[0] = rule_of[0];
          first[1] = rule_of[1];
          const RuleSource& x = rules[first[0]];
          const RuleSource& y = rules[first[1]];
          found.push_back({{first[0], first[1]},
                           {x.label, x.line, y.label, y.line, search.example(), x.file, y.file, ""}});
        }
        for (int k = 0; k < 2; ++k) wins[k] = wins[k] && literal_here[k] && !copy_here[1 - k];
        if (!wins[0] && !wins[1]) {
          settled = true;
          break;
        }
      }
      if (!settled && !search.complete()) {
        out.complete = false;
        continue;
      }
      if (first[0] >= 0 && (wins[0] || wins[1]))
        found.back().second.best_label = rules[first[wins[0] ? 0 : 1]].label;
    }
  }
  // In script order of the rules.
  std::sort(found.begin(), found.end(),
            [](const auto& x, const auto& y) { return x.first < y.first; });
  for (auto& f : found) out.overlaps.push_back(std::move(f.second));
  return out;
}

}  // namespace detail
}  // namespace nlu

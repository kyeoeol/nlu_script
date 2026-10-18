// Nondeterministic automaton over normalized input characters.
#ifndef NLU_SCRIPT_NFA_HPP
#define NLU_SCRIPT_NFA_HPP

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace nlu {
namespace detail {

// Conditions on the previous input character, for Korean particles. After a
// character other than a Hangul syllable or the space every condition holds,
// since whether it ends in a consonant depends on how it is read.
enum class Guard : std::uint8_t {
  None,
  AfterFinal,            // a syllable with a final consonant
  AfterNoFinal,          // a syllable without one
  AfterFinalExceptRieul,  // a final consonant other than ㄹ
  AfterNoFinalOrRieul,   // no final consonant, or ㄹ
};

inline bool guard_holds(Guard g, char32_t prev) {
  if (prev == 0 || prev == U' ') return false;
  if (prev < 0xAC00 || prev > 0xD7A3) return true;
  const unsigned final_index = (prev - 0xAC00) % 28;  // 0: none, 8: ㄹ
  switch (g) {
    case Guard::AfterFinal: return final_index != 0;
    case Guard::AfterNoFinal: return final_index == 0;
    case Guard::AfterFinalExceptRieul: return final_index != 0 && final_index != 8;
    case Guard::AfterNoFinalOrRieul: return final_index == 0 || final_index == 8;
    default: return true;
  }
}

struct Edge {
  enum class Kind : std::uint8_t {
    Epsilon,  // consumes nothing
    Guard,    // consumes nothing; passable if `guard` holds for the previous character
    Action,   // consumes nothing, records `action` (slot tags, attributes)
    Char,     // consumes `ch`
    Space,    // consumes the (normalized) space
    Any,      // consumes any non-space character not in sets[set]
  };
  Kind kind;
  char32_t ch;
  int set;     // Any: index into Nfa::sets, or -1 for no exceptions
  int action;  // Action: index into Nfa::actions
  int to;
  Guard guard = Guard::None;  // Guard: the condition

  bool consumes() const {
    return kind != Kind::Epsilon && kind != Kind::Guard && kind != Kind::Action;
  }
};

struct Action {
  enum class Kind : std::uint8_t { Open, Close, Assign };
  Kind kind;
  int slot;  // slot name id; for Assign, -1 targets the innermost open slot
  std::string key;
  std::string value;
};

struct State {
  std::vector<Edge> edges;  // in priority order
  int accept_rule = -1;
};

struct Nfa {
  std::vector<State> states;
  std::vector<std::vector<char32_t>> sets;  // sorted
  std::vector<Action> actions;
  std::vector<std::string> slot_names;

  int add_state() {
    states.emplace_back();
    return static_cast<int>(states.size()) - 1;
  }
  void add_edge(int from, Edge::Kind kind, int to, char32_t ch = 0, int set = -1, int action = -1) {
    states[from].edges.push_back(Edge{kind, ch, set, action, to});
  }
  void epsilon(int from, int to) { add_edge(from, Edge::Kind::Epsilon, to); }
  void guard(int from, Guard g, int to) {
    states[from].edges.push_back(Edge{Edge::Kind::Guard, 0, -1, -1, to, g});
    has_guards = true;
  }
  bool has_guards = false;

  int slot_id(const std::string& name) {
    for (std::size_t i = 0; i < slot_names.size(); ++i)
      if (slot_names[i] == name) return static_cast<int>(i);
    slot_names.push_back(name);
    return static_cast<int>(slot_names.size()) - 1;
  }

  bool matches(const Edge& e, char32_t c) const {
    switch (e.kind) {
      case Edge::Kind::Char:
        return e.ch == c;
      case Edge::Kind::Space:
        return c == U' ';
      case Edge::Kind::Any:
        return c != U' ' &&
               (e.set < 0 || !std::binary_search(sets[e.set].begin(), sets[e.set].end(), c));
      default:
        return false;
    }
  }
};

// Epsilon closures for subset construction. A closure keeps only the states
// that consume input or accept, so sets that behave alike compare equal.
// Guard edges are followed if they hold for `prev`, the character just
// consumed (0 at the start of the input).
class Closure {
 public:
  explicit Closure(const Nfa& nfa) : nfa_(nfa), mark_(nfa.states.size(), 0) {}

  // Returns the sorted closure of `stack`.
  std::vector<int> operator()(std::vector<int> stack, char32_t prev) {
    if (++generation_ == 0) {
      std::fill(mark_.begin(), mark_.end(), 0);
      generation_ = 1;
    }
    std::vector<int> out;
    while (!stack.empty()) {
      int s = stack.back();
      stack.pop_back();
      if (mark_[s] == generation_) continue;
      mark_[s] = generation_;
      const State& st = nfa_.states[s];
      bool important = st.accept_rule >= 0;
      for (const Edge& e : st.edges) {
        if (e.consumes()) {
          important = true;
        } else if (e.kind != Edge::Kind::Guard || guard_holds(e.guard, prev)) {
          stack.push_back(e.to);
        }
      }
      if (important) out.push_back(s);
    }
    std::sort(out.begin(), out.end());
    return out;
  }

 private:
  const Nfa& nfa_;
  std::vector<unsigned> mark_;
  unsigned generation_ = 0;
};

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_NFA_HPP

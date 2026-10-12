// Nondeterministic automaton over normalized input characters.
#ifndef NLU_SCRIPT_NFA_HPP
#define NLU_SCRIPT_NFA_HPP

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace nlu {
namespace detail {

struct Edge {
  enum class Kind : std::uint8_t {
    Epsilon,  // consumes nothing
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

  bool consumes() const { return kind != Kind::Epsilon && kind != Kind::Action; }
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

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_NFA_HPP

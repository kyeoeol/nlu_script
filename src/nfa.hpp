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

// Characters as guards see them: what a syllable ends in, or not a syllable.
enum class CharClass : std::uint8_t {
  All,  // any character (no restriction)
  NotSyllable,
  SyllableNoFinal,
  SyllableRieul,  // final ㄹ
  SyllableOtherFinal,
};

inline CharClass char_class(char32_t c) {
  if (c < 0xAC00 || c > 0xD7A3) return CharClass::NotSyllable;
  const unsigned final_index = (c - 0xAC00) % 28;
  return final_index == 0 ? CharClass::SyllableNoFinal
         : final_index == 8 ? CharClass::SyllableRieul
                            : CharClass::SyllableOtherFinal;
}

inline bool guard_holds(Guard g, char32_t prev) {
  if (prev == 0 || prev == U' ') return false;
  const CharClass cls = char_class(prev);
  if (cls == CharClass::NotSyllable) return true;
  switch (g) {
    case Guard::AfterFinal: return cls != CharClass::SyllableNoFinal;
    case Guard::AfterNoFinal: return cls == CharClass::SyllableNoFinal;
    case Guard::AfterFinalExceptRieul: return cls == CharClass::SyllableOtherFinal;
    case Guard::AfterNoFinalOrRieul: return cls != CharClass::SyllableOtherFinal;
    default: return true;
  }
}

// A character of class `cls` that `chars` (sorted) doesn't mention, to stand
// for every such character.
inline char32_t unmentioned(const std::vector<char32_t>& chars, CharClass cls) {
  auto mentioned = [&](char32_t c) { return std::binary_search(chars.begin(), chars.end(), c); };
  if (cls == CharClass::NotSyllable || cls == CharClass::All) {
    for (char32_t c : std::u32string(U"xyzqjkvw"))
      if (!mentioned(c)) return c;
    char32_t c = 0x4E00;
    while (mentioned(c)) ++c;
    return c;
  }
  char32_t c = cls == CharClass::SyllableNoFinal ? 0xAC00 : cls == CharClass::SyllableRieul ? 0xAC08
                                                                                             : 0xAC01;
  while (mentioned(c)) c += 28;
  return c;
}

// The CharClasses that characters an automaton doesn't mention split into,
// when guards or class-limited wildcards tell syllables apart; otherwise the
// first stands for them all (unmentioned() treats it like CharClass::All).
inline constexpr CharClass kOtherClasses[4] = {CharClass::NotSyllable, CharClass::SyllableNoFinal,
                                              CharClass::SyllableRieul,
                                              CharClass::SyllableOtherFinal};

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
  CharClass char_class = CharClass::All;  // Any: only characters of this class
  // Consumes text for a wildcard or a typo rather than pattern text: every
  // Any edge, and the Char edges of an excluded wildcard. It doesn't count
  // towards a match's specificity.
  bool wildcard = false;

  bool consumes() const {
    return kind != Kind::Epsilon && kind != Kind::Guard && kind != Kind::Action;
  }
};

// Computes attributes from a slot's text when the match is built.
enum class Normalizer : std::uint8_t {
  None,
  KoreanNumber,  // value: the number written with digits and Korean numerals
  DateTime,      // day_offset, weekday, month, hour, ...: see datetime.hpp
  EnglishNumber,  // value: the number written in English words or digits
  Ordinal,        // value: the position (첫 번째, 21st)
  Money,          // value and currency (KRW, USD, ...)
};

struct Action {
  enum class Kind : std::uint8_t { Open, Close, Assign };
  Kind kind;
  int slot;  // slot name id; for Assign, -1 targets the innermost open slot
  std::string key;
  std::string value;
  Normalizer normalizer = Normalizer::None;  // Open: applied to the slot's text
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
    states[from].edges.push_back(
        Edge{kind, ch, set, action, to, Guard::None, CharClass::All, kind == Edge::Kind::Any});
  }
  void epsilon(int from, int to) { add_edge(from, Edge::Kind::Epsilon, to); }
  void guard(int from, Guard g, int to) {
    states[from].edges.push_back(Edge{Edge::Kind::Guard, 0, -1, -1, to, g, CharClass::All, false});
    has_guards = true;
  }
  void any_of_class(int from, int to, int set, CharClass cls) {
    states[from].edges.push_back(Edge{Edge::Kind::Any, 0, set, -1, to, Guard::None, cls, true});
    has_guards = true;
  }
  // Whether characters behave differently by their final consonant: the
  // automaton has guards or class-limited wildcards.
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
               (e.set < 0 || !std::binary_search(sets[e.set].begin(), sets[e.set].end(), c)) &&
               (e.char_class == CharClass::All || char_class(c) == e.char_class);
      default:
        return false;
    }
  }

  // The rules that the states of `set` accept, sorted.
  std::vector<int> accepted_rules(const std::vector<int>& set) const {
    std::vector<int> out;
    for (int s : set)
      if (states[s].accept_rule >= 0) out.push_back(states[s].accept_rule);
    std::sort(out.begin(), out.end());
    return out;
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
    stack_ = std::move(stack);
    return close(prev);
  }

  // The closure of the states that `set` reaches by consuming `c`.
  std::vector<int> step(const std::vector<int>& set, char32_t c) {
    stack_.clear();
    for (int s : set)
      for (const Edge& e : nfa_.states[s].edges)
        if (e.consumes() && nfa_.matches(e, c)) stack_.push_back(e.to);
    return close(c);
  }

 private:
  const Nfa& nfa_;
  std::vector<unsigned> mark_;
  unsigned generation_ = 0;
  std::vector<int> stack_, out_;  // scratch, kept to save allocations

  // The sorted closure of the states in stack_, sized exactly.
  std::vector<int> close(char32_t prev) {
    if (++generation_ == 0) {
      std::fill(mark_.begin(), mark_.end(), 0);
      generation_ = 1;
    }
    out_.clear();
    while (!stack_.empty()) {
      int s = stack_.back();
      stack_.pop_back();
      if (mark_[s] == generation_) continue;
      mark_[s] = generation_;
      const State& st = nfa_.states[s];
      bool important = st.accept_rule >= 0;
      for (const Edge& e : st.edges) {
        if (e.consumes()) {
          important = true;
        } else if (e.kind != Edge::Kind::Guard || guard_holds(e.guard, prev)) {
          stack_.push_back(e.to);
        }
      }
      if (important) out_.push_back(s);
    }
    std::sort(out_.begin(), out_.end());
    return std::vector<int>(out_.begin(), out_.end());
  }
};

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_NFA_HPP

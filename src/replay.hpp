// Phase 2 of matching: for a rule that the DFA found accepting the input,
// replay() runs a Pike VM over that rule's NFA, which replays the input with
// priorities (greedy groups, lazy wildcards) to recover slot spans and
// attribute assignments, and build_match() turns them into a Match.
#ifndef NLU_SCRIPT_REPLAY_HPP
#define NLU_SCRIPT_REPLAY_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "nfa.hpp"
#include "nlu_script/nlu_script.hpp"
#include "unicode.hpp"

namespace nlu {
namespace detail {

struct CompiledRule;  // script_impl.hpp

struct Event {
  int action;
  int pos;
};

// A rule's replay over a text it accepts.
struct Replay {
  std::vector<Event> events;  // the actions on the highest priority accepting path
  int specificity = 0;        // see Match::specificity
};

// Replays `rule` over `text`, which the DFA found it accepts: runs the
// rule's Pike VM if it needs one (see needs_pike_vm()), and works out the
// specificity. Returns false if the rule does not match after all.
bool replay(const Nfa& nfa, const CompiledRule& rule, const std::u32string& text, Replay& out);

// Whether the Pike VM can tell anything about a match of the rule with
// states [first, last) that the DFA doesn't: without actions it records no
// slots, and without wildcards their character count is 0.
bool needs_pike_vm(const Nfa& nfa, int first, int last);

// The match of `rule` over the bytes [begin, end) of `original`, which
// contain every event of `replayed`; `norm` is `original` normalized.
Match build_match(const Nfa& nfa, const CompiledRule& rule, const Replay& replayed,
                  const std::string& original, const NormalizedText& norm, std::size_t begin,
                  std::size_t end);

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_REPLAY_HPP

// Static checks over a compiled script.
#ifndef NLU_SCRIPT_LINT_HPP
#define NLU_SCRIPT_LINT_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "nfa.hpp"
#include "nlu_script/nlu_script.hpp"

namespace nlu {
namespace detail {

// Where a rule comes from.
struct RuleSource {
  std::string label;
  int line;
  std::string file;
  int start;        // its start state
  int first, last;  // its states occupy the id range [first, last)
};

struct RuleAnalysis {
  std::vector<Overlap> overlaps;  // in script order of their rules
  std::vector<DeadRule> dead_rules;  // in script order, of the labels searched fully
  bool complete = true;  // false if a search stopped at the state limit
};

// Explores the normalized inputs the rules of `script` accept, searching the
// determinized automaton of each pair of labels, and of each label alone,
// with at most `max_states` states (of the automata and of the searches) in
// all:
//  - every pair of labels that accept a common input, each with the cheapest
//    such input: fewest characters, then spaces wherever the patterns allow
//    them, then a placeholder such as `x` at wildcard positions;
//  - which label of such a pair match_best() always prefers;
//  - rules whose every input an earlier rule of their label accepts first.
// A search stops once what it finds can no longer change. `rules[r]`
// describes rule `r` (accept_rule r).
RuleAnalysis analyze_rules(const Nfa& script, const std::vector<RuleSource>& rules,
                           std::size_t max_states);

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_LINT_HPP

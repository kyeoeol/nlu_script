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
  bool wildcard_free;  // no path through the rule consumes a wildcard character
};

struct RuleAnalysis {
  std::vector<Overlap> overlaps;  // in script order of their rules
  std::vector<DeadRule> dead_rules;  // in script order; empty if not complete
  bool complete = true;
};

// Explores every normalized input the rules accept, in one search over the
// determinized automaton from `start` (at most `max_states` states):
//  - every pair of labels that accept a common input, each with the cheapest
//    such input: fewest characters, then spaces wherever the patterns allow
//    them, then a placeholder such as `x` at wildcard positions;
//  - which label of such a pair match_best() always prefers;
//  - rules whose every input an earlier rule of their label accepts first.
// `rules[r]` describes rule `r` (accept_rule r). For each rule, `nfa` also
// holds a copy without wildcard edges that accepts as rule rules.size() + r.
RuleAnalysis analyze_rules(const Nfa& nfa, int start, const std::vector<RuleSource>& rules,
                           std::size_t max_states);

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_LINT_HPP

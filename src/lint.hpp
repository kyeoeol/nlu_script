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

struct OverlapSearch {
  std::vector<Overlap> overlaps;  // ordered by line, then other_line
  bool complete = true;
};

// Finds every pair of labels that accept a common input, each with the
// cheapest such input: fewest characters, then spaces wherever the patterns
// allow them, then a placeholder such as `x` at wildcard positions. Explores
// the determinized automaton from `start`, interning at most `max_states`
// states. `labels[r]` and `lines[r]` describe rule `r`.
OverlapSearch find_overlaps(const Nfa& nfa, int start, const std::vector<std::string>& labels,
                            const std::vector<int>& lines, std::size_t max_states);

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_LINT_HPP

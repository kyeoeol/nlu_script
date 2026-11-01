// A rule of a compiled script.
#ifndef NLU_SCRIPT_RULE_HPP
#define NLU_SCRIPT_RULE_HPP

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace nlu {
namespace detail {

// Where a rule comes from, and its states in the script's automaton.
struct CompiledRule {
  std::string label;
  int line;
  std::string file;
  int start;  // start state
  int first;  // the rule's states occupy the id range [first, last)
  int last;
  bool needs_vm;  // see needs_pike_vm()
};

// The rule owning each of an automaton's first `states` states, given its
// rules: rule r owns the states [rules[r].first, rules[r].last). States no
// rule owns (the start state) get -1.
inline std::vector<int> rule_owners(std::size_t states, const std::vector<CompiledRule>& rules) {
  std::vector<int> owner(states, -1);
  for (std::size_t r = 0; r < rules.size(); ++r)
    std::fill(owner.begin() + rules[r].first, owner.begin() + rules[r].last, static_cast<int>(r));
  return owner;
}

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_RULE_HPP

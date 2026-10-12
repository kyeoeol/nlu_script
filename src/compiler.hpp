#ifndef NLU_SCRIPT_COMPILER_HPP
#define NLU_SCRIPT_COMPILER_HPP

#include <map>
#include <string>

#include "ast.hpp"
#include "nfa.hpp"

namespace nlu {
namespace detail {

struct Definition {
  NodePtr body;  // nullptr while the definition has no values yet
  SourcePos pos;
};

struct Definitions {
  std::map<std::string, Definition> entities;
  std::map<std::string, Definition> variables;
};

// Compiles `pattern` into `nfa` and returns the id of its start state. The
// pattern's final state gets accept_rule = `rule`. Throws CompileError.
int compile_rule(Nfa& nfa, const Definitions& defs, const Node& pattern, int rule);

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_COMPILER_HPP

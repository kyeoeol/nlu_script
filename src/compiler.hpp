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
// pattern's final state gets accept_rule = `rule`. With
// `trailing_punctuation`, the rule also accepts punctuation after its end
// ("are you there?!"). Throws CompileError.
int compile_rule(Nfa& nfa, const Definitions& defs, const Node& pattern, int rule,
                 bool trailing_punctuation);

// The state inside a word of the wildcard that compile_wildcard() builds for
// `<?>*?` or `<?>+?` from state `in`, if a space may follow the wildcard (a
// soft join or the end of the rule comes next); otherwise -1. It reads
// loaded automata too, which carry no record of their wildcards.
int wildcard_word(const Nfa& nfa, int in);

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_COMPILER_HPP

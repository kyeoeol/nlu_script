// Built-in entities referred to as `@{NAME} in patterns.
#ifndef NLU_SCRIPT_SYSTEM_ENTITIES_HPP
#define NLU_SCRIPT_SYSTEM_ENTITIES_HPP

#include <map>
#include <string>

#include "ast.hpp"
#include "nfa.hpp"

namespace nlu {
namespace detail {

struct SystemEntity {
  NodePtr body;           // the pattern, built from AST nodes
  Normalizer normalizer;  // computes the slot's attributes from its text
};

// Every system entity by name.
const std::map<std::string, SystemEntity>& system_entities();

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_SYSTEM_ENTITIES_HPP

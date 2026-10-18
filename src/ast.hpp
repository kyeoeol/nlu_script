// Abstract syntax tree of a pattern.
#ifndef NLU_SCRIPT_AST_HPP
#define NLU_SCRIPT_AST_HPP

#include <memory>
#include <string>
#include <vector>

namespace nlu {
namespace detail {

struct SourcePos {
  int line = 0;
  int column = 0;
};

// {key=value} or {slot.key=value}; an empty `slot` targets the innermost
// enclosing slot.
struct Assignment {
  std::string slot;
  std::string key;
  std::string value;
  SourcePos pos;
};

struct Node;
using NodePtr = std::shared_ptr<const Node>;

struct Node {
  enum class Kind {
    Literal,    // text: characters joined without spaces
    Sequence,   // children joined by `hard_joins`
    Choice,     // [a|b]: one of children
    Optional,   // (a|b): children[0] or nothing
    Repeat,     // [a]^(min,max): children[0] repeated
    Permute,    // ~[a, b]~: all children in any order
    Wildcard,   // <?>, <?>*?, <?>+?
    Slot,       // <name>children[0]</name>
    EntityRef,  // @{name}
    VarRef,     // @var.{name}
    SystemRef,  // `@{name}
    Particle,   // 을/를: `text` after a final consonant, `vowel_text` otherwise
  };
  enum class WildKind { One, Star, Plus };

  Kind kind = Kind::Literal;
  SourcePos pos;
  std::u32string text;
  std::vector<NodePtr> children;
  std::vector<bool> hard_joins;  // Sequence: hard_joins[i] joins children[i], children[i + 1]
  int min = 0;
  int max = 0;                   // Repeat: -1 means unbounded
  bool soft_repeat = true;       // Repeat: whether a space may separate repetitions
  WildKind wild = WildKind::One;
  NodePtr exclusion;             // Wildcard: text it must not match in full
  std::string name;
  std::u32string vowel_text;     // Particle: the form after a vowel
  bool rieul_as_vowel = false;   // Particle: a final ㄹ takes the vowel form (으로/로)
  std::vector<Assignment> assignments;  // performed after matching this node
};

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_AST_HPP

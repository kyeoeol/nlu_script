#ifndef NLU_SCRIPT_PARSER_HPP
#define NLU_SCRIPT_PARSER_HPP

#include <string>
#include <vector>

#include "ast.hpp"

namespace nlu {
namespace detail {

// Parses one pattern. `start` locates text[0] in the script, for error
// messages. With `width_folding`, literal text is width-folded (Ａ -> A).
// Throws CompileError.
NodePtr parse_pattern(const std::u32string& text, const SourcePos& start,
                      bool width_folding = false);

// Splits on commas that are not nested in brackets, braces or quotes.
// Returns the start index and the text of each part.
std::vector<std::pair<std::size_t, std::u32string>> split_top_level_commas(
    const std::u32string& text);

bool is_special(char32_t c);
bool is_name_char(char32_t c);

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_PARSER_HPP

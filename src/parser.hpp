#ifndef NLU_SCRIPT_PARSER_HPP
#define NLU_SCRIPT_PARSER_HPP

#include <string>
#include <vector>

#include "ast.hpp"

namespace nlu {
namespace detail {

// Parses one pattern. `line` and `column` locate text[0] in the script and are
// used for error messages. Throws CompileError.
NodePtr parse_pattern(const std::u32string& text, int line, int column);

// Splits on commas that are not nested in brackets, braces or quotes.
// Returns the start index and the text of each part.
std::vector<std::pair<std::size_t, std::u32string>> split_top_level_commas(
    const std::u32string& text);

bool is_special(char32_t c);
bool is_name_char(char32_t c);

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_PARSER_HPP

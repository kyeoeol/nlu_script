#ifndef NLU_SCRIPT_UNICODE_HPP
#define NLU_SCRIPT_UNICODE_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace nlu {
namespace detail {

struct DecodedChar {
  char32_t code;
  std::size_t begin;  // byte offset of the first byte
  std::size_t end;    // byte offset one past the last byte
};

// Invalid byte sequences decode to U+FFFD, one per offending byte.
std::vector<DecodedChar> decode_utf8(const std::string& text);
void append_utf8(char32_t code, std::string& out);
std::string to_utf8(const std::u32string& text);

bool is_space(char32_t c);
// Case folding applied to both patterns and input (ASCII only for now).
char32_t fold_case(char32_t c);

// Input text after whitespace collapsing, trimming and case folding, with a
// map from each normalized character back to the original bytes.
struct NormalizedText {
  std::u32string text;
  std::vector<std::size_t> begin;  // original byte offset of text[i]
  std::vector<std::size_t> end;    // original byte offset past text[i]
};

NormalizedText normalize(const std::string& text);

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_UNICODE_HPP

#ifndef NLU_SCRIPT_UNICODE_HPP
#define NLU_SCRIPT_UNICODE_HPP

#include <cstddef>
#include <string>
#include <string_view>
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
// Puts text in Unicode Normalization Form C: canonical decomposition,
// canonical ordering of combining marks, and canonical composition (Hangul
// jamo included, e.g. U+1112 U+1161 U+11AB -> U+D55C). A composed character
// spans the bytes of the characters it replaces.
void normalize_nfc(std::vector<DecodedChar>& chars);
void append_utf8(char32_t code, std::string& out);
std::string to_utf8(const std::u32string& text);

// Whether `s` has `prefix` at `at`, which is at most s.size().
inline bool starts_with(std::u32string_view s, std::size_t at, std::u32string_view prefix) {
  return s.compare(at, prefix.size(), prefix) == 0;
}
inline bool ends_with(std::u32string_view s, std::u32string_view suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool is_space(char32_t c);
// Simple (one-to-one) Unicode case folding, applied to patterns and input.
char32_t fold_case(char32_t c);
// Full-width and half-width forms to their usual ones (Ａ -> A, ｶ -> カ),
// applied to patterns and input with `option fold-width`.
char32_t fold_width(char32_t c);

// Input text after NFC, whitespace collapsing, trimming, case folding and
// (optionally) width folding, with a map from each normalized character back
// to the original bytes.
struct NormalizedText {
  std::u32string text;
  std::vector<std::size_t> begin;  // original byte offset of text[i]
  std::vector<std::size_t> end;    // original byte offset past text[i]
};

NormalizedText normalize(const std::string& text, bool width_folding);

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_UNICODE_HPP

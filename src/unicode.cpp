#include "unicode.hpp"

namespace nlu {
namespace detail {

std::vector<DecodedChar> decode_utf8(const std::string& text) {
  std::vector<DecodedChar> out;
  out.reserve(text.size());
  const std::size_t n = text.size();
  std::size_t i = 0;
  while (i < n) {
    unsigned char b = static_cast<unsigned char>(text[i]);
    std::size_t len = 0;
    char32_t code = 0;
    char32_t min = 0;
    if (b < 0x80) {
      len = 1;
      code = b;
    } else if ((b & 0xE0) == 0xC0) {
      len = 2;
      code = b & 0x1F;
      min = 0x80;
    } else if ((b & 0xF0) == 0xE0) {
      len = 3;
      code = b & 0x0F;
      min = 0x800;
    } else if ((b & 0xF8) == 0xF0) {
      len = 4;
      code = b & 0x07;
      min = 0x10000;
    }
    bool valid = len > 0 && i + len <= n;
    for (std::size_t k = 1; valid && k < len; ++k) {
      unsigned char c = static_cast<unsigned char>(text[i + k]);
      if ((c & 0xC0) != 0x80) {
        valid = false;
      } else {
        code = (code << 6) | (c & 0x3F);
      }
    }
    if (valid && len > 1 && (code < min || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)))
      valid = false;
    if (!valid) {
      out.push_back({0xFFFD, i, i + 1});
      ++i;
    } else {
      out.push_back({code, i, i + len});
      i += len;
    }
  }
  return out;
}

void compose_hangul(std::vector<DecodedChar>& chars) {
  // Hangul syllable composition, Unicode section 3.12.
  const char32_t kSBase = 0xAC00, kLBase = 0x1100, kVBase = 0x1161, kTBase = 0x11A7;
  const char32_t kLCount = 19, kVCount = 21, kTCount = 28, kSCount = 11172;
  std::size_t out = 0;
  for (std::size_t i = 0; i < chars.size(); ++i) {
    const char32_t c = chars[i].code;
    if (out > 0) {
      DecodedChar& last = chars[out - 1];
      const char32_t l = last.code - kLBase, v = c - kVBase;
      const char32_t s = last.code - kSBase, t = c - kTBase;
      if (last.code >= kLBase && l < kLCount && c >= kVBase && v < kVCount) {
        last.code = kSBase + (l * kVCount + v) * kTCount;
        last.end = chars[i].end;
        continue;
      }
      if (last.code >= kSBase && s < kSCount && s % kTCount == 0 && c > kTBase && t < kTCount) {
        last.code += t;
        last.end = chars[i].end;
        continue;
      }
    }
    chars[out++] = chars[i];
  }
  chars.resize(out);
}

void append_utf8(char32_t c, std::string& out) {
  if (c < 0x80) {
    out += static_cast<char>(c);
  } else if (c < 0x800) {
    out += static_cast<char>(0xC0 | (c >> 6));
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    out += static_cast<char>(0xE0 | (c >> 12));
    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (c >> 18));
    out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
  }
}

std::string to_utf8(const std::u32string& text) {
  std::string out;
  for (char32_t c : text) append_utf8(c, out);
  return out;
}

bool is_space(char32_t c) {
  switch (c) {
    case U' ':
    case U'\t':
    case U'\n':
    case U'\r':
    case U'\f':
    case U'\v':
    case 0x00A0:  // no-break space
    case 0x3000:  // ideographic space
      return true;
    default:
      return false;
  }
}

char32_t fold_case(char32_t c) {
  if (c >= U'A' && c <= U'Z') return c + (U'a' - U'A');
  return c;
}

NormalizedText normalize(const std::string& text) {
  NormalizedText out;
  bool pending_space = false;
  std::vector<DecodedChar> chars = decode_utf8(text);
  compose_hangul(chars);
  for (const DecodedChar& d : chars) {
    if (is_space(d.code)) {
      pending_space = !out.text.empty();
      continue;
    }
    if (pending_space) {
      // The collapsed space covers the whole whitespace run.
      out.text += U' ';
      out.begin.push_back(out.end.back());
      out.end.push_back(d.begin);
      pending_space = false;
    }
    out.text += fold_case(d.code);
    out.begin.push_back(d.begin);
    out.end.push_back(d.end);
  }
  return out;
}

}  // namespace detail
}  // namespace nlu

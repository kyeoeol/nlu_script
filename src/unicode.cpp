#include "unicode.hpp"

#include <algorithm>
#include <utility>

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

namespace {

struct CodePair {
  char32_t from, to;
};

struct CodeTriple {
  char32_t a, b, c;
};

#include "unicode_tables.inc"

template <typename Row, std::size_t N>
const Row* find_row(const Row (&table)[N], char32_t c) {
  const Row* end = table + N;
  const Row* it = std::lower_bound(table, end, c, [](const Row& r, char32_t v) {
    return static_cast<char32_t>(r.from) < v;
  });
  return it != end && it->from == c ? it : nullptr;
}

int combining_class(char32_t c) {
  if (c < 0x300) return 0;
  const CodePair* row = find_row(kCombiningClasses, c);
  return row ? static_cast<int>(row->to) : 0;
}

// Hangul syllable composition, Unicode section 3.12.
constexpr char32_t kSBase = 0xAC00, kLBase = 0x1100, kVBase = 0x1161, kTBase = 0x11A7;
constexpr char32_t kLCount = 19, kVCount = 21, kTCount = 28, kSCount = 11172;

// The canonical composition of `a` and `b`, or 0.
char32_t compose(char32_t a, char32_t b) {
  if (a >= kLBase && a - kLBase < kLCount && b >= kVBase && b - kVBase < kVCount)
    return kSBase + ((a - kLBase) * kVCount + (b - kVBase)) * kTCount;
  if (a >= kSBase && a - kSBase < kSCount && (a - kSBase) % kTCount == 0 && b > kTBase &&
      b - kTBase < kTCount)
    return a + (b - kTBase);
  const CodeTriple* end = kCompositions + sizeof kCompositions / sizeof kCompositions[0];
  const CodeTriple* it = std::lower_bound(
      kCompositions, end, std::make_pair(a, b), [](const CodeTriple& r, std::pair<char32_t, char32_t> v) {
        return std::make_pair(r.a, r.b) < v;
      });
  return it != end && it->a == a && it->b == b ? it->c : 0;
}

void decompose(const DecodedChar& d, std::vector<DecodedChar>& out) {
  if (d.code >= 0xC0) {
    const CodeTriple* end = kDecompositions + sizeof kDecompositions / sizeof kDecompositions[0];
    const CodeTriple* it = std::lower_bound(
        kDecompositions, end, d.code, [](const CodeTriple& r, char32_t v) { return r.a < v; });
    if (it != end && it->a == d.code) {
      decompose({it->b, d.begin, d.end}, out);
      if (it->c) decompose({it->c, d.begin, d.end}, out);
      return;
    }
  }
  out.push_back(d);
}

}  // namespace

void normalize_nfc(std::vector<DecodedChar>& chars) {
  // Most text is already composed and has no combining marks.
  bool simple = true;
  for (const DecodedChar& d : chars) simple = simple && d.code < 0x300;
  if (simple) return;

  std::vector<DecodedChar> out;
  out.reserve(chars.size());
  for (const DecodedChar& d : chars) decompose(d, out);
  // Canonical ordering: stable sort of each run of combining marks by class.
  for (std::size_t i = 0; i < out.size();) {
    if (!combining_class(out[i].code)) {
      ++i;
      continue;
    }
    std::size_t j = i;
    while (j < out.size() && combining_class(out[j].code)) ++j;
    std::stable_sort(out.begin() + i, out.begin() + j, [](const DecodedChar& x, const DecodedChar& y) {
      return combining_class(x.code) < combining_class(y.code);
    });
    i = j;
  }
  // Canonical composition: a character joins the last starter unless a
  // character between them is a starter or has the same or a higher class.
  std::size_t n = 0;  // composed characters kept in out[0, n)
  std::size_t starter = 0;
  bool have_starter = false;
  int last_class = 0;  // class of the last character kept after the starter
  for (std::size_t i = 0; i < out.size(); ++i) {
    const DecodedChar d = out[i];
    const int cls = combining_class(d.code);
    const bool adjacent = have_starter && n == starter + 1;
    if (have_starter && (adjacent || (last_class != 0 && last_class < cls))) {
      DecodedChar& s = out[starter];
      if (char32_t c = compose(s.code, d.code)) {
        s.code = c;
        s.begin = std::min(s.begin, d.begin);
        s.end = std::max(s.end, d.end);
        continue;
      }
    }
    if (cls == 0) {
      starter = n;
      have_starter = true;
      last_class = 0;
    } else {
      last_class = cls;
    }
    out[n++] = d;
  }
  out.resize(n);
  chars = std::move(out);
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
  if (c < 0x80) return c >= U'A' && c <= U'Z' ? c + (U'a' - U'A') : c;
  const CodePair* row = find_row(kCaseFolding, c);
  return row ? row->to : c;
}

char32_t fold_width(char32_t c) {
  if (c < 0xFF00 || c > 0xFFEF) return c;
  const CodePair* row = find_row(kWidthFolding, c);
  return row ? row->to : c;
}

NormalizedText normalize(const std::string& text, bool width_folding) {
  NormalizedText out;
  bool pending_space = false;
  std::vector<DecodedChar> chars = decode_utf8(text);
  if (width_folding)
    for (DecodedChar& d : chars) d.code = fold_width(d.code);
  normalize_nfc(chars);
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

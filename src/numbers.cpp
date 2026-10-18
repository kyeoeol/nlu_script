#include "numbers.hpp"

#include "unicode.hpp"

namespace nlu {
namespace detail {

namespace {

using Value = unsigned long long;
constexpr Value kLimit = 1000000000000000000ULL;  // 10^18

// a * b + c, or false if the result reaches kLimit.
bool mul_add(Value a, Value b, Value c, Value& out) {
  if (a != 0 && b >= kLimit / a + 1) return false;
  out = a * b + c;
  return out < kLimit;
}

int sino_digit(char32_t c) {
  const char32_t digits[] = U"일이삼사오육칠팔구";
  for (int i = 0; digits[i]; ++i)
    if (digits[i] == c) return i + 1;
  return -1;
}

Value small_unit(char32_t c) {
  switch (c) {
    case U'십': return 10;
    case U'백': return 100;
    case U'천': return 1000;
    default: return 0;
  }
}

Value large_unit(char32_t c) {
  switch (c) {
    case U'만': return 10000ULL;
    case U'억': return 100000000ULL;
    case U'조': return 1000000000000ULL;
    default: return 0;
  }
}

}  // namespace

bool korean_number_value(const std::string& text, std::string& value) {
  Value total = 0;    // sum of the 만, 억 and 조 groups so far
  Value section = 0;  // below 만: sum of the 십, 백 and 천 parts so far
  Value pending = 0;  // the digits not yet given a unit
  bool has_pending = false;
  bool in_digits = false;
  bool any = false;
  for (const DecodedChar& d : decode_utf8(text)) {
    const char32_t c = d.code;
    if (c >= U'0' && c <= U'9') {
      if (!in_digits) pending = 0;
      if (!mul_add(pending, 10, c - U'0', pending)) return false;
      has_pending = in_digits = any = true;
      continue;
    }
    if (c == U',' && in_digits) continue;
    in_digits = false;
    if (is_space(c)) continue;
    if (int digit = sino_digit(c); digit > 0) {
      pending = static_cast<Value>(digit);
      has_pending = any = true;
    } else if (Value unit = small_unit(c)) {
      if (!mul_add(has_pending ? pending : 1, unit, section, section)) return false;
      pending = 0;
      has_pending = false;
      any = true;
    } else if (Value unit = large_unit(c)) {
      Value group = section + pending;
      if (!mul_add(group ? group : 1, unit, total, total)) return false;
      section = pending = 0;
      has_pending = false;
      any = true;
    } else {
      return false;
    }
  }
  Value result = total + section + pending;
  if (!any || result >= kLimit) return false;
  value = std::to_string(result);
  return true;
}

}  // namespace detail
}  // namespace nlu

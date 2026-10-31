#include "numbers.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "unicode.hpp"
#include "vocabulary.hpp"

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
  const std::size_t at = kSinoDigits.find(c);
  return at == std::u32string_view::npos ? -1 : static_cast<int>(at) + 1;
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

// Native Korean numbers from 1 to 99 (하나, 스물셋, 스무, 한); -1 if `s` is
// not one.
int native_value(const std::u32string& s) {
  // The ten, if any, then the one.
  int value = 0;
  std::size_t i = 0;
  auto ten = [&](const NumberWord& w) {
    if (i == 0 && starts_with(s, 0, w.text)) {
      value = w.value;
      i = std::char_traits<char32_t>::length(w.text);
    }
  };
  for (const NumberWord& w : kNativeTens) ten(w);
  ten(kNativeCountingTwenty);
  if (i == s.size()) return value > 0 ? value : -1;  // 열, 스물, 스무
  if (starts_with(s, 0, kNativeCountingTwenty.text)) return -1;  // 스무 stands alone
  const std::u32string_view rest = std::u32string_view(s).substr(i);
  int one = value_of(kNativeOnes, rest);
  if (one < 0) one = value_of(kNativeCountingOnes, rest);
  return one > 0 ? value + one : -1;
}

}  // namespace

bool korean_number_value(const std::string& text, std::string& value) {
  {
    std::u32string compact;
    for (const DecodedChar& d : decode_utf8(text))
      if (!is_space(d.code)) compact += d.code;
    if (int native = native_value(compact); native > 0) {
      value = std::to_string(native);
      return true;
    }
  }
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

namespace {

std::string lowercase(const std::string& text) {
  std::string out;
  for (char c : text) out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  return out;
}

// Words separated by spaces and hyphens.
std::vector<std::string> words_of(const std::string& text) {
  std::vector<std::string> out;
  std::string word;
  for (char c : lowercase(text) + " ") {
    if (c == ' ' || c == '-') {
      if (!word.empty()) out.push_back(word);
      word.clear();
    } else {
      word += c;
    }
  }
  return out;
}

std::u32string widen(const std::string& text) {
  std::u32string out;
  for (const DecodedChar& d : decode_utf8(text)) out += d.code;
  return out;
}

int english_word_value(const std::string& word) {
  const std::u32string w = widen(word);
  int value = value_of(kEnglishSmall, w);
  if (value < 0) value = value_of(kEnglishTens, w);
  if (value < 0 && word == "a") value = 1;
  return value;
}

bool digits_value(const std::string& word, Value& out) {
  if (word.empty()) return false;
  Value v = 0;
  for (char c : word) {
    if (c == ',') continue;
    if (c < '0' || c > '9' || !mul_add(v, 10, static_cast<Value>(c - '0'), v)) return false;
  }
  out = v;
  return true;
}

}  // namespace

bool english_number_value(const std::string& text, std::string& value) {
  Value total = 0, current = 0;
  bool any = false;
  for (const std::string& word : words_of(text)) {
    Value n = 0;
    if (word == "and") continue;
    if (word == "hundred") {
      if (!mul_add(current ? current : 1, 100, 0, current)) return false;
    } else if (word == "thousand" || word == "million" || word == "billion") {
      const Value scale = word == "thousand" ? 1000ULL : word == "million" ? 1000000ULL
                                                                           : 1000000000ULL;
      if (!mul_add(current ? current : 1, scale, total, total)) return false;
      current = 0;
    } else if (english_word_value(word) >= 0) {
      current += static_cast<Value>(english_word_value(word));
    } else if (digits_value(word, n)) {
      current += n;
    } else {
      return false;
    }
    any = true;
  }
  if (!any || total + current >= kLimit) return false;
  value = std::to_string(total + current);
  return true;
}

bool ordinal_value(const std::string& text, std::string& value) {
  std::u32string s;
  for (const DecodedChar& d : decode_utf8(lowercase(text)))
    if (!is_space(d.code)) s += d.code;
  auto ends = [&](const std::u32string& suffix) {
    return s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
  };
  // Korean: N번째, N째 (첫 is 1).
  for (const char32_t* suffix : {U"번째", U"째"}) {
    if (!ends(suffix)) continue;
    std::u32string head = s.substr(0, s.size() - std::char_traits<char32_t>::length(suffix));
    if (head == kNativeFirst.text) {
      value = std::to_string(kNativeFirst.value);
      return true;
    }
    if (int native = native_value(head); native > 0) {
      value = std::to_string(native);
      return true;
    }
    Value n = 0;
    if (digits_value(to_utf8(head), n)) {
      value = std::to_string(n);
      return true;
    }
    return false;
  }
  // English: 21st, twenty-first, twelfth.
  std::vector<std::string> words = words_of(text);
  if (words.empty()) return false;
  int total = 0;
  for (std::size_t i = 0; i < words.size(); ++i) {
    const std::string& w = words[i];
    bool found = false;
    if (i + 1 == words.size()) {
      if (int ordinal = value_of(kEnglishOrdinals, widen(w)); ordinal > 0) {
        total += ordinal;
        found = true;
      }
      if (!found && w.size() > 2) {
        Value n = 0;
        if (digits_value(w.substr(0, w.size() - 2), n) && n < 1000000) {
          total += static_cast<int>(n);
          found = true;
        }
      }
    } else if (english_word_value(w) >= 20) {
      total += english_word_value(w);
      found = true;
    }
    if (!found) return false;
  }
  value = std::to_string(total);
  return true;
}

namespace {

// Digits with optional thousands separators and decimals, times `scale`
// ("2.8" and 1,000,000 give 2800000). Returns false if `number` is not such
// digits or the product is not a whole number below kLimit.
bool scaled_value(const std::string& number, Value scale, std::string& value) {
  const std::size_t dot = number.find('.');
  Value whole = 0, result = 0;
  if (!digits_value(number.substr(0, dot), whole) || !mul_add(whole, scale, 0, result))
    return false;
  if (dot != std::string::npos) {
    Value unit = scale;
    for (char c : number.substr(dot + 1)) {
      if (c < '0' || c > '9' || unit % 10 != 0) return false;
      unit /= 10;
      result += static_cast<Value>(c - '0') * unit;
    }
  }
  if (result >= kLimit) return false;
  value = std::to_string(result);
  return true;
}

}  // namespace

bool money_value(const std::string& text, std::string& value, std::string& currency) {
  // Every currency marker in UTF-8, longest first, so dollars wins over
  // dollar and euro over eur.
  static const std::vector<std::pair<std::string, const char*>> markers = [] {
    std::vector<std::pair<std::string, const char*>> out;
    auto add = [&](const auto& table) {
      for (const NamedWord& w : table) out.emplace_back(to_utf8(w.text), w.name);
    };
    add(kCurrencyWordsKo);
    add(kCurrencySymbols);
    add(kCurrencyWordsEn);
    add(kCurrencyCodes);
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
      return a.first.size() > b.first.size();
    });
    return out;
  }();
  std::string amount = lowercase(text);
  currency.clear();
  for (const auto& m : markers) {
    std::size_t at = amount.find(m.first);
    if (at == std::string::npos) continue;
    currency = m.second;
    amount.erase(at, m.first.size());
    break;
  }
  if (currency.empty()) return false;
  // Trim, then read the amount: digits with decimals, Korean or English.
  std::size_t b = amount.find_first_not_of(' '), e = amount.find_last_not_of(' ');
  if (b == std::string::npos) return false;
  amount = amount.substr(b, e - b + 1);
  // Digits with a scale: 30k, 2.8 million.
  for (const NumberWord& s : kAmountScales) {
    const std::string scale = to_utf8(s.text);
    if (amount.size() <= scale.size() ||
        amount.compare(amount.size() - scale.size(), scale.size(), scale) != 0)
      continue;
    std::string number = amount.substr(0, amount.size() - scale.size());
    while (!number.empty() && number.back() == ' ') number.pop_back();
    if (scaled_value(number, static_cast<Value>(s.value), value)) return true;
  }
  std::string digits;
  bool plain = true;
  for (char c : amount) {
    if ((c >= '0' && c <= '9') || c == '.') {
      digits += c;
    } else if (c != ',') {
      plain = false;
    }
  }
  if (plain && !digits.empty() && digits.front() != '.') {
    std::size_t dot = digits.find('.');
    std::string whole = dot == std::string::npos ? digits : digits.substr(0, dot);
    Value n = 0;
    if (!digits_value(whole, n)) return false;
    value = std::to_string(n) + (dot == std::string::npos ? "" : digits.substr(dot));
    return true;
  }
  return korean_number_value(amount, value) || english_number_value(amount, value);
}

}  // namespace detail
}  // namespace nlu

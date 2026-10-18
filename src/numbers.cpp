#include "numbers.hpp"

#include <string>
#include <utility>
#include <vector>

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

// Native Korean numbers from 1 to 99 (하나, 스물셋, 스무, 한); -1 if `s` is
// not one.
int native_value(const std::u32string& s) {
  static const std::pair<const char32_t*, int> tens[] = {
      {U"스물", 20}, {U"서른", 30}, {U"마흔", 40}, {U"예순", 60}, {U"일흔", 70},
      {U"여든", 80}, {U"아흔", 90}, {U"스무", 20}, {U"열", 10},   {U"쉰", 50}};
  static const std::pair<const char32_t*, int> ones[] = {
      {U"하나", 1}, {U"다섯", 5}, {U"여섯", 6}, {U"일곱", 7}, {U"여덟", 8}, {U"아홉", 9},
      {U"한", 1},   {U"둘", 2},   {U"두", 2},   {U"셋", 3},   {U"세", 3},   {U"넷", 4},
      {U"네", 4}};
  int value = 0;
  std::size_t i = 0;
  for (const auto& t : tens) {
    std::u32string w = t.first;
    if (s.compare(0, w.size(), w) == 0) {
      value = t.second;
      i = w.size();
      break;
    }
  }
  if (i == s.size()) return value > 0 ? value : -1;  // 열, 스물, 스무
  if (s.compare(0, 2, U"스무") == 0) return -1;     // 스무 stands alone
  for (const auto& o : ones) {
    std::u32string w = o.first;
    if (s.compare(i, std::u32string::npos, w) == 0) return value + o.second;
  }
  return -1;
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

int english_word_value(const std::string& word) {
  static const char* const small[] = {"zero",    "one",     "two",       "three",    "four",
                                      "five",    "six",     "seven",     "eight",    "nine",
                                      "ten",     "eleven",  "twelve",    "thirteen", "fourteen",
                                      "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
  static const char* const tens[] = {"twenty", "thirty",  "forty",  "fifty",
                                     "sixty",  "seventy", "eighty", "ninety"};
  for (int i = 0; i < 20; ++i)
    if (word == small[i]) return i;
  for (int i = 0; i < 8; ++i)
    if (word == tens[i]) return 20 + 10 * i;
  if (word == "a") return 1;
  return -1;
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
    if (head == U"첫") {
      value = "1";
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
  static const std::pair<const char*, int> ordinals[] = {
      {"first", 1},       {"second", 2},      {"third", 3},       {"fourth", 4},
      {"fifth", 5},       {"sixth", 6},       {"seventh", 7},     {"eighth", 8},
      {"ninth", 9},       {"tenth", 10},      {"eleventh", 11},   {"twelfth", 12},
      {"thirteenth", 13}, {"fourteenth", 14}, {"fifteenth", 15},  {"sixteenth", 16},
      {"seventeenth", 17}, {"eighteenth", 18}, {"nineteenth", 19}, {"twentieth", 20},
      {"thirtieth", 30},  {"fortieth", 40},   {"fiftieth", 50},   {"sixtieth", 60},
      {"seventieth", 70}, {"eightieth", 80},  {"ninetieth", 90}};
  std::vector<std::string> words = words_of(text);
  if (words.empty()) return false;
  int total = 0;
  for (std::size_t i = 0; i < words.size(); ++i) {
    const std::string& w = words[i];
    bool found = false;
    if (i + 1 == words.size()) {
      for (const auto& o : ordinals) {
        if (w == o.first) {
          total += o.second;
          found = true;
        }
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

bool money_value(const std::string& text, std::string& value, std::string& currency) {
  static const std::pair<const char*, const char*> markers[] = {
      {"파운드", "GBP"}, {"달러", "USD"}, {"유로", "EUR"}, {"위안", "CNY"}, {"원", "KRW"},
      {"불", "USD"},     {"엔", "JPY"},   {"$", "USD"},    {"₩", "KRW"},    {"€", "EUR"},
      {"£", "GBP"},      {"¥", "JPY"},    {"dollars", "USD"}, {"dollar", "USD"},
      {"bucks", "USD"},  {"buck", "USD"}, {"euros", "EUR"}, {"euro", "EUR"},
      {"pounds", "GBP"}, {"pound", "GBP"}, {"yuan", "CNY"}, {"yen", "JPY"}, {"won", "KRW"},
      {"usd", "USD"},    {"krw", "KRW"},  {"eur", "EUR"},  {"jpy", "JPY"},  {"gbp", "GBP"},
      {"cny", "CNY"}};
  std::string amount = lowercase(text);
  currency.clear();
  for (const auto& m : markers) {
    std::size_t at = amount.find(m.first);
    if (at == std::string::npos) continue;
    currency = m.second;
    amount.erase(at, std::string(m.first).size());
    break;
  }
  if (currency.empty()) return false;
  // Trim, then read the amount: digits with decimals, Korean or English.
  std::size_t b = amount.find_first_not_of(' '), e = amount.find_last_not_of(' ');
  if (b == std::string::npos) return false;
  amount = amount.substr(b, e - b + 1);
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

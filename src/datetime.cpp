#include "datetime.hpp"

#include <algorithm>
#include <string>

#include "numbers.hpp"
#include "unicode.hpp"

namespace nlu {
namespace detail {

namespace {

using Attributes = std::vector<std::pair<std::string, std::string>>;

void set(Attributes& attrs, const std::string& key, const std::string& value) {
  for (auto& kv : attrs) {
    if (kv.first == key) {
      kv.second = value;
      return;
    }
  }
  attrs.emplace_back(key, value);
}

const std::string* get(const Attributes& attrs, const std::string& key) {
  for (const auto& kv : attrs)
    if (kv.first == key) return &kv.second;
  return nullptr;
}

std::string offset(int n) { return n > 0 ? "+" + std::to_string(n) : std::to_string(n); }

// A word and the attributes it sets. Words are matched without spaces, since
// spaces are removed from the text first.
struct Word {
  std::u32string text;
  Attributes attrs;
};

std::vector<Word> make_words() {
  std::vector<Word> words;
  auto add = [&](std::u32string text, Attributes attrs) {
    words.push_back({std::move(text), std::move(attrs)});
  };
  // Korean
  const std::pair<const char32_t*, int> days_ko[] = {{U"오늘", 0},  {U"내일", 1}, {U"모레", 2},
                                                     {U"글피", 3},  {U"어제", -1}, {U"그제", -2},
                                                     {U"그저께", -2}};
  for (const auto& d : days_ko) add(d.first, {{"day_offset", offset(d.second)}});
  const std::pair<const char32_t*, int> which_ko[] = {
      {U"이번", 0}, {U"다음", 1}, {U"지난", -1}, {U"저번", -1}};
  for (const auto& w : which_ko) {
    add(std::u32string(w.first) + U"주", {{"week_offset", offset(w.second)}});
    add(std::u32string(w.first) + U"달", {{"month_offset", offset(w.second)}});
  }
  add(U"담주", {{"week_offset", "+1"}});
  const std::pair<const char32_t*, int> years_ko[] = {
      {U"올해", 0}, {U"내년", 1}, {U"작년", -1}, {U"재작년", -2}};
  for (const auto& y : years_ko) add(y.first, {{"year_offset", offset(y.second)}});
  const char* const weekday_names[] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
  const char32_t weekdays_ko[] = U"월화수목금토일";
  for (int i = 0; i < 7; ++i)
    add(std::u32string(1, weekdays_ko[i]) + U"요일", {{"weekday", weekday_names[i]}});
  const std::pair<const char32_t*, const char*> parts_ko[] = {
      {U"새벽", "DAWN"}, {U"아침", "MORNING"}, {U"오전", "AM"},      {U"낮", "DAY"},
      {U"점심", "NOON"}, {U"오후", "PM"},      {U"저녁", "EVENING"}, {U"밤", "NIGHT"}};
  for (const auto& p : parts_ko) add(p.first, {{"part_of_day", p.second}});
  add(U"정오", {{"hour", "12"}, {"minute", "0"}});
  add(U"자정", {{"hour", "0"}, {"minute", "0"}});
  add(U"반", {{"minute", "30"}});
  // English
  const std::pair<const char32_t*, int> days_en[] = {{U"today", 0},
                                                     {U"tomorrow", 1},
                                                     {U"yesterday", -1},
                                                     {U"thedayaftertomorrow", 2},
                                                     {U"thedaybeforeyesterday", -2}};
  for (const auto& d : days_en) add(d.first, {{"day_offset", offset(d.second)}});
  const char32_t* const weekdays_en[] = {U"monday", U"tuesday",  U"wednesday", U"thursday",
                                         U"friday", U"saturday", U"sunday"};
  const std::pair<const char32_t*, int> which_en[] = {{U"this", 0}, {U"next", 1}, {U"last", -1}};
  for (int i = 0; i < 7; ++i) {
    add(weekdays_en[i], {{"weekday", weekday_names[i]}});
    for (const auto& w : which_en)
      add(std::u32string(w.first) + weekdays_en[i],
          {{"week_offset", offset(w.second)}, {"weekday", weekday_names[i]}});
  }
  for (const auto& w : which_en)
    add(std::u32string(w.first) + U"week", {{"week_offset", offset(w.second)}});
  const char32_t* const months_en[] = {U"january", U"february", U"march",     U"april",
                                       U"may",     U"june",     U"july",      U"august",
                                       U"september", U"october", U"november", U"december"};
  for (int i = 0; i < 12; ++i) {
    add(months_en[i], {{"month", std::to_string(i + 1)}});
    add(std::u32string(months_en[i]).substr(0, 3), {{"month", std::to_string(i + 1)}});
  }
  add(U"sept", {{"month", "9"}});
  const std::pair<const char32_t*, Attributes> parts_en[] = {
      {U"morning", {{"part_of_day", "MORNING"}}},
      {U"afternoon", {{"part_of_day", "PM"}}},
      {U"evening", {{"part_of_day", "EVENING"}}},
      {U"night", {{"part_of_day", "NIGHT"}}},
      {U"inthemorning", {{"part_of_day", "MORNING"}}},
      {U"intheafternoon", {{"part_of_day", "PM"}}},
      {U"intheevening", {{"part_of_day", "EVENING"}}},
      {U"atnight", {{"part_of_day", "NIGHT"}}},
      {U"thismorning", {{"day_offset", "0"}, {"part_of_day", "MORNING"}}},
      {U"thisafternoon", {{"day_offset", "0"}, {"part_of_day", "PM"}}},
      {U"thisevening", {{"day_offset", "0"}, {"part_of_day", "EVENING"}}},
      {U"tonight", {{"day_offset", "0"}, {"part_of_day", "NIGHT"}}},
      {U"am", {{"part_of_day", "AM"}}},
      {U"a.m.", {{"part_of_day", "AM"}}},
      {U"pm", {{"part_of_day", "PM"}}},
      {U"p.m.", {{"part_of_day", "PM"}}}};
  for (const auto& p : parts_en) add(p.first, p.second);
  add(U"noon", {{"hour", "12"}, {"minute", "0"}});
  add(U"midnight", {{"hour", "0"}, {"minute", "0"}});
  // Longest first, so 그저께 wins over 그제 and march over mar.
  std::stable_sort(words.begin(), words.end(),
                   [](const Word& a, const Word& b) { return a.text.size() > b.text.size(); });
  return words;
}

const std::pair<const char32_t*, int> kNativeHours[] = {
    {U"열한", 11}, {U"열두", 12}, {U"다섯", 5}, {U"여섯", 6}, {U"일곱", 7}, {U"여덟", 8},
    {U"아홉", 9},  {U"한", 1},    {U"두", 2},   {U"세", 3},   {U"네", 4},   {U"열", 10}};

bool is_sino(char32_t c) {
  return std::u32string(U"일이삼사오육칠팔구십백천").find(c) != std::u32string::npos;
}

bool starts_with(const std::u32string& s, std::size_t at, const std::u32string& prefix) {
  return s.compare(at, prefix.size(), prefix) == 0;
}

// Stores `value` under `key` if it lies in [low, high].
void set_field(Attributes& attrs, const std::string& key, long long value, long long low,
               long long high) {
  if (value >= low && value <= high) set(attrs, key, std::to_string(value));
}

struct Unit {
  const char32_t* text;
  const char* field;
  long long low, high;
};

const Unit kUnits[] = {{U"o'clock", "hour", 0, 24}, {U"년", "year", 0, 9999},
                       {U"월", "month", 1, 12},     {U"일", "day", 1, 31},
                       {U"시", "hour", 0, 24},      {U"분", "minute", 0, 59},
                       {U"st", "day", 1, 31},       {U"nd", "day", 1, 31},
                       {U"rd", "day", 1, 31},       {U"th", "day", 1, 31}};

// The unit at `at`, if any.
const Unit* unit_at(const std::u32string& s, std::size_t at) {
  for (const Unit& u : kUnits)
    if (starts_with(s, at, u.text)) return &u;
  return nullptr;
}

// The hour on a 24-hour clock, given the part of the day it falls in.
long long to_24_hour(long long hour, const std::string& part) {
  auto between = [&](long long low, long long high) { return hour >= low && hour <= high; };
  if (part == "AM") return hour == 12 ? 0 : hour;
  if (part == "PM" || part == "EVENING") return between(1, 11) ? hour + 12 : hour;
  if (part == "NIGHT") return between(6, 11) ? hour + 12 : hour == 12 ? 0 : hour;
  if (part == "DAY") return between(1, 6) ? hour + 12 : hour;
  if (part == "NOON") return between(1, 5) ? hour + 12 : hour;
  return hour;
}

}  // namespace

Attributes datetime_attributes(const std::string& text) {
  static const std::vector<Word> words = make_words();
  std::u32string s;
  for (const DecodedChar& d : decode_utf8(text))
    if (!is_space(d.code)) s += fold_case(d.code);

  Attributes attrs;
  bool after_month_name = false;  // "march 5": a bare number is the day
  std::size_t i = 0;
  while (i < s.size()) {
    // A number in digits, maybe with minutes (3:30), then its unit.
    if (s[i] >= U'0' && s[i] <= U'9') {
      long long value = 0;
      for (; i < s.size() && s[i] >= U'0' && s[i] <= U'9'; ++i) value = value * 10 + (s[i] - U'0');
      long long minute = -1;
      if (i + 2 < s.size() && s[i] == U':') {
        minute = (s[i + 1] - U'0') * 10 + (s[i + 2] - U'0');
        i += 3;
      }
      if (const Unit* unit = unit_at(s, i)) {
        set_field(attrs, unit->field, value, unit->low, unit->high);
        i += std::char_traits<char32_t>::length(unit->text);
      } else if (after_month_name) {
        set_field(attrs, "day", value, 1, 31);
      } else {
        set_field(attrs, "hour", value, 0, 24);  // 15:30, or 3 before am/pm
      }
      if (minute >= 0) set_field(attrs, "minute", minute, 0, 59);
      after_month_name = false;
      continue;
    }
    // A native Korean hour: 세 시, 열두시.
    bool native = false;
    for (const auto& h : kNativeHours) {
      std::u32string n = h.first;
      if (starts_with(s, i, n) && starts_with(s, i + n.size(), U"시")) {
        set_field(attrs, "hour", h.second, 1, 12);
        i += n.size() + 1;
        native = true;
        break;
      }
    }
    if (native) continue;
    // A word (일요일, 오후, next monday). Words come before numerals, since
    // some start with one (오후, 이번주).
    const auto word = std::find_if(words.begin(), words.end(),
                                   [&](const Word& w) { return starts_with(s, i, w.text); });
    if (word != words.end()) {
      for (const auto& kv : word->attrs) set(attrs, kv.first, kv.second);
      after_month_name = get(word->attrs, "month") != nullptr;
      i += word->text.size();
      continue;
    }
    // Sino-Korean numerals with a unit (삼월, 십오일, 이십일일). The run may
    // swallow the unit 일 (day), which is then given back.
    std::size_t end = i;
    while (end < s.size() && is_sino(s[end])) ++end;
    if (end > i + 1 && !unit_at(s, end) && s[end - 1] == U'일') --end;
    const Unit* unit = end > i ? unit_at(s, end) : nullptr;
    std::string value;
    if (unit && korean_number_value(to_utf8(s.substr(i, end - i)), value)) {
      set_field(attrs, unit->field, std::stoll(value), unit->low, unit->high);
      i = end + std::char_traits<char32_t>::length(unit->text);
      after_month_name = false;
      continue;
    }
    ++i;  // "of" and anything else the grammar allows around the fields
  }

  const std::string* hour = get(attrs, "hour");
  const std::string* part = get(attrs, "part_of_day");
  if (hour && part) set(attrs, "hour", std::to_string(to_24_hour(std::stoll(*hour), *part)));
  return attrs;
}

}  // namespace detail
}  // namespace nlu

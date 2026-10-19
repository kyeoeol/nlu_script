#include "datetime.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <string>

#include "nlu_script/nlu_script.hpp"
#include "numbers.hpp"
#include "unicode.hpp"

namespace nlu {
namespace detail {

void set_attribute(Attributes& attrs, const std::string& key, const std::string& value) {
  for (auto& kv : attrs) {
    if (kv.first == key) {
      kv.second = value;
      return;
    }
  }
  attrs.emplace_back(key, value);
}

namespace {

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
  return std::u32string_view(U"일이삼사오육칠팔구십백천").find(c) != std::u32string_view::npos;
}

// Stores `value` under `key` if it lies in [low, high].
void set_field(Attributes& attrs, const std::string& key, long long value, long long low,
               long long high) {
  if (value >= low && value <= high) set_attribute(attrs, key, std::to_string(value));
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


// Reads one date or time (not a range or duration), from text without
// spaces. Hours stay as written; adjust_hour() puts them on a 24-hour clock.
Attributes scan_single(const std::u32string& s) {
  static const std::vector<Word> words = make_words();

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
      for (const auto& kv : word->attrs) set_attribute(attrs, kv.first, kv.second);
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

  return attrs;
}


// Puts the hour on a 24-hour clock when the part of the day settles it.
void adjust_hour(Attributes& attrs) {
  const std::string* hour = get(attrs, "hour");
  const std::string* part = get(attrs, "part_of_day");
  if (hour && part) set_attribute(attrs, "hour", std::to_string(to_24_hour(std::stoll(*hour), *part)));
}

std::u32string strip_spaces(const std::u32string& s) {
  std::u32string out;
  for (char32_t c : s)
    if (c != U' ') out += c;
  return out;
}

// --- durations ---------------------------------------------------------------

struct Count {
  const char32_t* text;
  int value;
};

// Counts written as words, longest first.
const Count kCountWords[] = {
    {U"eleven", 11}, {U"twelve", 12}, {U"three", 3}, {U"seven", 7}, {U"eight", 8},
    {U"four", 4},    {U"five", 5},    {U"nine", 9},  {U"one", 1},   {U"two", 2},
    {U"six", 6},     {U"ten", 10},    {U"an", 1},    {U"a", 1},     {U"열한", 11},
    {U"열두", 12},   {U"다섯", 5},    {U"여섯", 6},  {U"일곱", 7},  {U"여덟", 8},
    {U"아홉", 9},    {U"한", 1},      {U"두", 2},    {U"세", 3},    {U"네", 4},
    {U"열", 10}};

struct DurationUnit {
  const char32_t* text;
  const char* field;
};

// Longest first, so 시간 wins over 시 and minutes over minute.
const DurationUnit kDurationUnits[] = {
    {U"minutes", "minute_offset"}, {U"minute", "minute_offset"}, {U"months", "month_offset"},
    {U"month", "month_offset"},    {U"hours", "hour_offset"},    {U"hour", "hour_offset"},
    {U"weeks", "week_offset"},     {U"week", "week_offset"},     {U"years", "year_offset"},
    {U"year", "year_offset"},      {U"days", "day_offset"},      {U"day", "day_offset"},
    {U"시간", "hour_offset"},      {U"개월", "month_offset"},    {U"주일", "week_offset"},
    {U"분", "minute_offset"},      {U"달", "month_offset"},      {U"주", "week_offset"},
    {U"일", "day_offset"},         {U"년", "year_offset"}};

const DurationUnit* duration_unit_at(const std::u32string& s, std::size_t at) {
  for (const DurationUnit& u : kDurationUnits)
    if (starts_with(s, at, u.text)) return &u;
  return nullptr;
}

// Whether `s` (without spaces) is a duration such as 2시간뒤 or in2hours.
bool is_duration(const std::u32string& s) {
  if (ends_with(s, U"뒤") || ends_with(s, U"후") || (ends_with(s, U"전") && !ends_with(s, U"오전")))
    return true;
  if (ends_with(s, U"ago") || ends_with(s, U"later") || ends_with(s, U"fromnow")) return true;
  return starts_with(s, 0, U"in") && !starts_with(s, 0, U"inthe") && s.size() > 2;
}

// Reads a duration into signed offsets: 1시간30분후 gives hour_offset=+1 and
// minute_offset=+30, 3daysago day_offset=-3.
Attributes scan_duration(const std::u32string& s) {
  const int sign = ends_with(s, U"전") || ends_with(s, U"ago") ? -1 : 1;
  static const Count kDayWords[] = {{U"하루", 1}, {U"이틀", 2}, {U"사흘", 3}, {U"나흘", 4}};
  static const char32_t* const kFiller[] = {U"fromnow", U"later", U"ago", U"in",
                                            U"뒤",      U"후",    U"전"};
  Attributes attrs;
  std::size_t i = 0;
  auto add = [&](const char* field, long long value) {
    set_attribute(attrs, field, offset(static_cast<int>(sign * value)));
  };
  while (i < s.size()) {
    bool done = false;
    for (const Count& d : kDayWords) {
      if (starts_with(s, i, d.text)) {
        add("day_offset", d.value);
        i += std::char_traits<char32_t>::length(d.text);
        done = true;
        break;
      }
    }
    if (done) continue;
    // The count: digits, a word, or sino-Korean numerals.
    long long value = -1;
    std::size_t end = i;
    if (s[i] >= U'0' && s[i] <= U'9') {
      value = 0;
      for (; end < s.size() && s[end] >= U'0' && s[end] <= U'9'; ++end)
        value = value * 10 + (s[end] - U'0');
    } else {
      for (const Count& c : kCountWords) {
        std::size_t n = std::char_traits<char32_t>::length(c.text);
        if (starts_with(s, i, c.text) && duration_unit_at(s, i + n)) {
          value = c.value;
          end = i + n;
          break;
        }
      }
      if (value < 0) {
        while (end < s.size() && is_sino(s[end])) ++end;
        if (end > i + 1 && !duration_unit_at(s, end) && s[end - 1] == U'일') --end;
        std::string digits;
        if (end > i && duration_unit_at(s, end) &&
            korean_number_value(to_utf8(s.substr(i, end - i)), digits))
          value = std::stoll(digits);
      }
    }
    if (value >= 0) {
      if (const DurationUnit* unit = duration_unit_at(s, end)) {
        add(unit->field, value);
        i = end + std::char_traits<char32_t>::length(unit->text);
        continue;
      }
    }
    bool filler = false;
    for (const char32_t* f : kFiller) {
      if (starts_with(s, i, f)) {
        i += std::char_traits<char32_t>::length(f);
        filler = true;
        break;
      }
    }
    if (!filler) ++i;
  }
  return attrs;
}

// --- ranges ------------------------------------------------------------------

// Splits a range such as 3시부터5시까지 or "from 3 to 5pm" into its two ends
// (text with spaces, folded). Returns false if `s` is not a range.
bool split_range(const std::u32string& s, std::u32string& first, std::u32string& second) {
  auto trim = [](std::u32string t) {
    while (!t.empty() && t.front() == U' ') t.erase(t.begin());
    while (!t.empty() && t.back() == U' ') t.pop_back();
    return t;
  };
  auto cut = [&](std::size_t at, std::size_t length) {
    first = trim(s.substr(0, at));
    second = trim(s.substr(at + length));
    for (const char32_t* end : {U"까지", U"사이"})
      if (ends_with(second, end)) second = trim(second.substr(0, second.size() - 2));
    return !first.empty() && !second.empty();
  };
  for (const char32_t* marker : {U"부터", U"에서"}) {
    std::size_t at = s.find(marker);
    if (at != std::u32string::npos) return cut(at, 2);
  }
  std::size_t at = s.find(U'~');
  if (at != std::u32string::npos) return cut(at, 1);
  // English: "from A to B", "between A and B", "A until B".
  std::u32string t = s;
  for (const char32_t* lead : {U"from ", U"between "})
    if (starts_with(t, 0, lead)) t = t.substr(std::char_traits<char32_t>::length(lead));
  for (const char32_t* word : {U" to ", U" until ", U" till ", U" and "}) {
    at = t.find(word);
    if (at != std::u32string::npos) {
      std::size_t length = std::char_traits<char32_t>::length(word);
      first = trim(t.substr(0, at));
      second = trim(t.substr(at + length));
      return !first.empty() && !second.empty();
    }
  }
  at = t.find(U'-');
  if (at != std::u32string::npos) {
    first = trim(t.substr(0, at));
    second = trim(t.substr(at + 1));
    return !first.empty() && !second.empty();
  }
  return false;
}

}  // namespace

Attributes datetime_attributes(const std::string& text) {
  std::u32string s;
  for (const DecodedChar& d : decode_utf8(text)) s += is_space(d.code) ? U' ' : fold_case(d.code);

  std::u32string first, second;
  if (!split_range(s, first, second)) {
    std::u32string compact = strip_spaces(s);
    if (is_duration(compact)) return scan_duration(compact);
    Attributes attrs = scan_single(compact);
    adjust_hour(attrs);
    return attrs;
  }
  Attributes start = scan_single(strip_spaces(first));
  Attributes end = scan_single(strip_spaces(second));
  // One end's part of the day carries over to the other's hour: 오후 3시부터
  // 5시까지, from 3 to 5pm (but not 11 to 2pm, where 11 is in the morning).
  const std::string* start_part = get(start, "part_of_day");
  const std::string* end_part = get(end, "part_of_day");
  const std::string* start_hour = get(start, "hour");
  const std::string* end_hour = get(end, "hour");
  if (start_hour && end_hour) {
    if (!start_part && end_part && std::stoll(*start_hour) % 12 <= std::stoll(*end_hour) % 12)
      set_attribute(start, "part_of_day", *end_part);
    else if (start_part && !end_part)
      set_attribute(end, "part_of_day", *start_part);
  }
  adjust_hour(start);
  adjust_hour(end);
  for (const auto& kv : end) start.emplace_back("end_" + kv.first, kv.second);
  return start;
}

const char* const kGrains[6] = {"year", "month", "week", "day", "hour", "minute"};

}  // namespace detail

// --- resolving against a reference time ----------------------------------------

namespace {

using detail::Attributes;
using detail::kGrains;

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
// days_from_civil), and back.
long long days_from_civil(long long y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long long>(doe) - 719468;
}

void civil_from_days(long long z, int& year, int& month, int& day) {
  z += 719468;
  const long long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  year = static_cast<int>(yoe + era * 400 + (month <= 2));
}

int days_in_month(int year, int month) {
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return month == 2 && leap ? 29 : days[month - 1];
}

int weekday_index(const std::string& name) {  // Monday 0
  static const char* const names[] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
  for (int i = 0; i < 7; ++i)
    if (name == names[i]) return i;
  return -1;
}

const std::string* field(const Attributes& attrs, const std::string& prefix,
                         const std::string& key) {
  for (const auto& kv : attrs)
    if (kv.first == prefix + key) return &kv.second;
  return nullptr;
}

// The value of field `key` if it is an integer in the field's range (offsets
// up to 100,000 either way); values that aren't are treated as absent, so
// arithmetic on them can't overflow.
bool field_value(const std::string& key, const std::string& text, int& value) {
  if (text.empty()) return false;
  errno = 0;
  char* end = nullptr;
  const long long v = std::strtoll(text.c_str(), &end, 10);
  if (errno || *end) return false;
  long long low = -100000, high = 100000;
  if (key == "year") low = 0, high = 9999;
  if (key == "month") low = 1, high = 12;
  if (key == "day") low = 1, high = 31;
  if (key == "hour") low = 0, high = 24;
  if (key == "minute") low = 0, high = 59;
  if (v < low || v > high) return false;
  value = static_cast<int>(v);
  return true;
}

// Resolves the fields under `prefix` ("" or "end_"). Dates start from `base`,
// times and durations from `now`; `grain` (an index into kGrains) only gets finer.
DateTimeValue resolve_one(const Attributes& attrs, const std::string& prefix,
                          const DateTimeValue& now, const DateTimeValue& base, int& grain) {
  auto get = [&](const char* key) {
    const std::string* text = field(attrs, prefix, key);
    int value = 0;
    return text && field_value(key, *text, value) ? value : 0;
  };
  auto has = [&](const char* key) {
    const std::string* text = field(attrs, prefix, key);
    int value;
    return text && (std::string(key) == "weekday" ? weekday_index(*text) >= 0
                                                  : field_value(key, *text, value));
  };
  auto finer = [&](int g) { grain = std::max(grain, g); };
  int year = base.year, month = base.month, day = base.day;
  long long today = days_from_civil(now.year, static_cast<unsigned>(now.month),
                                    static_cast<unsigned>(now.day));
  auto clamp_day = [&] { day = std::min(day, days_in_month(year, month)); };
  auto add_months = [&](int k) {
    int total = year * 12 + (month - 1) + k;
    year = total / 12;
    month = total % 12 + 1;
  };

  if (has("year_offset")) {
    year += get("year_offset");
    month = day = 1;
    finer(0);
  }
  if (has("year")) {
    year = get("year");
    if (year < 100) year += 2000;
    month = day = 1;
    finer(0);
  }
  if (has("month_offset")) {
    add_months(get("month_offset"));
    day = 1;
    finer(1);
  }
  if (has("month")) {
    month = get("month");
    day = has("day") ? get("day") : 1;
    finer(1);
    // A month (and day) alone means the next one, this one included.
    if (!has("year") && !has("year_offset") && !has("month_offset")) {
      clamp_day();
      const bool past = has("day") ? days_from_civil(year, static_cast<unsigned>(month),
                                                     static_cast<unsigned>(day)) < today
                                   : month < now.month;
      if (past && year == now.year) ++year;
    }
  }
  if (has("day")) {
    day = get("day");
    finer(3);
    if (!has("month") && !has("month_offset") && !has("year") && !has("year_offset") &&
        year == now.year && month == now.month && day < now.day)
      add_months(1);
  }
  clamp_day();
  long long date = days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
  const long long today_weekday = ((today + 3) % 7 + 7) % 7;  // 1970-01-01 was a Thursday
  const long long monday = today - today_weekday;
  if (has("week_offset")) {
    date = monday + 7LL * get("week_offset");
    finer(2);
  }
  if (has("weekday")) {
    const int wd = weekday_index(*field(attrs, prefix, "weekday"));
    if (has("week_offset")) {
      date += wd;
    } else if (!has("month") && !has("day")) {
      date = today + ((wd - today_weekday) + 7) % 7;  // the next one, today included
    }
    finer(3);
  }
  if (has("day_offset")) {
    date += get("day_offset");
    finer(3);
  }

  DateTimeValue out{0, 0, 0, -1, -1};
  if (has("hour_offset") || has("minute_offset")) {
    // A duration counts from now.
    long long minutes = today * 1440 + std::max(now.hour, 0) * 60 + std::max(now.minute, 0) +
                        60LL * get("hour_offset") + get("minute_offset");
    long long days = minutes >= 0 ? minutes / 1440 : (minutes - 1439) / 1440;
    civil_from_days(days + (date - today), out.year, out.month, out.day);
    out.hour = static_cast<int>(minutes - days * 1440) / 60;
    out.minute = static_cast<int>(minutes - days * 1440) % 60;
    finer(5);
    return out;
  }
  civil_from_days(date, out.year, out.month, out.day);
  if (has("hour")) {
    out.hour = get("hour") % 24;
    out.minute = has("minute") ? get("minute") : 0;
    finer(has("minute") && get("minute") != 0 ? 5 : 4);
  } else if (has("minute")) {
    out.hour = std::max(now.hour, 0);
    out.minute = get("minute");
    finer(5);
  }
  if (!grain && !has("year") && !has("year_offset")) finer(3);  // nothing coarser was given
  return out;
}

}  // namespace

DateTimeRange resolve_datetime(const std::vector<std::pair<std::string, std::string>>& attributes,
                               const DateTimeValue& reference) {
  DateTimeValue now = reference;
  now.year = std::min(std::max(now.year, 0), 9999);
  now.month = std::min(std::max(now.month, 1), 12);
  now.day = std::min(std::max(now.day, 1), days_in_month(now.year, now.month));
  now.hour = std::min(std::max(now.hour, -1), 23);
  now.minute = std::min(std::max(now.minute, -1), 59);
  DateTimeRange out;
  int grain = 0;
  out.start = resolve_one(attributes, "", now, now, grain);
  bool end_date = false, any_end = false;
  for (const auto& kv : attributes) {
    if (kv.first.compare(0, 4, "end_") != 0) continue;
    any_end = true;
    for (const char* key : {"end_year", "end_month", "end_day", "end_weekday", "end_day_offset",
                            "end_week_offset", "end_month_offset", "end_year_offset"})
      end_date = end_date || kv.first == key;
  }
  if (any_end) {
    int end_grain = 0;
    out.end = resolve_one(attributes, "end_", now, end_date ? now : out.start, end_grain);
    out.has_end = true;
  }
  out.grain = kGrains[grain];
  return out;
}

}  // namespace nlu

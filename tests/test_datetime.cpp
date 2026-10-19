// Behavior of the DATETIME system entity and its normalized attributes.
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "harness.hpp"
#include "nlu_script/nlu_script.hpp"

namespace {

// The DATETIME slot's attributes as "key=value ...", for input that is a
// date or time and nothing else.
std::string attributes(const std::string& text) {
  static const nlu::Script script = nlu::Script::compile("t\n    `@{DATETIME}\n");
  std::vector<nlu::Match> found = script.match(text);
  if (found.size() != 1 || found[0].slots.size() != 1) return "<no match>";
  std::string out;
  for (const auto& kv : found[0].slots[0].attributes)
    out += (out.empty() ? "" : " ") + kv.first + "=" + kv.second;
  return out;
}

}  // namespace

TEST(datetime_relative_days_and_weeks_in_korean) {
  CHECK_EQ(attributes("오늘"), std::string("day_offset=0"));
  CHECK_EQ(attributes("내일"), std::string("day_offset=+1"));
  CHECK_EQ(attributes("모레"), std::string("day_offset=+2"));
  CHECK_EQ(attributes("그저께"), std::string("day_offset=-2"));
  CHECK_EQ(attributes("다음 주 월요일"), std::string("week_offset=+1 weekday=MON"));
  CHECK_EQ(attributes("지난주 금요일"), std::string("week_offset=-1 weekday=FRI"));
  CHECK_EQ(attributes("이번 주"), std::string("week_offset=0"));
  CHECK_EQ(attributes("일요일"), std::string("weekday=SUN"));
  CHECK_EQ(attributes("내년 3월"), std::string("year_offset=+1 month=3"));
  CHECK_EQ(attributes("다음 달 15일"), std::string("month_offset=+1 day=15"));
}

TEST(datetime_calendar_dates_in_korean) {
  CHECK_EQ(attributes("3월 5일"), std::string("month=3 day=5"));
  CHECK_EQ(attributes("2026년 3월 5일"), std::string("year=2026 month=3 day=5"));
  CHECK_EQ(attributes("12월31일"), std::string("month=12 day=31"));
  CHECK_EQ(attributes("삼월 십오일"), std::string("month=3 day=15"));
  CHECK_EQ(attributes("이십일일"), std::string("day=21"));
  CHECK_EQ(attributes("13월"), std::string("<no match>"));
  CHECK_EQ(attributes("32일"), std::string("<no match>"));
  // Numerals the grammar accepts but no calendar has: the value is dropped.
  CHECK_EQ(attributes("십삼월"), std::string(""));
}

TEST(datetime_times_in_korean) {
  CHECK_EQ(attributes("오후 3시"), std::string("part_of_day=PM hour=15"));
  CHECK_EQ(attributes("오전 10시 30분"), std::string("part_of_day=AM hour=10 minute=30"));
  CHECK_EQ(attributes("저녁 7시 반"), std::string("part_of_day=EVENING hour=19 minute=30"));
  CHECK_EQ(attributes("밤 11시"), std::string("part_of_day=NIGHT hour=23"));
  CHECK_EQ(attributes("밤 1시"), std::string("part_of_day=NIGHT hour=1"));
  CHECK_EQ(attributes("세 시 반"), std::string("hour=3 minute=30"));  // 오전 or 오후 unknown
  CHECK_EQ(attributes("열두시 삼십분"), std::string("hour=12 minute=30"));
  CHECK_EQ(attributes("15시"), std::string("hour=15"));
  CHECK_EQ(attributes("정오"), std::string("hour=12 minute=0"));
  CHECK_EQ(attributes("아침"), std::string("part_of_day=MORNING"));
  CHECK_EQ(attributes("25시"), std::string("<no match>"));
  CHECK_EQ(attributes("3시 60분"), std::string("<no match>"));
}

TEST(datetime_dates_and_times_combine) {
  CHECK_EQ(attributes("내일 오후 3시"), std::string("day_offset=+1 part_of_day=PM hour=15"));
  CHECK_EQ(attributes("다음주 금요일 저녁 7시 반"),
           std::string("week_offset=+1 weekday=FRI part_of_day=EVENING hour=19 minute=30"));
  CHECK_EQ(attributes("2026년 3월 5일 오전 10시 30분"),
           std::string("year=2026 month=3 day=5 part_of_day=AM hour=10 minute=30"));
  CHECK_EQ(attributes("tomorrow 3pm"), std::string("day_offset=+1 hour=15 part_of_day=PM"));
  CHECK_EQ(attributes("내일 내일"), std::string("<no match>"));
  CHECK_EQ(attributes("다음"), std::string("<no match>"));
}

TEST(datetime_in_english) {
  CHECK_EQ(attributes("today"), std::string("day_offset=0"));
  CHECK_EQ(attributes("Tomorrow"), std::string("day_offset=+1"));
  CHECK_EQ(attributes("the day after tomorrow"), std::string("day_offset=+2"));
  CHECK_EQ(attributes("next Monday"), std::string("week_offset=+1 weekday=MON"));
  CHECK_EQ(attributes("last week"), std::string("week_offset=-1"));
  CHECK_EQ(attributes("March 5th"), std::string("month=3 day=5"));
  CHECK_EQ(attributes("5th of march"), std::string("day=5 month=3"));
  CHECK_EQ(attributes("dec 31"), std::string("month=12 day=31"));
  CHECK_EQ(attributes("3:30 pm"), std::string("hour=15 minute=30 part_of_day=PM"));
  CHECK_EQ(attributes("12am"), std::string("hour=0 part_of_day=AM"));
  CHECK_EQ(attributes("15:30"), std::string("hour=15 minute=30"));
  CHECK_EQ(attributes("noon"), std::string("hour=12 minute=0"));
  CHECK_EQ(attributes("tonight"), std::string("day_offset=0 part_of_day=NIGHT"));
  CHECK_EQ(attributes("friday evening"), std::string("weekday=FRI part_of_day=EVENING"));
  CHECK_EQ(attributes("march 32"), std::string("<no match>"));
  CHECK_EQ(attributes("13pm"), std::string("<no match>"));
}

TEST(datetime_slot_in_a_sentence) {
  nlu::Script s = nlu::Script::compile(
      "agent:callback\n"
      "    `@{DATETIME}::(에) 전화 주세요\n"
      "    call me back `@{DATETIME}\n");
  std::vector<nlu::Match> found = s.match("내일 오후 3시에 전화 주세요");
  CHECK_EQ(found.size(), 1u);
  if (found.size() == 1)
    CHECK_EQ(found[0].tagged,
             std::string("<DATETIME day_offset=\"+1\" part_of_day=\"PM\" hour=\"15\">"
                         "내일 오후 3시</DATETIME>에 전화 주세요"));
  found = s.match("call me back tomorrow at 3pm");
  CHECK_EQ(found.size(), 1u);
  if (found.size() == 1)
    CHECK_EQ(found[0].tagged,
             std::string("call me back <DATETIME day_offset=\"+1\" hour=\"15\" "
                         "part_of_day=\"PM\">tomorrow at 3pm</DATETIME>"));
  // A leading "at" belongs to the pattern, not the entity.
  CHECK(s.match("call me back at 3pm").empty());
}

TEST(datetime_ranges) {
  CHECK_EQ(attributes("3시부터 5시까지"), std::string("hour=3 end_hour=5"));
  CHECK_EQ(attributes("오후 3시부터 5시까지"),
           std::string("part_of_day=PM hour=15 end_hour=17 end_part_of_day=PM"));
  CHECK_EQ(attributes("내일부터 모레까지"), std::string("day_offset=+1 end_day_offset=+2"));
  CHECK_EQ(attributes("3시~5시"), std::string("hour=3 end_hour=5"));
  CHECK_EQ(attributes("from 3 to 5pm"),
           std::string("hour=15 part_of_day=PM end_hour=17 end_part_of_day=PM"));
  CHECK_EQ(attributes("3-5pm"), std::string("hour=15 part_of_day=PM end_hour=17 end_part_of_day=PM"));
  CHECK_EQ(attributes("between monday and friday"),
           std::string("weekday=MON end_weekday=FRI"));
  // A bare number is an hour only in an English range.
  CHECK_EQ(attributes("3"), std::string("<no match>"));
}

TEST(datetime_durations) {
  CHECK_EQ(attributes("2시간 뒤"), std::string("hour_offset=+2"));
  CHECK_EQ(attributes("1시간 30분 후"), std::string("hour_offset=+1 minute_offset=+30"));
  CHECK_EQ(attributes("두 시간 후"), std::string("hour_offset=+2"));
  CHECK_EQ(attributes("삼십 분 뒤"), std::string("minute_offset=+30"));
  CHECK_EQ(attributes("이틀 전"), std::string("day_offset=-2"));
  CHECK_EQ(attributes("3주 뒤"), std::string("week_offset=+3"));
  CHECK_EQ(attributes("한 달 후"), std::string("month_offset=+1"));
  CHECK_EQ(attributes("in 2 hours"), std::string("hour_offset=+2"));
  CHECK_EQ(attributes("in an hour"), std::string("hour_offset=+1"));
  CHECK_EQ(attributes("3 days ago"), std::string("day_offset=-3"));
  CHECK_EQ(attributes("2 weeks from now"), std::string("week_offset=+2"));
  CHECK_EQ(attributes("in 1 hour 30 minutes"), std::string("hour_offset=+1 minute_offset=+30"));
}

TEST(datetime_reads_decomposed_hangul) {
  // 내일 as conjoining jamo (NFD).
  CHECK_EQ(attributes("\u1102\u1162\u110B\u1175\u11AF"), std::string("day_offset=+1"));
}

TEST(datetime_more_orders) {
  CHECK_EQ(attributes("3월 5일 월요일"), std::string("month=3 day=5 weekday=MON"));
  CHECK_EQ(attributes("3pm tomorrow"), std::string("hour=15 part_of_day=PM day_offset=+1"));
  CHECK_EQ(attributes("noon on friday"), std::string("hour=12 minute=0 weekday=FRI"));
}

namespace {

std::string resolved(const std::string& text, const nlu::DateTimeValue& now) {
  static const nlu::Script script = nlu::Script::compile("t\n    `@{DATETIME}\n");
  std::vector<nlu::Match> found = script.match(text);
  if (found.size() != 1) return "<no match>";
  nlu::DateTimeRange r = nlu::resolve_datetime(found[0].slots[0].attributes, now);
  auto show = [](const nlu::DateTimeValue& v) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02d", v.year, v.month, v.day);
    std::string out = buffer;
    if (v.hour >= 0) {
      std::snprintf(buffer, sizeof buffer, " %02d:%02d", v.hour, v.minute < 0 ? 0 : v.minute);
      out += buffer;
    }
    return out;
  };
  return show(r.start) + (r.has_end ? " ~ " + show(r.end) : "") + " (" + r.grain + ")";
}

}  // namespace

TEST(datetime_resolves_against_a_reference_time) {
  // Wednesday 2026-10-21, 14:20. Weeks start on Monday (ISO 8601).
  const nlu::DateTimeValue now{2026, 10, 21, 14, 20};
  CHECK_EQ(resolved("내일 오후 3시", now), std::string("2026-10-22 15:00 (hour)"));
  CHECK_EQ(resolved("모레", now), std::string("2026-10-23 (day)"));
  CHECK_EQ(resolved("다음 주 월요일", now), std::string("2026-10-26 (day)"));
  CHECK_EQ(resolved("이번 주 금요일", now), std::string("2026-10-23 (day)"));
  CHECK_EQ(resolved("다음 주", now), std::string("2026-10-26 (week)"));
  // A weekday, month or day alone means the next one, today included.
  CHECK_EQ(resolved("월요일", now), std::string("2026-10-26 (day)"));
  CHECK_EQ(resolved("수요일", now), std::string("2026-10-21 (day)"));
  CHECK_EQ(resolved("3월 5일", now), std::string("2027-03-05 (day)"));
  CHECK_EQ(resolved("12월 25일", now), std::string("2026-12-25 (day)"));
  CHECK_EQ(resolved("15일", now), std::string("2026-11-15 (day)"));
  CHECK_EQ(resolved("2026년 3월 5일", now), std::string("2026-03-05 (day)"));
  CHECK_EQ(resolved("다음 달 15일", now), std::string("2026-11-15 (day)"));
  CHECK_EQ(resolved("내년", now), std::string("2027-01-01 (year)"));
  CHECK_EQ(resolved("2시간 뒤", now), std::string("2026-10-21 16:20 (minute)"));
  CHECK_EQ(resolved("30분 후", now), std::string("2026-10-21 14:50 (minute)"));
  CHECK_EQ(resolved("3 days ago", now), std::string("2026-10-18 (day)"));
  CHECK_EQ(resolved("tonight", now), std::string("2026-10-21 (day)"));
  CHECK_EQ(resolved("3시부터 5시까지", now),
           std::string("2026-10-21 03:00 ~ 2026-10-21 05:00 (hour)"));
  // The end of a range takes the start's date when it gives none.
  CHECK_EQ(resolved("내일 3시부터 5시까지", now),
           std::string("2026-10-22 03:00 ~ 2026-10-22 05:00 (hour)"));
  CHECK_EQ(resolved("내일부터 모레까지", now), std::string("2026-10-22 ~ 2026-10-23 (day)"));
  // Month ends and leap years.
  CHECK_EQ(resolved("내일", nlu::DateTimeValue{2028, 2, 28, -1, -1}), std::string("2028-02-29 (day)"));
  CHECK_EQ(resolved("다음 달", nlu::DateTimeValue{2026, 1, 31, -1, -1}),
           std::string("2026-02-01 (month)"));
  CHECK_EQ(resolved("1시간 뒤", nlu::DateTimeValue{2026, 12, 31, 23, 30}),
           std::string("2027-01-01 00:30 (minute)"));
}

TEST(resolve_datetime_ignores_attributes_out_of_range) {
  const nlu::DateTimeValue now{2026, 10, 21, 14, 20};
  auto date = [&](const std::vector<std::pair<std::string, std::string>>& attributes) {
    nlu::DateTimeValue v = nlu::resolve_datetime(attributes, now).start;
    return std::to_string(v.year) + "-" + std::to_string(v.month) + "-" + std::to_string(v.day);
  };
  CHECK_EQ(date({{"year_offset", "2147483647"}}), std::string("2026-10-21"));
  CHECK_EQ(date({{"day_offset", "-9999999999"}}), std::string("2026-10-21"));
  CHECK_EQ(date({{"month", "13"}}), std::string("2026-10-21"));
  CHECK_EQ(date({{"day", "0"}}), std::string("2026-10-21"));
  CHECK_EQ(date({{"weekday", "XYZ"}}), std::string("2026-10-21"));
  CHECK_EQ(date({{"day_offset", "+1x"}}), std::string("2026-10-21"));
  CHECK_EQ(date({{"day_offset", "+1"}, {"month", "13"}}), std::string("2026-10-22"));
}

TEST(resolve_datetime_clamps_the_reference_time) {
  nlu::DateTimeValue v = nlu::resolve_datetime({{"day_offset", "+1"}},
                                               nlu::DateTimeValue{2026, 13, 40, 99, 99}).start;
  CHECK_EQ(std::to_string(v.year) + "-" + std::to_string(v.month) + "-" + std::to_string(v.day),
           std::string("2027-1-1"));
}

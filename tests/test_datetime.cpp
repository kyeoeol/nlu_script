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
  CHECK_EQ(attributes("새벽 12시"), std::string("part_of_day=DAWN hour=0"));
  CHECK_EQ(attributes("새벽 3시"), std::string("part_of_day=DAWN hour=3"));
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

TEST(datetime_ending_in_a_part_of_the_day_is_not_a_duration) {
  // 오후 ends in 후 (after) and 오전 in 전 (before), like 2시간 후 and 이틀 전.
  CHECK_EQ(attributes("오후"), std::string("part_of_day=PM"));
  CHECK_EQ(attributes("내일 오후"), std::string("day_offset=+1 part_of_day=PM"));
  CHECK_EQ(attributes("일요일 오후"), std::string("weekday=SUN part_of_day=PM"));
  CHECK_EQ(attributes("다음 주 월요일 오후"),
           std::string("week_offset=+1 weekday=MON part_of_day=PM"));
  CHECK_EQ(attributes("내일 오전"), std::string("day_offset=+1 part_of_day=AM"));
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

TEST(datetime_english_date_then_time) {
  // The day and the hour are separate numbers, though spaces are ignored.
  CHECK_EQ(attributes("march 5 3pm"), std::string("month=3 day=5 hour=15 part_of_day=PM"));
  CHECK_EQ(attributes("dec 31 11:30"), std::string("month=12 day=31 hour=11 minute=30"));
  CHECK_EQ(attributes("march 5 at 3pm"), std::string("month=3 day=5 hour=15 part_of_day=PM"));
  // A number after the month is its day, unless the day came first.
  CHECK_EQ(attributes("5th of march at 3pm"),
           std::string("day=5 month=3 hour=15 part_of_day=PM"));
  CHECK_EQ(attributes("5th of march 3pm"), std::string("day=5 month=3 hour=15 part_of_day=PM"));
  // A number before `of` is the day.
  CHECK_EQ(attributes("5 of march"), std::string("day=5 month=3"));
}

TEST(datetime_months_and_days_of_the_month_in_english) {
  CHECK_EQ(attributes("this month"), std::string("month_offset=0"));
  CHECK_EQ(attributes("next month"), std::string("month_offset=+1"));
  CHECK_EQ(attributes("Last month"), std::string("month_offset=-1"));
  CHECK_EQ(attributes("the 25th"), std::string("day=25"));
  CHECK_EQ(attributes("the 1st"), std::string("day=1"));
  CHECK_EQ(attributes("the 25th at 3pm"), std::string("day=25 hour=15 part_of_day=PM"));
  CHECK_EQ(attributes("the 32nd"), std::string("<no match>"));
  // A day of the month alone takes its suffix; a bare number is not a date.
  CHECK_EQ(attributes("the 25"), std::string("<no match>"));
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

TEST(datetime_range_end_earlier_on_the_clock_is_in_the_next_half_of_the_day) {
  CHECK_EQ(attributes("오전 11시부터 1시까지"),
           std::string("part_of_day=AM hour=11 end_hour=13 end_part_of_day=PM"));
  CHECK_EQ(attributes("오전 11시부터 12시까지"),
           std::string("part_of_day=AM hour=11 end_hour=12 end_part_of_day=PM"));
  CHECK_EQ(attributes("아침 9시부터 1시까지"),
           std::string("part_of_day=MORNING hour=9 end_hour=13 end_part_of_day=PM"));
  CHECK_EQ(attributes("from 11am to 1 o'clock"),
           std::string("hour=11 part_of_day=AM end_hour=13 end_part_of_day=PM"));
  // After the evening or the night, the next half is the next day's morning.
  CHECK_EQ(attributes("밤 11시부터 1시까지"),
           std::string("part_of_day=NIGHT hour=23 end_hour=1 end_part_of_day=AM"));
  CHECK_EQ(attributes("밤 11시부터 12시까지"),
           std::string("part_of_day=NIGHT hour=23 end_hour=0 end_part_of_day=AM"));
  CHECK_EQ(attributes("저녁 7시부터 1시까지"),
           std::string("part_of_day=EVENING hour=19 end_hour=1 end_part_of_day=AM"));
}

TEST(datetime_durations) {
  CHECK_EQ(attributes("2시간 뒤"), std::string("hour_offset=+2 duration=true"));
  CHECK_EQ(attributes("1시간 30분 후"),
           std::string("hour_offset=+1 minute_offset=+30 duration=true"));
  CHECK_EQ(attributes("두 시간 후"), std::string("hour_offset=+2 duration=true"));
  CHECK_EQ(attributes("삼십 분 뒤"), std::string("minute_offset=+30 duration=true"));
  CHECK_EQ(attributes("이틀 전"), std::string("day_offset=-2 duration=true"));
  CHECK_EQ(attributes("3주 뒤"), std::string("week_offset=+3 duration=true"));
  CHECK_EQ(attributes("in 2 hours"), std::string("hour_offset=+2 duration=true"));
  CHECK_EQ(attributes("in an hour"), std::string("hour_offset=+1 duration=true"));
  CHECK_EQ(attributes("3 days ago"), std::string("day_offset=-3 duration=true"));
  CHECK_EQ(attributes("2 weeks from now"), std::string("week_offset=+2 duration=true"));
  CHECK_EQ(attributes("in 1 hour 30 minutes"),
           std::string("hour_offset=+1 minute_offset=+30 duration=true"));
  // A duration of months is not a calendar month: 한 달 후 is not 다음 달.
  CHECK_EQ(attributes("한 달 후"), std::string("month_offset=+1 duration=true"));
  CHECK_EQ(attributes("다음 달"), std::string("month_offset=+1"));
}

TEST(datetime_reads_decomposed_hangul) {
  // 내일 as conjoining jamo (NFD).
  CHECK_EQ(attributes("\u1102\u1162\u110B\u1175\u11AF"), std::string("day_offset=+1"));
}

TEST(datetime_more_orders) {
  CHECK_EQ(attributes("3월 5일 월요일"), std::string("month=3 day=5 weekday=MON"));
  // 일요일 starts with the numeral 일, which belongs to the weekday.
  CHECK_EQ(attributes("3월 이십일 일요일"), std::string("month=3 day=20 weekday=SUN"));
  CHECK_EQ(attributes("3월 오일 일요일"), std::string("month=3 day=5 weekday=SUN"));
  CHECK_EQ(attributes("이십일일 일요일"), std::string("day=21 weekday=SUN"));
  CHECK_EQ(attributes("3월 이십일일 일요일"), std::string("month=3 day=21 weekday=SUN"));
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
  CHECK_EQ(resolved("the 15th", now), std::string("2026-11-15 (day)"));
  CHECK_EQ(resolved("the 25th", now), std::string("2026-10-25 (day)"));
  CHECK_EQ(resolved("2026년 3월 5일", now), std::string("2026-03-05 (day)"));
  CHECK_EQ(resolved("다음 달 15일", now), std::string("2026-11-15 (day)"));
  CHECK_EQ(resolved("next month", now), std::string("2026-11-01 (month)"));
  CHECK_EQ(resolved("last month", now), std::string("2026-09-01 (month)"));
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
  // 24시 is midnight at the end of the day.
  CHECK_EQ(resolved("오늘 24시", now), std::string("2026-10-22 00:00 (hour)"));
  CHECK_EQ(resolved("22시부터 24시까지", now),
           std::string("2026-10-21 22:00 ~ 2026-10-22 00:00 (hour)"));
  // Month ends and leap years.
  CHECK_EQ(resolved("내일", nlu::DateTimeValue{2028, 2, 28, -1, -1}), std::string("2028-02-29 (day)"));
  CHECK_EQ(resolved("다음 달", nlu::DateTimeValue{2026, 1, 31, -1, -1}),
           std::string("2026-02-01 (month)"));
  CHECK_EQ(resolved("1시간 뒤", nlu::DateTimeValue{2026, 12, 31, 23, 30}),
           std::string("2027-01-01 00:30 (minute)"));
}

TEST(resolve_datetime_counts_durations_from_now) {
  const nlu::DateTimeValue now{2026, 10, 21, 14, 20};  // a Wednesday
  CHECK_EQ(resolved("한 달 후", now), std::string("2026-11-21 (day)"));
  CHECK_EQ(resolved("in 2 weeks", now), std::string("2026-11-04 (day)"));
  CHECK_EQ(resolved("a week ago", now), std::string("2026-10-14 (day)"));
  CHECK_EQ(resolved("1년 후", now), std::string("2027-10-21 (day)"));
  CHECK_EQ(resolved("2년 전", now), std::string("2024-10-21 (day)"));
  CHECK_EQ(resolved("3 days ago", now), std::string("2026-10-18 (day)"));
  CHECK_EQ(resolved("2시간 뒤", now), std::string("2026-10-21 16:20 (minute)"));
  CHECK_EQ(resolved("하루 2시간 후", now), std::string("2026-10-22 16:20 (minute)"));
  // Calendar offsets still start the week, month or year.
  CHECK_EQ(resolved("다음 달", now), std::string("2026-11-01 (month)"));
  CHECK_EQ(resolved("다음 주", now), std::string("2026-10-26 (week)"));
  // A month that is too short ends the count on its last day.
  CHECK_EQ(resolved("한 달 후", {2026, 1, 31, -1, -1}), std::string("2026-02-28 (day)"));
  CHECK_EQ(resolved("1년 후", {2028, 2, 29, -1, -1}), std::string("2029-02-28 (day)"));
  CHECK_EQ(resolved("1년 1개월 후", {2028, 1, 31, -1, -1}), std::string("2029-02-28 (day)"));
}

TEST(resolve_datetime_takes_a_range_end_given_alone_from_the_start) {
  const nlu::DateTimeValue now{2026, 10, 21, 14, 20};  // a Wednesday
  // A weekday, day of the month or month alone is the next one on or after
  // the start, not after now.
  CHECK_EQ(resolved("between monday and friday", now),
           std::string("2026-10-26 ~ 2026-10-30 (day)"));
  CHECK_EQ(resolved("다음 주 월요일부터 금요일까지", now),
           std::string("2026-10-26 ~ 2026-10-30 (day)"));
  CHECK_EQ(resolved("금요일부터 월요일까지", now), std::string("2026-10-23 ~ 2026-10-26 (day)"));
  CHECK_EQ(resolved("3월 5일부터 10일까지", now), std::string("2027-03-05 ~ 2027-03-10 (day)"));
  CHECK_EQ(resolved("12월 25일부터 1월 5일까지", now),
           std::string("2026-12-25 ~ 2027-01-05 (day)"));
  CHECK_EQ(resolved("3월부터 11월까지", now), std::string("2027-03-01 ~ 2027-11-01 (month)"));
  // An end with offsets of its own counts from now.
  CHECK_EQ(resolved("오늘부터 다음 주 금요일까지", now),
           std::string("2026-10-21 ~ 2026-10-30 (day)"));
  // A time alone before the start's is on the next day.
  CHECK_EQ(resolved("밤 11시부터 새벽 1시까지", now),
           std::string("2026-10-21 23:00 ~ 2026-10-22 01:00 (hour)"));
  CHECK_EQ(resolved("밤 11시부터 1시까지", now),
           std::string("2026-10-21 23:00 ~ 2026-10-22 01:00 (hour)"));
  CHECK_EQ(resolved("밤 11시부터 12시까지", now),
           std::string("2026-10-21 23:00 ~ 2026-10-22 00:00 (hour)"));
  CHECK_EQ(resolved("내일 밤 11시부터 새벽 1시까지", now),
           std::string("2026-10-22 23:00 ~ 2026-10-23 01:00 (hour)"));
  CHECK_EQ(resolved("오전 11시부터 1시까지", now),
           std::string("2026-10-21 11:00 ~ 2026-10-21 13:00 (hour)"));
  CHECK_EQ(resolved("오늘 밤 11시부터 내일 새벽 1시까지", now),
           std::string("2026-10-21 23:00 ~ 2026-10-22 01:00 (hour)"));
  // A bare hour, with no part of the day of its own or the start's, is 12
  // hours later on the start's day before it is on the next day. Its
  // attribute stays as written.
  CHECK_EQ(resolved("11시부터 1시까지", now),
           std::string("2026-10-21 11:00 ~ 2026-10-21 13:00 (hour)"));
  CHECK_EQ(resolved("10시부터 9시까지", now),
           std::string("2026-10-21 10:00 ~ 2026-10-21 21:00 (hour)"));
  CHECK_EQ(attributes("11시부터 1시까지"), std::string("hour=11 end_hour=1"));
  CHECK_EQ(resolved("3시부터 5시까지", now),
           std::string("2026-10-21 03:00 ~ 2026-10-21 05:00 (hour)"));
  // An end with a date, earlier on the start's day, moves by the unit it
  // left out.
  CHECK_EQ(resolved("금요일 오후 3시부터 금요일 오후 1시까지", now),
           std::string("2026-10-23 15:00 ~ 2026-10-30 13:00 (hour)"));
}

TEST(resolve_datetime_takes_the_next_month_or_year_with_the_day_given_alone) {
  // A day of the month alone is the next one, today included, in a month
  // that has it.
  CHECK_EQ(resolved("31일", {2026, 11, 5, -1, -1}), std::string("2026-12-31 (day)"));
  CHECK_EQ(resolved("the 31st", {2026, 11, 5, -1, -1}), std::string("2026-12-31 (day)"));
  CHECK_EQ(resolved("30일", {2026, 1, 31, -1, -1}), std::string("2026-03-30 (day)"));
  CHECK_EQ(resolved("29일", {2028, 1, 30, -1, -1}), std::string("2028-02-29 (day)"));
  CHECK_EQ(resolved("31일", {2026, 10, 31, -1, -1}), std::string("2026-10-31 (day)"));
  // A month and day alone: the next year that has the day.
  CHECK_EQ(resolved("2월 29일", {2026, 10, 21, -1, -1}), std::string("2028-02-29 (day)"));
  CHECK_EQ(resolved("2월 29일", {2028, 2, 29, -1, -1}), std::string("2028-02-29 (day)"));
  CHECK_EQ(resolved("2월 29일", {2097, 3, 1, -1, -1}), std::string("2104-02-29 (day)"));
  // A month given by an offset or with its year keeps its own days.
  CHECK_EQ(resolved("다음 달 31일", {2026, 10, 21, -1, -1}), std::string("2026-11-30 (day)"));
  CHECK_EQ(resolved("2026년 2월 29일", {2026, 10, 21, -1, -1}),
           std::string("2026-02-28 (day)"));
  // No year has 2월 30일: it is the month's last day.
  CHECK_EQ(resolved("2월 30일", {2026, 10, 21, -1, -1}), std::string("2027-02-28 (day)"));
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

TEST(resolve_datetime_stays_within_years_1_to_9999) {
  auto start = [](const std::vector<std::pair<std::string, std::string>>& attributes,
                  const nlu::DateTimeValue& now) {
    const nlu::DateTimeValue v = nlu::resolve_datetime(attributes, now).start;
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02d", v.year, v.month, v.day);
    std::string out = buffer;
    if (v.hour >= 0) {
      std::snprintf(buffer, sizeof buffer, " %02d:%02d", v.hour, v.minute);
      out += buffer;
    }
    return out;
  };
  const nlu::DateTimeValue now{2026, 10, 21, 14, 20};
  // Offsets the README allows (up to 100,000 either way) reach past both ends.
  CHECK_EQ(start({{"month_offset", "-30000"}}, now), std::string("0001-01-01"));
  CHECK_EQ(start({{"month_offset", "+100000"}}, now), std::string("9999-12-31"));
  CHECK_EQ(start({{"year_offset", "-100000"}}, now), std::string("0001-01-01"));
  CHECK_EQ(start({{"day_offset", "-1"}}, {1, 1, 1, -1, -1}), std::string("0001-01-01"));
  CHECK_EQ(start({{"day_offset", "+1"}}, {9999, 12, 31, -1, -1}), std::string("9999-12-31"));
  // A time beyond either end is that end's first or last minute.
  CHECK_EQ(start({{"day_offset", "-1"}, {"hour", "15"}}, {1, 1, 1, -1, -1}),
           std::string("0001-01-01 00:00"));
  CHECK_EQ(start({{"hour_offset", "+1"}}, {9999, 12, 31, 23, 30}),
           std::string("9999-12-31 23:59"));
  // The reference year is clamped into the same range.
  CHECK_EQ(start({{"day_offset", "0"}}, {0, 5, 5, -1, -1}), std::string("0001-05-05"));
}

TEST(resolve_datetime_clamps_the_reference_time) {
  nlu::DateTimeValue v = nlu::resolve_datetime({{"day_offset", "+1"}},
                                               nlu::DateTimeValue{2026, 13, 40, 99, 99}).start;
  CHECK_EQ(std::to_string(v.year) + "-" + std::to_string(v.month) + "-" + std::to_string(v.day),
           std::string("2027-1-1"));
}

// Behavior of the DATETIME system entity and its normalized attributes.
#include <string>
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

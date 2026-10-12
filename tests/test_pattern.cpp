// Behavior of the pattern language, mostly taken from the language guide.
#include "harness.hpp"
#include "nlu_script/nlu_script.hpp"

namespace {

// Compiles `definitions` followed by a single rule labelled "t".
nlu::Script rule(const std::string& pattern, const std::string& definitions = "") {
  return nlu::Script::compile(definitions + "t\n    " + pattern + "\n");
}

bool matches(const nlu::Script& script, const std::string& text) {
  return !script.match(text).empty();
}

bool matches(const std::string& pattern, const std::string& text) {
  return matches(rule(pattern), text);
}

}  // namespace

TEST(space_in_pattern_is_optional_in_input) {
  CHECK(matches("안녕 하세요", "안녕하세요"));
  CHECK(matches("안녕 하세요", "안녕 하세요"));
}

TEST(no_space_in_pattern_forbids_space_in_input) {
  CHECK(matches("안녕하세요", "안녕하세요"));
  CHECK(!matches("안녕하세요", "안녕 하세요"));
}

TEST(required_group_matches_every_combination) {
  nlu::Script s = rule("[주문|배달|배송] [해줘|해주세요|해줘요]");
  for (const char* verb : {"주문", "배달", "배송"})
    for (const char* ending : {"해줘", "해주세요", "해줘요"})
      CHECK(matches(s, std::string(verb) + ending));
  CHECK(!matches(s, "주문"));
  CHECK(!matches(s, "해줘"));
}

TEST(optional_group_may_be_omitted) {
  nlu::Script s = rule("안녕(하세요|하신가|하냐)");
  for (const char* text : {"안녕", "안녕하세요", "안녕하신가", "안녕하냐"})
    CHECK(matches(s, text));
  CHECK(!matches(s, "안녕하십니까"));
}

TEST(groups_nest) {
  nlu::Script s = rule("[불 (좀)|조명] [꺼(줘)|켜(줘)]");
  CHECK(matches(s, "불 좀 꺼줘"));
  CHECK(matches(s, "불 켜"));
  CHECK(matches(s, "조명 꺼줘"));
  CHECK(!matches(s, "조명 좀 꺼줘"));
}

TEST(adjacent_group_allows_space) {
  nlu::Script s = rule("주[라|세요]");
  for (const char* text : {"주라", "주 라", "주세요", "주 세요"}) CHECK(matches(s, text));
  CHECK(matches("[칼로리|열량]알려줘", "칼로리 알려줘"));
}

TEST(double_colon_joins_tokens_without_space) {
  nlu::Script s = rule("주::[라|세요]");
  CHECK(matches(s, "주라"));
  CHECK(matches(s, "주세요"));
  CHECK(!matches(s, "주 라"));
  CHECK(!matches(s, "주 세요"));
}

TEST(double_colon_keeps_particle_attached) {
  nlu::Script loose = rule("[아버지|어머니]가 방에 들어가신다");
  CHECK(matches(loose, "아버지가 방에 들어가신다"));
  CHECK(matches(loose, "아버지 가방에 들어가신다"));
  nlu::Script strict = rule("[아버지|어머니]::가 방에 들어가신다");
  CHECK(matches(strict, "아버지가 방에 들어가신다"));
  CHECK(!matches(strict, "아버지 가방에 들어가신다"));
}

TEST(double_colon_must_not_touch_spaces) {
  CHECK_THROWS(nlu::CompileError, rule("주 ::[라|세요]"));
  CHECK_THROWS(nlu::CompileError, rule("주:: [라|세요]"));
}

TEST(quoted_special_symbol_is_literal) {
  nlu::Script s = rule("잘지내(니|냐)\"?\"");
  for (const char* text : {"잘지내?", "잘지내니?", "잘지내냐?"}) CHECK(matches(s, text));
  CHECK(!matches(s, "잘지내니"));
  CHECK(matches("1\"+\"1", "1+1"));
}

TEST(unquoted_special_symbol_is_an_error) {
  for (const char* pattern : {"a.b", "a&b", "a-b", "a/b", "a=b", "a+b", "a*b", "a:b"})
    CHECK_THROWS(nlu::CompileError, rule(pattern));
}

TEST(unbalanced_brackets_are_errors) {
  for (const char* pattern : {"[a|b", "(a", "a]", "a)", "~[a, b]", "\"abc"})
    CHECK_THROWS(nlu::CompileError, rule(pattern));
}

TEST(permutation_covers_every_order) {
  nlu::Script s = rule("~[계정, 생성]~방법");
  CHECK(matches(s, "계정 생성 방법"));
  CHECK(matches(s, "생성 계정 방법"));
  CHECK(!matches(s, "계정 방법"));
  nlu::Script three = rule("~[a, b, [c|d]]~");
  CHECK(matches(three, "b d a"));
  CHECK(matches(three, "c a b"));
  CHECK(!matches(three, "a b"));
}

TEST(permutation_is_limited_to_five_tokens) {
  CHECK(matches("~[a, b, c, d, e]~", "e d c b a"));
  CHECK_THROWS(nlu::CompileError, rule("~[a, b, c, d, e, f]~"));
}

TEST(repetition_with_range) {
  nlu::Script s = rule("[빨리]^(1,3)");
  CHECK(matches(s, "빨리"));
  CHECK(matches(s, "빨리빨리"));
  CHECK(matches(s, "빨리빨리빨리"));
  CHECK(!matches(s, "빨리빨리빨리빨리"));
}

TEST(repetition_below) {
  nlu::Script s = rule("[빨리]^<3");
  CHECK(matches(s, "빨리"));
  CHECK(matches(s, "빨리빨리"));
  CHECK(!matches(s, "빨리빨리빨리"));
}

TEST(repetition_above) {
  nlu::Script s = rule("[빨리]^>2");
  CHECK(!matches(s, "빨리"));
  CHECK(!matches(s, "빨리빨리"));
  CHECK(matches(s, "빨리빨리빨리"));
  CHECK(matches(s, "빨리빨리빨리빨리빨리빨리"));
}

TEST(repetition_rejects_empty_ranges) {
  CHECK_THROWS(nlu::CompileError, rule("[a]^(3,2)"));
  CHECK_THROWS(nlu::CompileError, rule("[a]^<1"));
}

TEST(single_wildcard_matches_one_character) {
  nlu::Script s = rule("<?> 길동");
  CHECK(matches(s, "홍길동"));
  CHECK(!matches(s, "홍홍길동"));
  CHECK(!matches(s, "길동"));
}

TEST(star_wildcard_matches_anything_including_nothing) {
  nlu::Script s = rule("[안녕] <?>*?");
  CHECK(matches(s, "안녕 철수야"));
  CHECK(matches(s, "안녕 철수야 반가워"));
  CHECK(matches(s, "안녕"));
}

TEST(plus_wildcard_needs_at_least_one_character) {
  nlu::Script s = rule("[안녕] <?>+?");
  CHECK(matches(s, "안녕 철수야"));
  CHECK(!matches(s, "안녕"));
}

TEST(exclusion_rejects_wildcard_text_equal_to_excluded_tokens) {
  nlu::Script s = rule("\\[김철수|홍길동] <?>*? [슬픈|좋은|즐거운] 노래 틀어줘");
  CHECK(matches(s, "아이유 슬픈 노래 틀어줘"));
  CHECK(matches(s, "아이유나 이지은 슬픈 노래 틀어줘"));
  CHECK(matches(s, "지금 아이유 슬픈 노래 틀어줘"));
  CHECK(matches(s, "아이유나 홍길동 슬픈 노래 틀어줘"));
  CHECK(matches(s, "홍길동이나 아이유 슬픈 노래 틀어줘"));
  CHECK(!matches(s, "홍길동 슬픈 노래 틀어줘"));
  CHECK(!matches(s, "김철수 슬픈 노래 틀어줘"));
}

TEST(exclusion_with_wildcards_inside) {
  nlu::Script s = rule("\\[<?>*? 김철수|<?>*? 홍길동] <?>*? [슬픈|좋은|즐거운] 노래 틀어줘");
  CHECK(matches(s, "아이유 슬픈 노래 틀어줘"));
  CHECK(matches(s, "아이유나 이지은 슬픈 노래 틀어줘"));
  CHECK(matches(s, "지금 아이유 슬픈 노래 틀어줘"));
  CHECK(!matches(s, "아이유나 홍길동 슬픈 노래 틀어줘"));
  CHECK(!matches(s, "홍길동 슬픈 노래 틀어줘"));
  CHECK(!matches(s, "김철수 슬픈 노래 틀어줘"));
}

TEST(exclusion_must_precede_a_wildcard) {
  CHECK_THROWS(nlu::CompileError, rule("[\\김철수|\\홍길동] <?>*? [슬픈|좋은|즐거운] 노래 틀어줘"));
  CHECK_THROWS(nlu::CompileError, rule("<?>*? [\\김철수|\\홍길동] [슬픈|좋은|즐거운] 노래 틀어줘"));
}

TEST(domain_entity_expands_to_its_values) {
  nlu::Script s = rule("@{메뉴}[주문|배달]", "@메뉴: 치킨, 피자\n");
  for (const char* text : {"치킨 주문", "치킨 배달", "피자 주문", "피자 배달"}) CHECK(matches(s, text));
  CHECK(!matches(s, "햄버거 주문"));
}

TEST(entity_values_may_be_listed_on_indented_lines) {
  nlu::Script s = rule("@{메뉴} 주문", "@메뉴:\n    치킨\n    [피자|핏자]\n");
  CHECK(matches(s, "치킨 주문"));
  CHECK(matches(s, "핏자 주문"));
}

TEST(variable_is_inlined) {
  nlu::Script s = rule("@var.{숫자} @var.{숫자} [세|살] (입니다)",
                       "@var.숫자: [1|2|3|4|5|6|7|8|9]\n");
  CHECK(matches(s, "11세 입니다"));
  CHECK(matches(s, "23살 입니다"));
  CHECK(matches(s, "23살"));
  CHECK(!matches(s, "203살"));
}

TEST(system_entity_telnum) {
  nlu::Script s = rule("`@{TELNUM}[조회|확인]해줘");
  CHECK(matches(s, "010-0000-0000 조회해줘"));
  CHECK(matches(s, "010-1234-5678 확인해줘"));
  CHECK(matches(s, "02-123-4567 조회해줘"));
  CHECK(!matches(s, "010-12-5678 조회해줘"));
}

TEST(system_entity_number) {
  nlu::Script s = rule("`@{NUMBER} 개");
  CHECK(matches(s, "12개"));
  CHECK(matches(s, "3 개"));
  CHECK(!matches(s, "1 2개"));
}

TEST(undefined_references_are_errors) {
  CHECK_THROWS(nlu::CompileError, rule("@{없음} 주문"));
  CHECK_THROWS(nlu::CompileError, rule("@var.{없음} 주문"));
  CHECK_THROWS(nlu::CompileError, rule("`@{NO_SUCH_ENTITY}"));
}

TEST(recursive_variables_are_errors) {
  CHECK_THROWS(nlu::CompileError, rule("@var.{a}", "@var.a: x @var.{b}\n@var.b: y (@var.{a})\n"));
}

TEST(ascii_letters_match_case_insensitively) {
  CHECK(matches("turn up (the) volume", "Turn up the VOLUME"));
  CHECK(matches("Turn Up Volume", "turn up volume"));
}

TEST(whitespace_in_input_is_normalized) {
  CHECK(matches("turn up volume", "  turn \t up\n volume  "));
  CHECK(matches("안녕 하세요", "안녕　하세요"));
}

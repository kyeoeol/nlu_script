// Behavior of the pattern language, checked with Korean and English input.
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
  CHECK(matches("잔액 조회", "잔액조회"));
  CHECK(matches("잔액 조회", "잔액 조회"));
  CHECK(matches("log in", "login"));
  CHECK(matches("log in", "log in"));
}

TEST(no_space_in_pattern_forbids_space_in_input) {
  CHECK(matches("잔액조회", "잔액조회"));
  CHECK(!matches("잔액조회", "잔액 조회"));
  CHECK(matches("login", "login"));
  CHECK(!matches("login", "log in"));
}

TEST(required_group_matches_every_combination) {
  nlu::Script ko = rule("[송금|이체|결제] [취소해줘|취소해주세요|취소하고 싶어요]");
  for (const char* verb : {"송금", "이체", "결제"})
    for (const char* ending : {"취소해줘", "취소해주세요", "취소하고 싶어요"})
      CHECK(matches(ko, std::string(verb) + ending));
  CHECK(!matches(ko, "송금"));
  CHECK(!matches(ko, "취소해주세요"));

  nlu::Script en = rule("[cancel|refund] [my order|my payment]");
  for (const char* verb : {"cancel", "refund"})
    for (const char* object : {"my order", "my payment"})
      CHECK(matches(en, std::string(verb) + " " + object));
  CHECK(!matches(en, "cancel"));
  CHECK(!matches(en, "my payment"));
}

TEST(optional_group_may_be_omitted) {
  nlu::Script ko = rule("환불(해줘|해주세요)");
  for (const char* text : {"환불", "환불해줘", "환불해주세요"}) CHECK(matches(ko, text));
  CHECK(!matches(ko, "환불했어요"));

  nlu::Script en = rule("check my balance (please|now)");
  for (const char* text : {"check my balance", "check my balance please", "check my balance now"})
    CHECK(matches(en, text));
  CHECK(!matches(en, "check my balance later"));
}

TEST(groups_nest) {
  nlu::Script ko = rule("[카드 (좀)|계좌] [정지(해줘)|해지(해줘)]");
  CHECK(matches(ko, "카드 좀 정지해줘"));
  CHECK(matches(ko, "카드 해지"));
  CHECK(matches(ko, "계좌 정지해줘"));
  CHECK(!matches(ko, "계좌 좀 정지해줘"));

  nlu::Script en = rule("[freeze|unfreeze] [(my) card|my account]");
  CHECK(matches(en, "freeze my card"));
  CHECK(matches(en, "unfreeze card"));
  CHECK(matches(en, "freeze my account"));
  CHECK(!matches(en, "freeze account"));
}

TEST(adjacent_group_allows_space) {
  nlu::Script ko = rule("환불[해줘|해주세요]");
  for (const char* text : {"환불해줘", "환불 해줘", "환불해주세요", "환불 해주세요"})
    CHECK(matches(ko, text));
  CHECK(matches("[잔액|한도]알려줘", "잔액 알려줘"));

  nlu::Script en = rule("sign[in|up]");
  for (const char* text : {"signin", "sign in", "signup", "sign up"}) CHECK(matches(en, text));
  CHECK(matches("[check|show]balance", "check balance"));
}

TEST(double_colon_joins_tokens_without_space) {
  nlu::Script ko = rule("환불::[해줘|해주세요]");
  CHECK(matches(ko, "환불해줘"));
  CHECK(matches(ko, "환불해주세요"));
  CHECK(!matches(ko, "환불 해줘"));
  CHECK(!matches(ko, "환불 해주세요"));

  nlu::Script en = rule("[account|card]::(s)");
  CHECK(matches(en, "account"));
  CHECK(matches(en, "cards"));
  CHECK(!matches(en, "card s"));
}

TEST(double_colon_keeps_particle_attached) {
  // 계좌가 압류됐어요 (account seized) vs. 계좌 가압류됐어요 (account provisionally seized).
  nlu::Script loose = rule("[계좌|카드]가 압류됐어요");
  CHECK(matches(loose, "계좌가 압류됐어요"));
  CHECK(matches(loose, "계좌 가압류됐어요"));
  nlu::Script strict = rule("[계좌|카드]::가 압류됐어요");
  CHECK(matches(strict, "계좌가 압류됐어요"));
  CHECK(!matches(strict, "계좌 가압류됐어요"));

  // The noun "login" vs. the verb phrase "log in".
  nlu::Script loose_en = rule("[log|sign]in page is down");
  CHECK(matches(loose_en, "login page is down"));
  CHECK(matches(loose_en, "log in page is down"));
  nlu::Script strict_en = rule("[log|sign]::in page is down");
  CHECK(matches(strict_en, "login page is down"));
  CHECK(!matches(strict_en, "log in page is down"));
}

TEST(double_colon_must_not_touch_spaces) {
  CHECK_THROWS(nlu::CompileError, rule("환불 ::[해줘|해주세요]"));
  CHECK_THROWS(nlu::CompileError, rule("sign:: [in|up]"));
}

TEST(quoted_special_symbol_is_literal) {
  nlu::Script ko = rule("송금 됐나요\"?\"");
  for (const char* text : {"송금됐나요?", "송금 됐나요?"}) CHECK(matches(ko, text));
  CHECK(!matches(ko, "송금됐나요"));

  nlu::Script en = rule("is my card blocked\"?\"");
  CHECK(matches(en, "is my card blocked?"));
  CHECK(!matches(en, "is my card blocked"));
  CHECK(matches("pre\"-\"approval status", "pre-approval status"));
  CHECK(matches("0\".\"5% 캐시백", "0.5% 캐시백"));
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
  nlu::Script ko = rule("~[카드, 재발급]~ 신청");
  CHECK(matches(ko, "카드 재발급 신청"));
  CHECK(matches(ko, "재발급 카드 신청"));
  CHECK(!matches(ko, "카드 신청"));

  nlu::Script en = rule("~[reset, password]~");
  CHECK(matches(en, "reset password"));
  CHECK(matches(en, "password reset"));
  CHECK(!matches(en, "reset"));

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
  nlu::Script ko = rule("[ㅠ]^(1,3)");
  CHECK(matches(ko, "ㅠ"));
  CHECK(matches(ko, "ㅠㅠ"));
  CHECK(matches(ko, "ㅠㅠㅠ"));
  CHECK(!matches(ko, "ㅠㅠㅠㅠ"));

  nlu::Script en = rule("[help]^(1,3)");
  CHECK(matches(en, "help"));
  CHECK(matches(en, "help help"));
  CHECK(matches(en, "help help help"));
  CHECK(!matches(en, "help help help help"));
}

TEST(repetition_below) {
  nlu::Script ko = rule("[정말]^<3 급해요");
  CHECK(matches(ko, "정말 급해요"));
  CHECK(matches(ko, "정말 정말 급해요"));
  CHECK(!matches(ko, "정말 정말 정말 급해요"));

  nlu::Script en = rule("[very]^<3 urgent");
  CHECK(matches(en, "very urgent"));
  CHECK(matches(en, "very very urgent"));
  CHECK(!matches(en, "very very very urgent"));
}

TEST(repetition_above) {
  nlu::Script ko = rule("[제발]^>2");
  CHECK(!matches(ko, "제발"));
  CHECK(!matches(ko, "제발 제발"));
  CHECK(matches(ko, "제발 제발 제발"));
  CHECK(matches(ko, "제발제발제발제발"));

  nlu::Script en = rule("[please]^>2");
  CHECK(!matches(en, "please please"));
  CHECK(matches(en, "please please please"));
  CHECK(matches(en, "pleasepleasepleaseplease"));
}

TEST(oversized_rules_are_errors) {
  // Each level multiplies the automaton by 100; compiling stops early with an
  // error instead of running out of memory.
  CHECK_THROWS(nlu::CompileError, rule("[[[[a]^(1,100)]^(1,100)]^(1,100)]^(1,100)"));
  CHECK(matches("[[a]^(1,100)]^(1,100)", "aaaa"));
}

TEST(repetition_rejects_empty_ranges) {
  CHECK_THROWS(nlu::CompileError, rule("[a]^(3,2)"));
  CHECK_THROWS(nlu::CompileError, rule("[a]^<1"));
}

TEST(single_wildcard_matches_one_character) {
  nlu::Script ko = rule("<?>월 명세서");
  CHECK(matches(ko, "3월 명세서"));
  CHECK(!matches(ko, "12월 명세서"));
  CHECK(!matches(ko, "월 명세서"));

  nlu::Script en = rule("credit grade <?>");
  CHECK(matches(en, "credit grade A"));
  CHECK(!matches(en, "credit grade AA"));
  CHECK(!matches(en, "credit grade"));
}

TEST(star_wildcard_matches_anything_including_nothing) {
  nlu::Script ko = rule("[문의] <?>*?");
  CHECK(matches(ko, "문의 결제 오류가 나요"));
  CHECK(matches(ko, "문의"));

  nlu::Script en = rule("[complaint] <?>*?");
  CHECK(matches(en, "complaint the app keeps crashing"));
  CHECK(matches(en, "complaint"));
}

TEST(plus_wildcard_needs_at_least_one_character) {
  nlu::Script ko = rule("[문의] <?>+?");
  CHECK(matches(ko, "문의 결제 오류가 나요"));
  CHECK(!matches(ko, "문의"));

  nlu::Script en = rule("[complaint] <?>+?");
  CHECK(matches(en, "complaint the app keeps crashing"));
  CHECK(!matches(en, "complaint"));
}

TEST(exclusion_rejects_wildcard_text_equal_to_excluded_tokens) {
  nlu::Script ko = rule("\\[적금|예금] <?>+? 해지");
  CHECK(matches(ko, "청약통장 해지"));
  CHECK(matches(ko, "청약통장이랑 적금 해지"));
  CHECK(matches(ko, "적금이랑 청약통장 해지"));
  CHECK(!matches(ko, "적금 해지"));
  CHECK(!matches(ko, "예금 해지"));

  nlu::Script en = rule("close my \\[savings|checking] <?>+? account");
  CHECK(matches(en, "close my brokerage account"));
  CHECK(matches(en, "close my joint savings account"));
  CHECK(matches(en, "close my savings and brokerage account"));
  CHECK(!matches(en, "close my savings account"));
  CHECK(!matches(en, "close my Checking account"));
}

TEST(exclusion_with_wildcards_inside) {
  nlu::Script ko = rule("\\[<?>*? 적금|<?>*? 예금] <?>+? 해지");
  CHECK(matches(ko, "청약통장 해지"));
  CHECK(!matches(ko, "적금 해지"));
  CHECK(!matches(ko, "청약통장이랑 적금 해지"));
  // The exclusion must match the whole wildcard text, which here does not end with 적금.
  CHECK(matches(ko, "적금이랑 청약통장 해지"));

  nlu::Script en = rule("close my \\[<?>*? savings|<?>*? checking] <?>+? account");
  CHECK(matches(en, "close my brokerage account"));
  CHECK(!matches(en, "close my savings account"));
  CHECK(!matches(en, "close my joint savings account"));
  CHECK(matches(en, "close my savings and brokerage account"));
}

TEST(exclusion_surrounded_by_wildcards_rejects_text_containing_it) {
  nlu::Script ko = rule("\\[<?>*? 적금 <?>*?|<?>*? 예금 <?>*?] <?>+? 해지");
  CHECK(matches(ko, "청약통장 해지"));
  CHECK(!matches(ko, "청약통장이랑 적금 해지"));
  CHECK(!matches(ko, "적금이랑 청약통장 해지"));
  CHECK(!matches(ko, "정기예금 상품 해지"));

  nlu::Script en = rule("close my \\[<?>*? savings <?>*?|<?>*? checking <?>*?] <?>+? account");
  CHECK(matches(en, "close my brokerage account"));
  CHECK(!matches(en, "close my joint savings account"));
  CHECK(!matches(en, "close my savings and brokerage account"));
  CHECK(!matches(en, "close my high yield checking account"));
}

TEST(exclusion_must_precede_a_wildcard) {
  CHECK_THROWS(nlu::CompileError, rule("[\\적금|\\예금] <?>+? 해지"));
  CHECK_THROWS(nlu::CompileError, rule("<?>+? [\\적금|\\예금] 해지"));
  CHECK_THROWS(nlu::CompileError, rule("close my [\\savings|\\checking] <?>+? account"));
}

TEST(domain_entity_expands_to_its_values) {
  const std::string cards = "@card: [credit card|신용카드], [debit card|체크카드]\n";
  nlu::Script ko = rule("@{card} [분실|재발급]", cards);
  for (const char* text : {"신용카드 분실", "신용카드 재발급", "체크카드 분실", "체크카드 재발급"})
    CHECK(matches(ko, text));
  CHECK(!matches(ko, "교통카드 분실"));

  nlu::Script en = rule("[lost|replace] (my|a) @{card}", cards);
  for (const char* text : {"lost my credit card", "replace a debit card", "lost debit card"})
    CHECK(matches(en, text));
  CHECK(!matches(en, "lost my gift card"));
}

TEST(entity_values_may_be_listed_on_indented_lines) {
  const std::string cards = "@card:\n    [credit card|신용카드]\n    [debit card|체크카드|직불카드]\n";
  nlu::Script s = rule("[@{card} 재발급|reissue (my) @{card}]", cards);
  CHECK(matches(s, "직불카드 재발급"));
  CHECK(matches(s, "reissue my debit card"));
  CHECK(matches(s, "reissue credit card"));
}

TEST(variable_is_inlined) {
  const std::string digit = "@var.digit: [0|1|2|3|4|5|6|7|8|9]\n";
  const std::string last4 = "@var.{digit}@var.{digit}@var.{digit}@var.{digit}";
  nlu::Script ko = rule("끝자리 " + last4 + " (카드)", digit);
  CHECK(matches(ko, "끝자리 4821"));
  CHECK(matches(ko, "끝자리 4821 카드"));
  CHECK(!matches(ko, "끝자리 48210"));

  nlu::Script en = rule("(my) card ending in " + last4, digit);
  CHECK(matches(en, "card ending in 4821"));
  CHECK(matches(en, "my card ending in 4821"));
  CHECK(!matches(en, "card ending in 482"));
}

TEST(system_entity_telnum) {
  nlu::Script ko = rule("휴대폰 번호 `@{TELNUM}::(으로|로) 변경해주세요");
  CHECK(matches(ko, "휴대폰 번호 010-9876-5432로 변경해주세요"));
  CHECK(matches(ko, "휴대폰 번호 01098765432로 변경해주세요"));
  CHECK(!matches(ko, "휴대폰 번호 010-98-5432로 변경해주세요"));

  nlu::Script en = rule("change my phone number to `@{TELNUM}");
  CHECK(matches(en, "change my phone number to 010 9876 5432"));
  CHECK(matches(en, "change my phone number to 031-765-4321"));
  CHECK(!matches(en, "change my phone number to 911"));
}

TEST(system_entity_number) {
  nlu::Script ko = rule("`@{NUMBER} 개월 할부");
  CHECK(matches(ko, "3개월 할부"));
  CHECK(matches(ko, "12 개월 할부"));
  CHECK(!matches(ko, "1 2개월 할부"));

  nlu::Script en = rule("split into `@{NUMBER} (monthly) payments");
  CHECK(matches(en, "split into 6 payments"));
  CHECK(matches(en, "split into 12 monthly payments"));
  CHECK(!matches(en, "split into six payments"));
}

TEST(undefined_references_are_errors) {
  CHECK_THROWS(nlu::CompileError, rule("@{없음} 해지"));
  CHECK_THROWS(nlu::CompileError, rule("@var.{missing} transfer"));
  CHECK_THROWS(nlu::CompileError, rule("`@{NO_SUCH_ENTITY}"));
}

TEST(recursive_variables_are_errors) {
  CHECK_THROWS(nlu::CompileError, rule("@var.{a}", "@var.a: x @var.{b}\n@var.b: y (@var.{a})\n"));
}

TEST(recursive_entities_are_errors) {
  // Found by fuzzing: these used to recurse until the stack overflowed.
  CHECK_THROWS(nlu::CompileError, rule("@{a}", "@a: x @{a}\n"));
  CHECK_THROWS(nlu::CompileError, rule("@{a}", "@a: \\[@{a}] <?>*?\n"));
  CHECK_THROWS(nlu::CompileError, rule("@{a}", "@a: @{b}\n@b: (@{a})\n"));
  CHECK_THROWS(nlu::CompileError, rule("@{a}", "@a: @var.{b}\n@var.b: [@{a}|c]\n"));
  // Using an entity twice, or inside another one, is fine.
  CHECK(matches(rule("@{b} @{b}", "@a: x\n@b: @{a} y\n"), "x y x y"));
}

TEST(deeply_nested_groups_are_errors) {
  CHECK_THROWS(nlu::CompileError, rule(std::string(300, '[') + "a" + std::string(300, ']')));
  CHECK(matches(std::string(50, '(') + "a" + std::string(50, ')'), "a"));
}

TEST(ascii_letters_match_case_insensitively) {
  CHECK(matches("reset (my) PIN", "Reset my pin"));
  CHECK(matches("Check My Balance", "check my balance"));
  CHECK(matches("[ATM|OTP] 오류", "otp 오류"));
}

TEST(whitespace_in_input_is_normalized) {
  CHECK(matches("check my balance", "  check \t my\n balance  "));
  CHECK(matches("잔액 조회", "잔액　조회"));
}

// Decomposed Hangul (NFD, as in text copied from macOS file names) is composed
// before matching, in input and in patterns alike.
TEST(decomposed_hangul_matches_composed_hangul) {
  const std::string decomposed = "카드 분실";
  CHECK(matches("카드 분실", decomposed));               // 카드 분실 in NFD
  CHECK(matches(decomposed, "카드 분실"));
  CHECK(matches("카드 분실", "카드 분실"));    // 부 + final ㄴ
  CHECK(matches("<?>드 분실", decomposed));              // one syllable is one character
  CHECK(!matches("카드 분실", "카드 부실"));  // 부실, not 분실
}

TEST(particle_pair_follows_the_final_consonant) {
  nlu::Script s = rule("[적금|예금|카드|계좌]을/를 해지");
  CHECK(matches(s, "적금을 해지"));
  CHECK(matches(s, "카드를 해지"));
  CHECK(matches(s, "계좌를해지"));
  CHECK(!matches(s, "적금를 해지"));
  CHECK(!matches(s, "카드을 해지"));
  CHECK(!matches(s, "적금 을 해지"));  // a particle stays on its word
  CHECK(!matches(s, "적금 해지"));
}

TEST(particle_pair_may_be_written_in_either_order) {
  nlu::Script s = rule("[적금|카드]는/은 어떻게 해지해요");
  CHECK(matches(s, "적금은 어떻게 해지해요"));
  CHECK(matches(s, "카드는 어떻게 해지해요"));
  CHECK(!matches(s, "카드은 어떻게 해지해요"));
}

TEST(particle_pairs_with_ro_treat_final_rieul_like_a_vowel) {
  nlu::Script s = rule("[서울|부산|대구] 지점으로/로 [가요|연결해주세요]");
  CHECK(matches(s, "서울 지점으로 가요"));
  CHECK(!matches(s, "서울 지점로 가요"));
  s = rule("[서울|부산|대구]으로/로 송금");
  CHECK(matches(s, "서울로 송금"));
  CHECK(!matches(s, "서울으로 송금"));
  CHECK(matches(s, "부산으로 송금"));
  CHECK(!matches(s, "부산로 송금"));
  CHECK(matches(s, "대구로 송금"));
}

TEST(particle_pair_follows_wildcards_and_entities) {
  nlu::Script s = rule("<?>+?을/를 해지해주세요");
  CHECK(matches(s, "청약통장을 해지해주세요"));
  CHECK(matches(s, "정기예금 상품을 해지해주세요"));
  CHECK(!matches(s, "청약통장를 해지해주세요"));
  s = rule("@{product}이/가 만기됐어요", "@product: [적금|정기예금|청약]\n");
  CHECK(matches(s, "적금이 만기됐어요"));
  CHECK(matches(s, "청약이 만기됐어요"));
  CHECK(!matches(s, "정기예금가 만기됐어요"));
}

TEST(particle_pair_after_other_characters_accepts_either_form) {
  // Whether `OTP` ends in a consonant depends on how it is read.
  nlu::Script s = rule("[OTP|ATM 카드]을/를 재발급");
  CHECK(matches(s, "OTP를 재발급"));
  CHECK(matches(s, "otp을 재발급"));
  CHECK(!matches(s, "ATM 카드을 재발급"));
}

TEST(optional_particle_pair) {
  nlu::Script s = rule("[적금|카드](을/를) 해지");
  CHECK(matches(s, "적금 해지"));
  CHECK(matches(s, "적금을 해지"));
  CHECK(matches(s, "카드를 해지"));
  CHECK(!matches(s, "적금를 해지"));
}

TEST(unknown_or_misplaced_particle_pairs_are_errors) {
  CHECK_THROWS(nlu::CompileError, rule("카드을/는 분실"));
  CHECK_THROWS(nlu::CompileError, rule("카드을/를해지"));
  CHECK_THROWS(nlu::CompileError, rule("카드 /를 분실"));
}

TEST(particle_pairs_work_inside_exclusions) {
  // The wildcard text may be anything but a word with its right particle.
  nlu::Script s = rule("\\[<?>+?을/를] <?>+? 해지");
  CHECK(!matches(s, "카드를 해지"));
  CHECK(!matches(s, "적금을 해지"));
  CHECK(matches(s, "카드을 해지"));
  CHECK(matches(s, "적금를 해지"));
  CHECK(matches(s, "OTP 해지"));
  CHECK(!matches(s, "OTP를 해지"));  // either form follows a non-Hangul character
}

TEST(letters_beyond_ascii_match_case_insensitively) {
  CHECK(matches("ärger mit der karte", "ÄRGER mit der Karte"));
  CHECK(matches("ΚΑΡΤΑ", "κάρτα") == false);  // accents still count
  CHECK(matches("κάρτα", "ΚΆΡΤΑ"));
  CHECK(matches("οδος", "ΟΔΟΣ"));             // final sigma folds like sigma
  CHECK(matches("привет", "ПРИВЕТ"));
}

TEST(canonically_equivalent_text_matches) {
  // NFC: precomposed and decomposed forms are the same text.
  CHECK(matches("café", "café"));
  CHECK(matches("café", "café"));
  CHECK(matches("Ångström", "Ångström"));  // ANGSTROM SIGN
  // Combining marks in either order.
  CHECK(matches("ậ", "ậ"));
  CHECK(matches("ậ", "ậ"));
}

TEST(typo_tolerant_groups_allow_one_edit) {
  nlu::Script s = rule("[I lost my card]&");
  CHECK(matches(s, "I lost my card"));
  CHECK(matches(s, "I lost my crd"));     // a letter left out
  CHECK(matches(s, "I lost my caard"));   // a letter added
  CHECK(matches(s, "I lost my cord"));    // a letter changed
  CHECK(!matches(s, "I lsot my card"));   // a swap is two edits
  CHECK(!matches(s, "I lst my crd"));
  CHECK(!matches("I lost my card", "I lost my crd"));  // only inside [ ]&

  nlu::Script ko = rule("[분실 신고]& (할게요)");
  CHECK(matches(ko, "분실 신구 할게요"));
  CHECK(matches(ko, "분실신고할게요"));
  CHECK(!matches(ko, "분실 신구 할께요"));  // the edit is used up inside the group
}

TEST(typo_matches_rank_below_exact_ones) {
  nlu::Script s = nlu::Script::compile(
      "card:lost\n    [I lost my card]&\nreport:any\n    I lost my <?>+?\n");
  std::vector<nlu::Match> best = s.match_best("I lost my card");
  CHECK_EQ(best.size(), 1u);
  if (best.size() == 1) CHECK_EQ(best[0].label, std::string("card:lost"));
  for (const nlu::Match& m : s.match("I lost my cord"))
    if (m.label == "card:lost") CHECK(m.specificity < 11);
}

// Behavior of scripts: labels, slots, attributes and compile errors.
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "harness.hpp"
#include "nlu_script/nlu_script.hpp"

namespace {

std::vector<std::string> labels(const nlu::Script& script, const std::string& text) {
  std::vector<std::string> out;
  for (const nlu::Match& m : script.match(text)) out.push_back(m.label);
  return out;
}

std::string tagged(const nlu::Script& script, const std::string& text) {
  std::vector<nlu::Match> found = script.match(text);
  return found.empty() ? "<no match>" : found.front().label + ":" + found.front().tagged;
}

std::string attribute(const nlu::Slot& slot, const std::string& key) {
  for (const auto& kv : slot.attributes)
    if (kv.first == key) return kv.second;
  return "<unset>";
}

}  // namespace

TEST(labels_are_reported_in_script_order) {
  nlu::Script s = nlu::Script::compile(
      "# comment line\n"
      "card:lost\n"
      "    report (a) lost card\n"
      "    I lost my card\n"
      "    카드 분실 신고 (할게요)\n"
      "\n"
      "report:any\n"
      "    report <?>+?\n"
      "\n"
      "agent:connect\n"
      "    talk to an agent\n"
      "    상담원 연결 (해주세요)\n");
  CHECK_EQ(labels(s, "Report a lost card"), (std::vector<std::string>{"card:lost", "report:any"}));
  CHECK_EQ(labels(s, "I lost my card"), (std::vector<std::string>{"card:lost"}));
  CHECK_EQ(labels(s, "report fraud"), (std::vector<std::string>{"report:any"}));
  CHECK_EQ(labels(s, "카드 분실 신고할게요"), (std::vector<std::string>{"card:lost"}));
  CHECK_EQ(labels(s, "talk to an agent"), (std::vector<std::string>{"agent:connect"}));
  CHECK_EQ(labels(s, "상담원 연결해주세요"), (std::vector<std::string>{"agent:connect"}));
  CHECK(labels(s, "thank you").empty());
  CHECK_EQ(s.rule_count(), 6u);
}

TEST(match_reports_the_line_of_the_rule_that_matched) {
  nlu::Script s = nlu::Script::compile(
      "# card rules\n"
      "card:lost\n"
      "    I lost my card\n"
      "    <?>+? my card\n"
      "\n"
      "agent:connect\n"
      "    talk to an agent\n"
      "card:lost\n"
      "    카드 분실\n");
  auto line_of = [&](const std::string& text) {
    std::vector<nlu::Match> found = s.match(text);
    return found.size() == 1 ? found[0].line : -1;
  };
  CHECK_EQ(line_of("I lost my card"), 3);  // lines 3 and 4 both match; the first wins
  CHECK_EQ(line_of("I found my card"), 4);
  CHECK_EQ(line_of("talk to an agent"), 7);
  CHECK_EQ(line_of("카드 분실"), 9);
}

TEST(label_without_slots_tags_plain_text) {
  nlu::Script s = nlu::Script::compile("card:lost\n    report (a) lost card\n");
  CHECK_EQ(tagged(s, "Report a lost card"), std::string("card:lost:Report a lost card"));
}

TEST(slot_captures_span_of_original_text) {
  nlu::Script s = nlu::Script::compile(
      "card:lost\n"
      "    @{카드} [분실|재발급] (신고)\n"
      "    I lost my @{카드}\n"
      "@카드: [신용카드|credit card], [체크카드|debit card]\n");
  std::vector<nlu::Match> found = s.match("체크카드 분실 신고");
  CHECK_EQ(found.size(), 1u);
  CHECK_EQ(found[0].slots.size(), 1u);
  CHECK_EQ(found[0].slots[0].name, std::string("카드"));
  CHECK_EQ(found[0].slots[0].text, std::string("체크카드"));
  CHECK_EQ(found[0].slots[0].begin, 0u);
  CHECK_EQ(found[0].slots[0].end, std::string("체크카드").size());
  CHECK_EQ(found[0].tagged, std::string("<카드>체크카드</카드> 분실 신고"));

  found = s.match("I lost my Debit Card");
  CHECK_EQ(found.size(), 1u);
  CHECK_EQ(found[0].slots[0].text, std::string("Debit Card"));
  CHECK_EQ(found[0].slots[0].begin, 10u);
  CHECK_EQ(found[0].slots[0].end, 20u);
  CHECK_EQ(found[0].tagged, std::string("I lost my <카드>Debit Card</카드>"));
}

TEST(slot_spans_decomposed_hangul_in_the_original_input) {
  nlu::Script s = nlu::Script::compile("card:lost\n    <card>[신용|체크]카드</card> 분실\n");
  const std::string card = "\u110e\u1166\u110f\u1173\u110f\u1161\u1103\u1173";  // 체크카드 in NFD
  const std::string input = card + " \u1107\u116e\u11ab\u1109\u1175\u11af";
  std::vector<nlu::Match> found = s.match(input);
  CHECK_EQ(found.size(), 1u);
  CHECK_EQ(found[0].slots[0].text, card);
  CHECK_EQ(found[0].slots[0].begin, 0u);
  CHECK_EQ(found[0].slots[0].end, card.size());
  CHECK_EQ(found[0].tagged, "<card>" + card + "</card>" + input.substr(card.size()));
}

TEST(slot_ends_before_a_particle_pair) {
  nlu::Script s = nlu::Script::compile(
      "card:lost\n    <card>[신용|체크]카드</card>을/를 분실했어요\n");
  CHECK_EQ(tagged(s, "체크카드를 분실했어요"),
           std::string("card:lost:<card>체크카드</card>를 분실했어요"));
}

TEST(slot_text_keeps_original_case_and_spacing) {
  nlu::Script s = nlu::Script::compile(
      "payment:cancel\n    cancel payment at <merchant><?>+?</merchant>\n");
  std::vector<nlu::Match> found = s.match("Cancel payment at  Corner   Bakery");
  CHECK_EQ(found.size(), 1u);
  CHECK_EQ(found[0].slots[0].text, std::string("Corner   Bakery"));
  CHECK_EQ(found[0].tagged, std::string("Cancel payment at  <merchant>Corner   Bakery</merchant>"));
}

TEST(wildcard_in_slot_is_lazy_and_optional_group_is_greedy) {
  nlu::Script s = nlu::Script::compile(
      "transfer\n    send money to <recipient><?>+?</recipient> (for <memo><?>+?</memo>)\n");
  CHECK_EQ(tagged(s, "send money to alex for rent"),
           std::string("transfer:send money to <recipient>alex</recipient> for <memo>rent</memo>"));
  CHECK_EQ(tagged(s, "send money to alex"),
           std::string("transfer:send money to <recipient>alex</recipient>"));
}

TEST(attributes_normalize_slot_values) {
  nlu::Script s = nlu::Script::compile(
      "card:lost\n"
      "    report a lost <card>([visa{brand=VISA}|mastercard{brand=MASTERCARD}])"
      " [credit{type=CREDIT}|debit{type=DEBIT}] card</card>\n"
      "    <card>([비자{brand=VISA}|마스터{brand=MASTERCARD}])"
      " [신용{type=CREDIT}|체크{type=DEBIT}]카드</card>::(를) 분실했어요\n");
  CHECK_EQ(tagged(s, "Report a lost Visa credit card"),
           std::string("card:lost:Report a lost <card brand=\"VISA\" type=\"CREDIT\">"
                       "Visa credit card</card>"));
  CHECK_EQ(tagged(s, "마스터 체크카드를 분실했어요"),
           std::string("card:lost:<card brand=\"MASTERCARD\" type=\"DEBIT\">"
                       "마스터 체크카드</card>를 분실했어요"));
  for (const char* text : {"report a lost debit card", "체크카드 분실했어요"}) {
    std::vector<nlu::Match> found = s.match(text);
    CHECK_EQ(found.size(), 1u);
    CHECK_EQ(attribute(found[0].slots[0], "type"), std::string("DEBIT"));
    CHECK_EQ(attribute(found[0].slots[0], "brand"), std::string("<unset>"));
  }
}

TEST(qualified_attribute_targets_named_slot_anywhere_in_rule) {
  nlu::Script s = nlu::Script::compile(
      "transfer:send\n"
      "    send <amount><?>+?</amount> [dollars{amount.currency=USD}|won{amount.currency=KRW}]"
      " to <recipient><?>+?</recipient>\n"
      "    <recipient><?>+?</recipient>::[한테|에게] <amount><?>+?</amount>"
      " [달러{amount.currency=USD}|원{amount.currency=KRW}] 보내줘\n");
  CHECK_EQ(tagged(s, "Send 50 dollars to Alex"),
           std::string("transfer:send:Send <amount currency=\"USD\">50</amount> dollars to "
                       "<recipient>Alex</recipient>"));
  CHECK_EQ(tagged(s, "엄마한테 3만 원 보내줘"),
           std::string("transfer:send:<recipient>엄마</recipient>한테 "
                       "<amount currency=\"KRW\">3만</amount> 원 보내줘"));
}

TEST(entity_values_can_carry_attributes) {
  nlu::Script s = nlu::Script::compile(
      "@currency: [dollar|달러]{code=USD}, [yen|엔화]{code=JPY}\n"
      "exchange:rate\n"
      "    @{currency} 환율 (알려줘)\n"
      "    @{currency} exchange rate\n");
  for (const auto& text_code : {std::make_pair("엔화 환율 알려줘", "JPY"),
                                std::make_pair("dollar exchange rate", "USD")}) {
    std::vector<nlu::Match> found = s.match(text_code.first);
    CHECK_EQ(found.size(), 1u);
    CHECK_EQ(found[0].slots[0].name, std::string("currency"));
    CHECK_EQ(attribute(found[0].slots[0], "code"), std::string(text_code.second));
  }
}

TEST(later_assignment_overrides_earlier_one) {
  nlu::Script s = nlu::Script::compile("t\n    <x>a{v=1} (b{v=2})</x>\n");
  CHECK_EQ(attribute(s.match("a")[0].slots[0], "v"), std::string("1"));
  CHECK_EQ(attribute(s.match("a b")[0].slots[0], "v"), std::string("2"));
}

TEST(repeated_slot_yields_one_slot_per_occurrence) {
  nlu::Script s = nlu::Script::compile("t\n    [<n>@var.{d}</n>]^(1,3)\n@var.d: [1|2|3]\n");
  std::vector<nlu::Match> found = s.match("1 2 3");
  CHECK_EQ(found.size(), 1u);
  CHECK_EQ(found[0].slots.size(), 3u);
  CHECK_EQ(found[0].tagged, std::string("<n>1</n> <n>2</n> <n>3</n>"));
}

TEST(nested_slots_are_rendered_nested) {
  nlu::Script s = nlu::Script::compile("t\n    <outer>a <inner>b</inner></outer> c\n");
  CHECK_EQ(tagged(s, "a b c"), std::string("t:<outer>a <inner>b</inner></outer> c"));
}

TEST(attribute_outside_slot_is_an_error) {
  CHECK_THROWS(nlu::CompileError, nlu::Script::compile("t\n    nine{hour=9}\n"));
  CHECK_THROWS(nlu::CompileError, nlu::Script::compile("t\n    <a>x</a> y{b.k=1}\n"));
}

TEST(mismatched_slot_tags_are_errors) {
  CHECK_THROWS(nlu::CompileError, nlu::Script::compile("t\n    <a>x</b>\n"));
  CHECK_THROWS(nlu::CompileError, nlu::Script::compile("t\n    <a>x\n"));
}

TEST(compile_error_reports_line_and_column) {
  try {
    nlu::Script::compile("card:lost\n    report (a) lost card\n    [신용|체크카드 분실\n");
    CHECK(false);
  } catch (const nlu::CompileError& e) {
    CHECK_EQ(e.line(), 3);
    CHECK(e.column() > 0);
  }
}

namespace {

// The lines of every error compiling `source` reports, or "ok".
std::string error_lines(const std::string& source) {
  try {
    nlu::Script::compile(source);
  } catch (const nlu::CompileError& e) {
    std::string out;
    for (const nlu::Diagnostic& d : e.errors())
      out += (out.empty() ? "" : " ") + std::to_string(d.line);
    return out;
  }
  return "ok";
}

}  // namespace

TEST(compile_reports_every_error_at_once) {
  const std::string source =
      "card:lost\n"
      "    [신용|체크카드 분실\n"      // 2: unclosed group
      "    I lost my @{card}\n"       // 3: undefined entity
      "agent:connect\n"
      "    talk to (an agent\n"       // 5: unclosed group
      "    상담원 @var.{polite}\n"     // 6: undefined variable
      "@empty:\n";                     // 7: entity without values
  CHECK_EQ(error_lines(source), std::string("2 3 5 6 7"));
  try {
    nlu::Script::compile(source);
  } catch (const nlu::CompileError& e) {
    // The first error stays available on its own; what() lists them all.
    CHECK_EQ(e.line(), 2);
    CHECK_EQ(e.errors().size(), 5u);
    CHECK_EQ(std::string(e.what()).find("line 2,"), 0u);
    CHECK(std::string(e.what()).find("\nline 7,") != std::string::npos);
  }
}

TEST(compile_errors_do_not_cascade) {
  // A broken label or definition line hides the lines indented under it.
  CHECK_EQ(error_lines("card lost\n    I lost my card\n    lost card\n"), std::string("1"));
  CHECK_EQ(error_lines("@bad name: x\n    y\n"), std::string("1"));
  CHECK_EQ(error_lines("    stray\n    lines\nok\n    fine\n"), std::string("1"));
  // An error inside a definition is reported once, not once per use.
  CHECK_EQ(error_lines("@card: [credit|debit\na\n    @{card}\nb\n    my @{card}\n"),
           std::string("1"));
}

TEST(pattern_line_outside_block_is_an_error) {
  CHECK_THROWS(nlu::CompileError, nlu::Script::compile("    잔액 조회\n"));
}

TEST(duplicate_definitions_are_errors) {
  CHECK_THROWS(nlu::CompileError, nlu::Script::compile("@a: x\n@a: y\n"));
  CHECK_THROWS(nlu::CompileError, nlu::Script::compile("@var.a: x\n@var.a: y\n"));
}

TEST(many_rules_classify_correctly) {
  std::string source;
  for (int i = 0; i < 2000; ++i) {
    source += "label" + std::to_string(i) + "\n    command number " + std::to_string(i) +
              " (please)\n";
  }
  nlu::Script s = nlu::Script::compile(source);
  CHECK_EQ(labels(s, "command number 1234 please"), (std::vector<std::string>{"label1234"}));
  CHECK_EQ(labels(s, "command number 7"), (std::vector<std::string>{"label7"}));
  CHECK(labels(s, "command number 20000").empty());
}

TEST(concurrent_matches_agree_with_the_script) {
  // Enough distinct rule text that the lazy DFA outgrows its cache and resets
  // while other threads are still matching.
  const int kRules = 3000;
  std::string source;
  for (int i = 0; i < kRules; ++i) {
    source += "label" + std::to_string(i) + "\n    order " + std::to_string(i * 7919) +
              " for customer " + std::to_string(i) + "\n";
  }
  nlu::Script s = nlu::Script::compile(source);
  auto input = [](int i) {
    return "order " + std::to_string(i * 7919) + " for customer " + std::to_string(i);
  };
  const int kThreads = 4;
  std::vector<int> wrong(kThreads, 0);
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      for (int k = 0; k < kRules; ++k) {
        int i = (k * (2 * t + 1) + t * 997) % kRules;
        if (labels(s, input(i)) != std::vector<std::string>{"label" + std::to_string(i)})
          ++wrong[t];
        if (!labels(s, input(i) + "0").empty()) ++wrong[t];
      }
    });
  }
  for (std::thread& t : threads) t.join();
  int total = 0;
  for (int w : wrong) total += w;
  CHECK_EQ(total, 0);
}

TEST(specificity_counts_characters_matched_by_pattern_text) {
  nlu::Script s = nlu::Script::compile(
      "card:any\n"
      "    I lost my <?>+?\n"
      "exchange:rate\n"
      "    @{currency} 환율 `@{NUMBER}\n"
      "@currency: 달러, 엔화\n");
  auto specificity = [&](const std::string& text) {
    std::vector<nlu::Match> found = s.match(text);
    return found.size() == 1 ? found[0].specificity : -1;
  };
  // Spaces don't count; wildcard text doesn't either.
  CHECK_EQ(specificity("I lost my debit card"), 7);
  CHECK_EQ(specificity("달러 환율 1300"), 8);
}

TEST(match_best_keeps_the_most_specific_labels) {
  nlu::Script s = nlu::Script::compile(
      "card:lost\n"
      "    I lost my [credit|debit] card\n"
      "report:any\n"
      "    I <?>+?\n"
      "report:card\n"
      "    <?>+? card\n");
  CHECK_EQ(labels(s, "I lost my debit card"),
           (std::vector<std::string>{"card:lost", "report:any", "report:card"}));
  std::vector<nlu::Match> best = s.match_best("I lost my debit card");
  CHECK_EQ(best.size(), 1u);
  if (best.size() == 1) CHECK_EQ(best[0].label, std::string("card:lost"));
  CHECK(s.match_best("thank you").empty());
}

TEST(match_best_keeps_ties_in_script_order) {
  nlu::Script s = nlu::Script::compile(
      "report:any\n"
      "    I <?>+?\n"
      "report:a\n"
      "    <?>+? a\n");
  std::vector<std::string> best;
  for (const nlu::Match& m : s.match_best("I found a")) best.push_back(m.label);
  CHECK_EQ(best, (std::vector<std::string>{"report:any", "report:a"}));
}

TEST(match_best_uses_the_most_specific_rule_of_a_label) {
  nlu::Script s = nlu::Script::compile(
      "card:lost\n"
      "    I lost my <card><?>+?</card>\n"
      "    I lost my <card>@var.{card}</card>\n"
      "@var.card: [credit|debit] card\n");
  std::vector<nlu::Match> all = s.match("I lost my debit card");
  std::vector<nlu::Match> best = s.match_best("I lost my debit card");
  CHECK_EQ(all.size(), 1u);
  CHECK_EQ(best.size(), 1u);
  if (all.size() != 1 || best.size() != 1) return;
  CHECK_EQ(all[0].line, 2);
  CHECK_EQ(best[0].line, 3);
  CHECK_EQ(best[0].specificity, 16);
  CHECK_EQ(best[0].tagged, std::string("I lost my <card>debit card</card>"));
}

TEST(match_best_prefers_entities_to_wildcards) {
  nlu::Script s = nlu::Script::compile(
      "@product: 적금, 예금\n"
      "account:close:product\n"
      "    @{product}(을/를) 해지\n"
      "account:close\n"
      "    <account><?>+?</account>(을/를) 해지\n");
  std::vector<nlu::Match> best = s.match_best("적금을 해지");
  CHECK_EQ(best.size(), 1u);
  if (best.size() == 1) CHECK_EQ(best[0].label, std::string("account:close:product"));
  best = s.match_best("청약통장을 해지");
  CHECK_EQ(best.size(), 1u);
  if (best.size() == 1) CHECK_EQ(best[0].label, std::string("account:close"));
}

TEST(korean_number_entity_normalizes_its_value) {
  nlu::Script s = nlu::Script::compile("transfer\n    `@{KO_NUMBER} 원 보내줘\n");
  auto value = [&](const std::string& text) -> std::string {
    std::vector<nlu::Match> found = s.match(text);
    if (found.size() != 1 || found[0].slots.size() != 1) return "<no match>";
    return attribute(found[0].slots[0], "value");
  };
  CHECK_EQ(value("3만 원 보내줘"), std::string("30000"));
  CHECK_EQ(value("3만 5천 원 보내줘"), std::string("35000"));
  CHECK_EQ(value("3만5천원 보내줘"), std::string("35000"));
  CHECK_EQ(value("삼만오천 원 보내줘"), std::string("35000"));
  CHECK_EQ(value("35000원 보내줘"), std::string("35000"));
  CHECK_EQ(value("35,000원 보내줘"), std::string("35000"));
  CHECK_EQ(value("2,500만 원 보내줘"), std::string("25000000"));
  CHECK_EQ(value("1억 2천만 원 보내줘"), std::string("120000000"));
  CHECK_EQ(value("만 원 보내줘"), std::string("10000"));
  CHECK_EQ(value("천오백 원 보내줘"), std::string("1500"));
  CHECK_EQ(value("십이만 원 보내줘"), std::string("120000"));
  CHECK_EQ(value("3천5백 원 보내줘"), std::string("3500"));
  CHECK_EQ(value("1조 원 보내줘"), std::string("1000000000000"));
  CHECK_EQ(value("만만 원 보내줘"), std::string("<no match>"));
  CHECK_EQ(value("많이 원 보내줘"), std::string("<no match>"));
  CHECK_EQ(value("3,50 원 보내줘"), std::string("<no match>"));
  // Too large to represent: the slot matches without a value.
  CHECK_EQ(value("99999999999999999999999 원 보내줘"), std::string("<unset>"));
}

TEST(korean_number_slot_covers_only_the_number) {
  nlu::Script s = nlu::Script::compile(
      "transfer:send\n"
      "    <recipient><?>+?</recipient>한테 <amount>`@{KO_NUMBER} 원</amount> 보내줘\n");
  CHECK_EQ(tagged(s, "엄마한테 3만 5천 원 보내줘"),
           std::string("transfer:send:<recipient>엄마</recipient>한테 <amount>"
                       "<KO_NUMBER value=\"35000\">3만 5천</KO_NUMBER> 원</amount> 보내줘"));
}

TEST(option_ignore_trailing_punctuation) {
  const std::string rules =
      "agent:check\n"
      "    are you there\n"
      "    거기 계세요\n"
      "card:blocked\n"
      "    is my card blocked \"?\"\n";
  nlu::Script strict = nlu::Script::compile(rules);
  CHECK(labels(strict, "are you there?").empty());
  nlu::Script lenient = nlu::Script::compile("option ignore-trailing-punctuation\n" + rules);
  for (const char* text : {"are you there", "are you there?", "are you there ?!", "are you there...",
                           "거기 계세요？", "거기 계세요~~"})
    CHECK_EQ(labels(lenient, text), (std::vector<std::string>{"agent:check"}));
  CHECK_EQ(labels(lenient, "is my card blocked?"), (std::vector<std::string>{"card:blocked"}));
  CHECK(labels(lenient, "are you? there").empty());
  // The punctuation stays in the tagged text, outside any slot.
  nlu::Script slots = nlu::Script::compile(
      "option ignore-trailing-punctuation\ncall\n    call <who><?>+?</who>\n");
  CHECK_EQ(tagged(slots, "call mom!"), std::string("call:call <who>mom</who>!"));
}

TEST(option_fold_width) {
  const std::string rules = "card:reissue\n    [OTP|ATM 카드] 재발급\n";
  CHECK(labels(nlu::Script::compile(rules), "ＯＴＰ 재발급").empty());
  nlu::Script s = nlu::Script::compile("option fold-width\n" + rules);
  CHECK_EQ(labels(s, "ＯＴＰ 재발급"), (std::vector<std::string>{"card:reissue"}));
  CHECK_EQ(labels(s, "ａｔｍ 카드 재발급"), (std::vector<std::string>{"card:reissue"}));
  // Patterns are folded too, and system entities see folded digits.
  nlu::Script wide = nlu::Script::compile(
      "option fold-width\nt\n    ＯＴＰ `@{NUMBER}개\n");
  std::vector<nlu::Match> found = wide.match("otp １２개");
  CHECK_EQ(found.size(), 1u);
  if (found.size() == 1) CHECK_EQ(found[0].slots[0].text, std::string("１２"));
}

TEST(unknown_or_misplaced_options_are_errors) {
  CHECK_THROWS(nlu::CompileError, nlu::Script::compile("option fold-everything\n"));
  CHECK_THROWS(nlu::CompileError, nlu::Script::compile("a\n    x\noption fold-width\n"));
}

namespace {

std::string spans(const nlu::Script& s, const std::string& text) {
  std::string out;
  for (const nlu::Match& m : s.search(text))
    out += (out.empty() ? "" : " | ") + m.label + "=" + text.substr(m.begin, m.end - m.begin);
  return out;
}

}  // namespace

TEST(search_finds_rules_inside_free_text) {
  nlu::Script s = nlu::Script::compile(
      "when\n    `@{DATETIME}\n"
      "money\n    `@{MONEY}\n"
      "card:lost\n    [체크|신용]카드 분실\n");
  const std::string text = "안녕하세요 내일 오후 3시에 5만 원 보내고 체크카드 분실 신고도 할게요";
  CHECK_EQ(spans(s, text), std::string("when=내일 오후 3시 | money=5만 원 | card:lost=체크카드 분실"));
  std::vector<nlu::Match> found = s.search(text);
  CHECK_EQ(found.size(), 3u);
  if (found.size() == 3) {
    CHECK_EQ(found[0].tagged,
             std::string("<DATETIME day_offset=\"+1\" part_of_day=\"PM\" hour=\"15\">"
                         "내일 오후 3시</DATETIME>"));
    CHECK_EQ(found[1].slots[0].attributes.size(), 2u);
  }
  CHECK(s.search("감사합니다").empty());
}

TEST(search_keeps_latin_words_whole) {
  nlu::Script s = nlu::Script::compile("card\n    card\n카드\n    카드\n");
  CHECK_EQ(spans(s, "a postcard and a card"), std::string("card=card"));
  CHECK_EQ(spans(s, "체크카드를 잃어버렸어요"), std::string("카드=카드"));
}

TEST(search_takes_the_longest_match_and_moves_past_it) {
  nlu::Script s = nlu::Script::compile(
      "short\n    lost card\n"
      "long\n    lost card report\n"
      "card\n    card\n");
  CHECK_EQ(spans(s, "lost card report and card"), std::string("long=lost card report | card=card"));
  // match() spans the whole input.
  std::vector<nlu::Match> found = s.match("lost card");
  CHECK_EQ(found.size(), 1u);
  if (found.size() == 1) {
    CHECK_EQ(found[0].begin, 0u);
    CHECK_EQ(found[0].end, std::string("lost card").size());
  }
}

namespace {

std::string order_rules(int count) {
  std::string source;
  for (int i = 0; i < count; ++i)
    source += "label" + std::to_string(i) + "\n    order " + std::to_string(i * 7919) +
              " for customer " + std::to_string(i) + "\n";
  return source;
}

std::string order(int i) {
  return "order " + std::to_string(i * 7919) + " for customer " + std::to_string(i);
}

}  // namespace

TEST(a_full_cache_keeps_matching_correctly) {
  nlu::Script s = nlu::Script::compile(order_rules(300));
  nlu::CacheOptions options;
  options.max_states = 200;
  s.set_cache_options(options);
  for (int round = 0; round < 3; ++round)
    for (int i = 0; i < 300; ++i)
      CHECK_EQ(labels(s, order(i)), (std::vector<std::string>{"label" + std::to_string(i)}));
  nlu::CacheStats stats = s.cache_stats();
  CHECK(stats.evictions > 0);
  CHECK(stats.states <= 200u);
  CHECK(stats.states > 1u);  // the states in use survive an eviction
}

TEST(the_cache_stays_within_its_memory_budget) {
  nlu::Script s = nlu::Script::compile(order_rules(300));
  nlu::CacheOptions options;
  options.max_bytes = 64 * 1024;
  s.set_cache_options(options);
  for (int i = 0; i < 300; ++i) s.match(order(i));
  CHECK(s.cache_stats().bytes <= 64 * 1024 + 16 * 1024);  // one state may go over
}

TEST(per_thread_caches_are_separate) {
  nlu::Script s = nlu::Script::compile(order_rules(200));
  nlu::CacheOptions options;
  options.per_thread = true;
  s.set_cache_options(options);
  CHECK_EQ(labels(s, order(7)), (std::vector<std::string>{"label7"}));
  const std::size_t mine = s.cache_stats().states;
  int wrong = 0;
  std::thread other([&] {
    for (int i = 0; i < 200; ++i)
      if (labels(s, order(i)) != std::vector<std::string>{"label" + std::to_string(i)}) ++wrong;
  });
  other.join();
  CHECK_EQ(wrong, 0);
  CHECK_EQ(s.cache_stats().states, mine);  // the other thread filled its own cache
}

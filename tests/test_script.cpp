// Behavior of scripts: labels, slots, attributes and compile errors.
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
      "control:up:volume\n"
      "    turn up (the) volume\n"
      "    volume up\n"
      "\n"
      "control:any\n"
      "    turn up <?>+?\n"
      "\n"
      "greeting\n"
      "    hello\n");
  CHECK_EQ(labels(s, "Turn up volume"), (std::vector<std::string>{"control:up:volume", "control:any"}));
  CHECK_EQ(labels(s, "volume up"), (std::vector<std::string>{"control:up:volume"}));
  CHECK_EQ(labels(s, "turn up the heat"), (std::vector<std::string>{"control:any"}));
  CHECK_EQ(labels(s, "hello"), (std::vector<std::string>{"greeting"}));
  CHECK(labels(s, "goodbye").empty());
  CHECK_EQ(s.rule_count(), 4u);
}

TEST(label_without_slots_tags_plain_text) {
  nlu::Script s = nlu::Script::compile("control:up:volume\n    turn up volume\n");
  CHECK_EQ(tagged(s, "Turn up volume"), std::string("control:up:volume:Turn up volume"));
}

TEST(slot_captures_span_of_original_text) {
  nlu::Script s = nlu::Script::compile("order\n    @{메뉴} [주문|배달] (해줘)\n@메뉴: 치킨, 피자\n");
  std::vector<nlu::Match> found = s.match("피자 배달해줘");
  CHECK_EQ(found.size(), 1u);
  CHECK_EQ(found[0].slots.size(), 1u);
  CHECK_EQ(found[0].slots[0].name, std::string("메뉴"));
  CHECK_EQ(found[0].slots[0].text, std::string("피자"));
  CHECK_EQ(found[0].slots[0].begin, 0u);
  CHECK_EQ(found[0].slots[0].end, std::string("피자").size());
  CHECK_EQ(found[0].tagged, std::string("<메뉴>피자</메뉴> 배달해줘"));
}

TEST(slot_text_keeps_original_case_and_spacing) {
  nlu::Script s = nlu::Script::compile("play\n    play <song><?>+?</song>\n");
  std::vector<nlu::Match> found = s.match("Play  Let It   Be");
  CHECK_EQ(found.size(), 1u);
  CHECK_EQ(found[0].slots[0].text, std::string("Let It   Be"));
  CHECK_EQ(found[0].tagged, std::string("Play  <song>Let It   Be</song>"));
}

TEST(wildcard_in_slot_is_lazy_and_optional_group_is_greedy) {
  nlu::Script s = nlu::Script::compile(
      "play\n    play <song><?>+?</song> (by <artist><?>+?</artist>)\n");
  CHECK_EQ(tagged(s, "play hello by adele"),
           std::string("play:play <song>hello</song> by <artist>adele</artist>"));
  CHECK_EQ(tagged(s, "play hello"), std::string("play:play <song>hello</song>"));
}

TEST(attributes_normalize_slot_values) {
  nlu::Script s = nlu::Script::compile(
      "alarm\n"
      "    wake me up at <cons_datetime>[nine{hour=9}|ten{hour=10}] o'clock"
      " (in the [morning{part_of_day=MORNING}|evening{part_of_day=EVENING}])</cons_datetime>\n");
  CHECK_EQ(tagged(s, "Wake me up at nine o'clock in the morning"),
           std::string("alarm:Wake me up at <cons_datetime hour=\"9\" part_of_day=\"MORNING\">"
                       "nine o'clock in the morning</cons_datetime>"));
  std::vector<nlu::Match> found = s.match("wake me up at ten o'clock");
  CHECK_EQ(found.size(), 1u);
  CHECK_EQ(attribute(found[0].slots[0], "hour"), std::string("10"));
  CHECK_EQ(attribute(found[0].slots[0], "part_of_day"), std::string("<unset>"));
}

TEST(qualified_attribute_targets_named_slot_anywhere_in_rule) {
  nlu::Script s = nlu::Script::compile(
      "translation:request:translation\n"
      "    translate <cons_translation><?>+?</cons_translation> [in|into]"
      " [japanese{cons_translation.target=ja}|korean{cons_translation.target=ko}]\n");
  CHECK_EQ(tagged(s, "Translate good morning in Japanese"),
           std::string("translation:request:translation:Translate "
                       "<cons_translation target=\"ja\">good morning</cons_translation> in Japanese"));
}

TEST(entity_values_can_carry_attributes) {
  nlu::Script s = nlu::Script::compile(
      "@lang: [japanese|일본어]{code=ja}, [korean|한국어]{code=ko}\n"
      "set_language\n"
      "    @{lang}::[로|으로] 바꿔줘\n");
  std::vector<nlu::Match> found = s.match("일본어로 바꿔줘");
  CHECK_EQ(found.size(), 1u);
  CHECK_EQ(found[0].slots[0].name, std::string("lang"));
  CHECK_EQ(attribute(found[0].slots[0], "code"), std::string("ja"));
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
    nlu::Script::compile("greeting\n    hello\n    good [morning|evening\n");
    CHECK(false);
  } catch (const nlu::CompileError& e) {
    CHECK_EQ(e.line(), 3);
    CHECK(e.column() > 0);
  }
}

TEST(pattern_line_outside_block_is_an_error) {
  CHECK_THROWS(nlu::CompileError, nlu::Script::compile("    hello\n"));
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

// Behavior of Script::lint(): overlapping labels and unused definitions.
#include <string>
#include <vector>

#include "harness.hpp"
#include "nlu_script/nlu_script.hpp"

namespace {

std::vector<std::string> labels(const nlu::Script& script, const std::string& text) {
  std::vector<std::string> out;
  for (const nlu::Match& m : script.match(text)) out.push_back(m.label);
  return out;
}

}  // namespace

TEST(lint_finds_labels_that_match_the_same_input) {
  nlu::Script s = nlu::Script::compile(
      "card:lost\n"
      "    I lost my [credit|debit] card\n"
      "agent:connect\n"
      "    talk to an agent\n"
      "report:any\n"
      "    I <?>+?\n");
  nlu::LintReport report = s.lint();
  CHECK(report.complete);
  CHECK_EQ(report.overlaps.size(), 1u);
  if (report.overlaps.size() != 1) return;
  const nlu::Overlap& o = report.overlaps[0];
  CHECK_EQ(o.label, std::string("card:lost"));
  CHECK_EQ(o.line, 2);
  CHECK_EQ(o.other_label, std::string("report:any"));
  CHECK_EQ(o.other_line, 6);
  // The shortest such input, with spaces wherever the patterns allow them.
  CHECK_EQ(o.example, std::string("i lost my debit card"));
  CHECK_EQ(labels(s, o.example), (std::vector<std::string>{"card:lost", "report:any"}));
}

TEST(lint_example_goes_through_wildcards) {
  nlu::Script s = nlu::Script::compile(
      "transfer:send\n"
      "    send <?>+? to 엄마\n"
      "transfer:recipient\n"
      "    send money to <?>+?\n");
  nlu::LintReport report = s.lint();
  CHECK_EQ(report.overlaps.size(), 1u);
  if (report.overlaps.size() != 1) return;
  CHECK_EQ(labels(s, report.overlaps[0].example),
           (std::vector<std::string>{"transfer:send", "transfer:recipient"}));
}

TEST(lint_names_the_rules_that_overlap) {
  nlu::Script s = nlu::Script::compile(
      "card:lost\n"
      "    I lost my card\n"
      "    카드 분실\n"
      "report:any\n"
      "    report <?>+?\n"
      "    카드 <?>+?\n");
  nlu::LintReport report = s.lint();
  CHECK_EQ(report.overlaps.size(), 1u);
  if (report.overlaps.size() != 1) return;
  CHECK_EQ(report.overlaps[0].line, 3);
  CHECK_EQ(report.overlaps[0].other_line, 6);
  CHECK_EQ(report.overlaps[0].example, std::string("카드 분실"));
}

TEST(lint_ignores_disjoint_labels_and_rules_of_one_label) {
  nlu::Script s = nlu::Script::compile(
      "card:lost\n"
      "    I lost my card\n"
      "    I lost my <?>+?\n"
      "agent:connect\n"
      "    [talk|speak] to an agent\n");
  CHECK(s.lint().overlaps.empty());
}

TEST(lint_respects_exclusions) {
  nlu::Script s = nlu::Script::compile(
      "@product: 적금, 예금\n"
      "account:close:product\n"
      "    @{product} 해지\n"
      "account:close\n"
      "    \\[<?>*? 적금 <?>*?|<?>*? 예금 <?>*?] <?>+? 해지\n"
      "account:close:any\n"
      "    <?>+? 해지\n");
  nlu::LintReport report = s.lint();
  std::vector<std::string> pairs;
  for (const nlu::Overlap& o : report.overlaps) pairs.push_back(o.label + " & " + o.other_label);
  CHECK_EQ(pairs, (std::vector<std::string>{"account:close:product & account:close:any",
                                            "account:close & account:close:any"}));
}

TEST(lint_reports_definitions_no_rule_uses) {
  nlu::Script s = nlu::Script::compile(
      "@currency: 달러, 엔화\n"
      "@var.digit: [0|1|2]\n"
      "@var.code: @var.{digit}@var.{digit}\n"
      "@var.unused_digit: [7|8]\n"
      "@bank: [국민|신한] @var.{unused_digit}\n"
      "exchange:rate\n"
      "    @{currency} 환율\n"
      "card:freeze\n"
      "    끝자리 @var.{code} 카드 정지\n");
  nlu::LintReport report = s.lint();
  std::vector<std::string> unused;
  for (const nlu::UnusedDefinition& d : report.unused)
    unused.push_back(d.name + "@" + std::to_string(d.line));
  // @var.unused_digit is only used by @bank, which no rule uses.
  CHECK_EQ(unused, (std::vector<std::string>{"@var.unused_digit@4", "@bank@5"}));
}

TEST(lint_reports_an_incomplete_search) {
  nlu::Script s = nlu::Script::compile("a\n    one\nb\n    two\n");
  CHECK(!s.lint(2).complete);
  CHECK(s.lint().complete);
}

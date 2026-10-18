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

TEST(lint_follows_particle_pairs) {
  nlu::Script s = nlu::Script::compile(
      "close:proper\n"
      "    적금을/를 해지\n"
      "close:typo\n"
      "    적금를 해지\n");
  CHECK(s.lint().overlaps.empty());

  s = nlu::Script::compile(
      "close:any\n"
      "    <?>을/를 해지\n"
      "close:vowel\n"
      "    <?>를 해지\n");
  nlu::LintReport report = s.lint();
  CHECK_EQ(report.overlaps.size(), 1u);
  if (report.overlaps.size() != 1) return;
  CHECK_EQ(labels(s, report.overlaps[0].example),
           (std::vector<std::string>{"close:any", "close:vowel"}));
}

namespace {

const char* kOverlapping =
    "card:lost\n"
    "    I lost my card\n"
    "report:any\n"
    "    I <?>+?\n"
    "report:card\n"
    "    <?>+? card\n";

std::vector<std::string> pairs(const std::vector<nlu::Overlap>& overlaps) {
  std::vector<std::string> out;
  for (const nlu::Overlap& o : overlaps) out.push_back(o.label + " & " + o.other_label);
  return out;
}

}  // namespace

TEST(lint_comment_allows_overlaps_among_its_labels) {
  nlu::Script s = nlu::Script::compile(std::string(kOverlapping) +
                                       "# lint: allow-overlap card:lost report:any\n");
  nlu::LintReport report = s.lint();
  CHECK_EQ(pairs(report.overlaps), (std::vector<std::string>{"card:lost & report:card",
                                                             "report:any & report:card"}));
  CHECK_EQ(pairs(report.allowed_overlaps), (std::vector<std::string>{"card:lost & report:any"}));
  CHECK(report.directive_errors.empty());
}

TEST(lint_comment_star_allows_every_overlap_of_a_label) {
  nlu::Script s = nlu::Script::compile(
      "# lint: allow-overlap report:any *\n"
      "#lint:allow-overlap   report:card card:lost\n" + std::string(kOverlapping));
  nlu::LintReport report = s.lint();
  CHECK(report.overlaps.empty());
  CHECK_EQ(report.allowed_overlaps.size(), 3u);
}

TEST(lint_comment_mistakes_are_reported) {
  nlu::Script s = nlu::Script::compile(std::string(kOverlapping) +
                                       "# lint: allow-overlap card:lsot report:any\n"
                                       "# lint: allow-overlap card:lost\n"
                                       "# lint: allow-overlpa card:lost report:any\n"
                                       "# lint is not run on this script\n");
  nlu::LintReport report = s.lint();
  std::vector<std::string> lines;
  for (const nlu::LintDirectiveError& e : report.directive_errors)
    lines.push_back(std::to_string(e.line));
  CHECK_EQ(lines, (std::vector<std::string>{"7", "8", "9"}));
  CHECK_EQ(report.overlaps.size(), 3u);
}

namespace {

std::string dead_lines(const std::string& source) {
  std::string out;
  for (const nlu::DeadRule& d : nlu::Script::compile(source).lint().dead_rules)
    out += (out.empty() ? "" : " ") + std::to_string(d.line) + "<" +
           std::to_string(d.covered_by_line);
  return out;
}

}  // namespace

TEST(lint_finds_rules_that_earlier_rules_of_their_label_hide) {
  // Line 3 is never reached: line 2 matches everything it does, first.
  CHECK_EQ(dead_lines("card:lost\n    I lost my <?>+?\n    I lost my card\n"), std::string("3<2"));
  // The other way round, each rule is used for some input.
  CHECK_EQ(dead_lines("card:lost\n    I lost my card\n    I lost my <?>+?\n"), std::string());
  // Hidden by two earlier rules together: no single one covers it.
  CHECK_EQ(dead_lines("a\n    x [1|2]\n    x 3\n    x [1|3]\n"), std::string("4<0"));
  // Rules of other labels don't hide a rule.
  CHECK_EQ(dead_lines("a\n    I <?>+?\nb\n    I lost my card\n"), std::string());
}

TEST(lint_tells_which_overlaps_match_best_resolves) {
  nlu::Script s = nlu::Script::compile(
      "@product: 적금, 예금\n"
      "account:close:product\n"
      "    @{product}(을/를) 해지\n"
      "account:close\n"
      "    <?>+?(을/를) 해지\n");
  nlu::LintReport report = s.lint();
  CHECK_EQ(report.overlaps.size(), 1u);
  // An entity beats a wildcard on every sentence both match.
  if (report.overlaps.size() == 1)
    CHECK_EQ(report.overlaps[0].best_label, std::string("account:close:product"));
  CHECK_EQ(report.findings().size(), 1u);
  CHECK(report.findings(true).empty());

  // Two wildcard rules may tie ("I x now"), so match_best() may return both.
  report = nlu::Script::compile("report:any\n    I <?>+?\nreport:now\n    <?>+? now\n").lint();
  CHECK_EQ(report.overlaps.size(), 1u);
  if (report.overlaps.size() == 1) CHECK_EQ(report.overlaps[0].best_label, std::string());
  CHECK_EQ(report.findings(true).size(), 1u);
}

TEST(lint_findings_read_like_the_cli) {
  nlu::Script s = nlu::Script::compile(
      "card:lost\n    I lost my <?>+?\n    I lost my card\n@unused: x\n");
  std::vector<nlu::LintFinding> findings = s.lint().findings();
  CHECK_EQ(findings.size(), 2u);
  if (findings.size() != 2) return;
  CHECK_EQ(findings[0].kind, std::string("dead-rule"));
  CHECK_EQ(findings[0].line, 3);
  CHECK(findings[0].message.find("line 2") != std::string::npos);
  CHECK_EQ(findings[1].kind, std::string("unused"));
  CHECK_EQ(findings[1].message, std::string("@unused is never used"));
}

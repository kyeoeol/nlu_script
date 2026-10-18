// Behavior of script test files, run by `nlu_script_cli --test`.
#include <sstream>
#include <stdexcept>
#include <string>

#include "harness.hpp"
#include "nlu_script/nlu_script.hpp"
#include "script_tests.hpp"

namespace {

const char* kScript =
    "card:lost\n"
    "    I lost my <card>[credit{type=CREDIT}|debit{type=DEBIT}] card</card>\n"
    "report:any\n"
    "    I <?>+?\n"
    "agent:connect\n"
    "    상담원 연결 (해주세요)\n";

struct Run {
  nlu_tools::TestSummary summary;
  std::string report;
};

Run run(const std::string& tests) {
  nlu::Script script = nlu::Script::compile(kScript);
  std::istringstream in(tests);
  std::ostringstream report;
  Run r;
  r.summary = nlu_tools::run_script_tests(script, "rules.nlu", in, "rules.test", report);
  r.report = report.str();
  return r;
}

bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

std::string error_of(const std::string& tests) {
  try {
    run(tests);
  } catch (const std::runtime_error& e) {
    return e.what();
  }
  return "<no error>";
}

}  // namespace

TEST(expected_outputs_are_the_cli_output_lines) {
  Run r = run(
      "# Comments and blank lines are skipped.\n"
      "\n"
      "I lost my debit card\n"
      "    card:lost:I lost my <card type=\"DEBIT\">debit card</card>\n"
      "    report:any:I lost my debit card\n"
      "상담원 연결해주세요   \n"
      "    agent:connect:상담원 연결해주세요\n"
      "thank you\n"
      "    -\n");
  CHECK_EQ(r.summary.passed, 3);
  CHECK_EQ(r.summary.failed, 0);
  CHECK_EQ(r.report, std::string());
}

TEST(failure_shows_where_and_what_differs) {
  Run r = run(
      "I lost my credit card\n"
      "    card:lost:I lost my <card type=\"DEBIT\">credit card</card>\n"
      "    report:any:I lost my credit card\n"
      "thank you\n"
      "    agent:connect:thank you\n");
  CHECK_EQ(r.summary.passed, 0);
  CHECK_EQ(r.summary.failed, 2);
  CHECK(contains(r.report, "rules.test:1: I lost my credit card"));
  CHECK(contains(r.report, "card:lost:I lost my <card type=\"DEBIT\">credit card</card>"));
  // The actual output names the rule that produced it.
  CHECK(contains(r.report, "card:lost:I lost my <card type=\"CREDIT\">credit card</card>"
                           "  (rules.nlu:2)"));
  CHECK(contains(r.report, "report:any:I lost my credit card  (rules.nlu:4)"));
  CHECK(contains(r.report, "rules.test:4: thank you"));
}

TEST(missing_or_extra_labels_fail) {
  Run r = run(
      "I lost my debit card\n"
      "    card:lost:I lost my <card type=\"DEBIT\">debit card</card>\n"
      "I lost my wallet\n"
      "    card:lost:I lost my wallet\n"
      "    report:any:I lost my wallet\n");
  CHECK_EQ(r.summary.passed, 0);
  CHECK_EQ(r.summary.failed, 2);
}

TEST(labels_must_come_in_script_order) {
  Run r = run(
      "I lost my debit card\n"
      "    report:any:I lost my debit card\n"
      "    card:lost:I lost my <card type=\"DEBIT\">debit card</card>\n");
  CHECK_EQ(r.summary.failed, 1);
}

TEST(malformed_test_files_are_errors) {
  CHECK(contains(error_of("    -\n"), "rules.test:1:"));
  CHECK(contains(error_of("thank you\nI lost my wallet\n    report:any:I lost my wallet\n"),
                 "rules.test:1:"));
  CHECK(contains(error_of("I lost my wallet\n    report:any:I lost my wallet\n\nthank you\n"),
                 "rules.test:4:"));
  CHECK(contains(error_of("thank you\n    -\n    report:any:thank you\n"), "rules.test:3:"));
}

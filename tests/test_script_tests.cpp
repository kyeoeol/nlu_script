// Behavior of script test files (Script::run_tests, `nlu_script_cli --test`).
#include <stdexcept>
#include <string>
#include <vector>

#include "harness.hpp"
#include "nlu_script/nlu_script.hpp"

namespace {

const char* kScript =
    "card:lost\n"
    "    I lost my <card>[credit{type=CREDIT}|debit{type=DEBIT}] card</card>\n"
    "report:any\n"
    "    I <?>+?\n"
    "agent:connect\n"
    "    상담원 연결 (해주세요)\n";

nlu::TestReport run(const std::string& tests, bool best = false) {
  return nlu::Script::compile(kScript).run_tests(tests, "rules.test", best);
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
  nlu::TestReport r = run(
      "# Comments and blank lines are skipped.\n"
      "\n"
      "I lost my debit card\n"
      "    card:lost:I lost my <card type=\"DEBIT\">debit card</card>\n"
      "    report:any:I lost my debit card\n"
      "상담원 연결해주세요   \n"
      "    agent:connect:상담원 연결해주세요\n"
      "thank you\r\n"
      "\t-\r\n");
  CHECK_EQ(r.passed, 3);
  CHECK_EQ(r.failures.size(), 0u);
}

TEST(output_lines_are_label_and_tagged_text_or_a_dash) {
  nlu::Script script = nlu::Script::compile(kScript);
  CHECK_EQ(nlu::output_lines(script.match("I lost my debit card")),
           (std::vector<std::string>{"card:lost:I lost my <card type=\"DEBIT\">debit card</card>",
                                     "report:any:I lost my debit card"}));
  CHECK_EQ(nlu::output_lines(script.match("thank you")), std::vector<std::string>{"-"});
}

TEST(best_mode_expects_only_the_most_specific_labels) {
  const std::string tests =
      "I lost my debit card\n"
      "    card:lost:I lost my <card type=\"DEBIT\">debit card</card>\n";
  CHECK_EQ(run(tests, true).passed, 1);
  CHECK_EQ(run(tests, false).failures.size(), 1u);
}

TEST(failure_shows_where_and_what_differs) {
  nlu::TestReport r = run(
      "I lost my credit card\n"
      "    card:lost:I lost my <card type=\"DEBIT\">credit card</card>\n"
      "    report:any:I lost my credit card\n"
      "thank you\n"
      "    agent:connect:thank you\n");
  CHECK_EQ(r.passed, 0);
  CHECK_EQ(r.failures.size(), 2u);
  if (r.failures.size() != 2) return;
  const nlu::TestFailure& card = r.failures[0];
  CHECK_EQ(card.line, 1);
  CHECK_EQ(card.input, std::string("I lost my credit card"));
  CHECK_EQ(card.expected,
           (std::vector<std::string>{"card:lost:I lost my <card type=\"DEBIT\">credit card</card>",
                                     "report:any:I lost my credit card"}));
  CHECK_EQ(card.actual,
           (std::vector<std::string>{"card:lost:I lost my <card type=\"CREDIT\">credit card</card>",
                                     "report:any:I lost my credit card"}));
  // Each actual line comes with the rule that produced it.
  CHECK_EQ(card.matches.size(), 2u);
  if (card.matches.size() == 2) {
    CHECK_EQ(card.matches[0].line, 2);
    CHECK_EQ(card.matches[1].line, 4);
  }
  const nlu::TestFailure& thanks = r.failures[1];
  CHECK_EQ(thanks.line, 4);
  CHECK_EQ(thanks.actual, std::vector<std::string>{"-"});
  CHECK_EQ(thanks.matches.size(), 0u);
}

TEST(missing_or_extra_labels_fail) {
  nlu::TestReport r = run(
      "I lost my debit card\n"
      "    card:lost:I lost my <card type=\"DEBIT\">debit card</card>\n"
      "I lost my wallet\n"
      "    card:lost:I lost my wallet\n"
      "    report:any:I lost my wallet\n");
  CHECK_EQ(r.passed, 0);
  CHECK_EQ(r.failures.size(), 2u);
}

TEST(labels_must_come_in_script_order) {
  nlu::TestReport r = run(
      "I lost my debit card\n"
      "    report:any:I lost my debit card\n"
      "    card:lost:I lost my <card type=\"DEBIT\">debit card</card>\n");
  CHECK_EQ(r.failures.size(), 1u);
}

TEST(malformed_test_files_are_errors) {
  CHECK(contains(error_of("    -\n"), "rules.test:1:"));
  CHECK(contains(error_of("thank you\nI lost my wallet\n    report:any:I lost my wallet\n"),
                 "rules.test:1:"));
  CHECK(contains(error_of("I lost my wallet\n    report:any:I lost my wallet\n\nthank you\n"),
                 "rules.test:4:"));
  CHECK(contains(error_of("thank you\n    -\n    report:any:thank you\n"), "rules.test:3:"));
}

TEST(missing_test_file_is_an_error) {
  nlu::Script script = nlu::Script::compile(kScript);
  std::string message = "<no error>";
  try {
    script.run_tests_file("/nonexistent/rules.test");
  } catch (const std::runtime_error& e) {
    message = e.what();
  }
  CHECK(contains(message, "/nonexistent/rules.test"));
}

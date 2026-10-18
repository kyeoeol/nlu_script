// Behavior of Script::explain(): how far each rule got into an input.
#include <string>
#include <vector>

#include "harness.hpp"
#include "nlu_script/nlu_script.hpp"

namespace {

const char* kScript =
    "card:lost\n"
    "    I lost my [credit|debit] card\n"
    "    체크카드 분실\n"
    "agent:connect\n"
    "    talk to an agent\n"
    "report:any\n"
    "    I <?>+? now\n";

std::string expected(const nlu::Explanation& e) {
  std::string out;
  for (const std::string& x : e.expected) out += (out.empty() ? "" : " ") + x;
  return out;
}

}  // namespace

TEST(explain_shows_how_far_rules_got_and_what_they_wanted_next) {
  nlu::Script s = nlu::Script::compile(kScript);
  std::vector<nlu::Explanation> found = s.explain("I lost my wallet");
  CHECK_EQ(found.size(), 2u);
  if (found.size() != 2) return;
  // report:any gets through the whole input but still wants " now".
  CHECK_EQ(found[0].label, std::string("report:any"));
  CHECK_EQ(found[0].matched, std::string("I lost my wallet").size());
  CHECK(!found[0].complete);
  // More wildcard text, or " now" (the space before it is optional).
  CHECK_EQ(expected(found[0]), std::string("any character n space"));
  CHECK_EQ(found[1].label, std::string("card:lost"));
  CHECK_EQ(found[1].line, 2);
  CHECK_EQ(found[1].matched, std::string("I lost my ").size());
  CHECK_EQ(expected(found[1]), std::string("c d"));
}

TEST(explain_reports_offsets_in_the_original_input) {
  nlu::Script s = nlu::Script::compile(kScript);
  const std::string input = "  체크카드   해지";
  std::vector<nlu::Explanation> found = s.explain(input);
  CHECK_EQ(found.size(), 1u);
  if (found.size() != 1) return;
  CHECK_EQ(found[0].line, 3);
  CHECK_EQ(input.substr(0, found[0].matched), std::string("  체크카드   "));
  CHECK_EQ(expected(found[0]), std::string("분"));
}

TEST(explain_marks_complete_matches_and_respects_the_limit) {
  nlu::Script s = nlu::Script::compile(kScript);
  std::vector<nlu::Explanation> found = s.explain("I lost my debit card now", 1);
  CHECK_EQ(found.size(), 1u);
  if (found.size() == 1) {
    CHECK_EQ(found[0].label, std::string("report:any"));
    CHECK(found[0].complete);
  }
  CHECK(s.explain("hello").empty());  // no rule gets past the first character
}

// Script test files: example sentences with the CLI output expected for each.
//
//   # comment
//   I lost my debit card
//       card:lost:I lost my <card type="DEBIT">debit card</card>
//   thank you
//       -
//
// An unindented line is an input sentence; the indented lines after it are
// the output lines expected for it, in order. `-` expects no match.
#ifndef NLU_SCRIPT_TOOLS_SCRIPT_TESTS_HPP
#define NLU_SCRIPT_TOOLS_SCRIPT_TESTS_HPP

#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "nlu_script/nlu_script.hpp"

namespace nlu_tools {

// The CLI's output for one input: `label:tagged` per match, or `-`.
std::vector<std::string> output_lines(const std::vector<nlu::Match>& matches);

struct TestSummary {
  int passed = 0;
  int failed = 0;
};

// Runs every case in `tests` against `script` and writes each failure to
// `report`. The names locate failures and rules in the report. With `best`,
// cases expect the output of match_best() instead of match(). Throws
// std::runtime_error if the test file is malformed.
TestSummary run_script_tests(const nlu::Script& script, const std::string& script_name,
                             std::istream& tests, const std::string& tests_name,
                             std::ostream& report, bool best = false);

}  // namespace nlu_tools

#endif  // NLU_SCRIPT_TOOLS_SCRIPT_TESTS_HPP

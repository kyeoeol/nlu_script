#include "script_tests.hpp"

#include <stdexcept>

namespace nlu_tools {

namespace {

bool is_blank(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string trim(const std::string& s) {
  std::size_t b = 0;
  while (b < s.size() && is_blank(s[b])) ++b;
  std::size_t e = s.size();
  while (e > b && is_blank(s[e - 1])) --e;
  return s.substr(b, e - b);
}

struct Case {
  int line = 0;
  std::string input;
  std::vector<std::string> expected;
};

}  // namespace

std::vector<std::string> output_lines(const std::vector<nlu::Match>& matches) {
  std::vector<std::string> out;
  for (const nlu::Match& m : matches) out.push_back(m.label + ":" + m.tagged);
  if (out.empty()) out.push_back("-");
  return out;
}

TestSummary run_script_tests(const nlu::Script& script, const std::string& script_name,
                             std::istream& tests, const std::string& tests_name,
                             std::ostream& report, bool best) {
  auto error = [&](int line, const std::string& message) {
    return std::runtime_error(tests_name + ":" + std::to_string(line) + ": " + message);
  };

  std::vector<Case> cases;
  std::string raw;
  int line_no = 0;
  while (std::getline(tests, raw)) {
    ++line_no;
    std::string content = trim(raw);
    if (content.empty() || content[0] == '#') continue;
    if (!is_blank(raw[0])) {
      if (!cases.empty() && cases.back().expected.empty())
        throw error(cases.back().line, "no expected output; write '-' to expect no match");
      cases.push_back({line_no, content, {}});
      continue;
    }
    if (cases.empty()) throw error(line_no, "expected output before any input sentence");
    std::vector<std::string>& expected = cases.back().expected;
    if (!expected.empty() && (content == "-" || expected[0] == "-"))
      throw error(line_no, "'-' must be the only expected output");
    expected.push_back(content);
  }
  if (!cases.empty() && cases.back().expected.empty())
    throw error(cases.back().line, "no expected output; write '-' to expect no match");

  TestSummary summary;
  for (const Case& c : cases) {
    std::vector<nlu::Match> matches = best ? script.match_best(c.input) : script.match(c.input);
    std::vector<std::string> actual = output_lines(matches);
    if (actual == c.expected) {
      ++summary.passed;
      continue;
    }
    ++summary.failed;
    report << tests_name << ":" << c.line << ": " << c.input << "\n";
    for (const std::string& line : c.expected) report << "  expected: " << line << "\n";
    for (std::size_t i = 0; i < actual.size(); ++i) {
      report << "  actual:   " << actual[i];
      if (i < matches.size())
        report << "  (" << (matches[i].file.empty() ? script_name : matches[i].file) << ":"
               << matches[i].line << ")";
      report << "\n";
    }
  }
  return summary;
}

}  // namespace nlu_tools

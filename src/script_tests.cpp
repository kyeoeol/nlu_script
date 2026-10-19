// Script test files: Script::run_tests and output_lines.
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "nlu_script/nlu_script.hpp"

namespace nlu {

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

std::vector<Case> read_cases(const std::string& tests, const std::string& name) {
  auto error = [&](int line, const std::string& message) {
    return std::runtime_error(name + ":" + std::to_string(line) + ": " + message);
  };

  std::vector<Case> cases;
  std::istringstream in(tests);
  std::string raw;
  int line_no = 0;
  while (std::getline(in, raw)) {
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
  return cases;
}

}  // namespace

std::vector<std::string> output_lines(const std::vector<Match>& matches) {
  std::vector<std::string> out;
  for (const Match& m : matches) out.push_back(m.label + ":" + m.tagged);
  if (out.empty()) out.push_back("-");
  return out;
}

TestReport Script::run_tests(const std::string& tests, const std::string& name, bool best) const {
  TestReport report;
  for (Case& c : read_cases(tests, name)) {
    std::vector<Match> matches = best ? match_best(c.input) : match(c.input);
    std::vector<std::string> actual = output_lines(matches);
    if (actual == c.expected) {
      ++report.passed;
      continue;
    }
    report.failures.push_back(TestFailure{c.line, std::move(c.input), std::move(c.expected),
                                          std::move(actual), std::move(matches)});
  }
  return report;
}

TestReport Script::run_tests_file(const std::string& path, bool best) const {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + path);
  std::ostringstream tests;
  tests << in.rdbuf();
  return run_tests(tests.str(), path, best);
}

}  // namespace nlu

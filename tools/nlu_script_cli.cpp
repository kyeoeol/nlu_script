// Reads sentences from stdin (one per line) and prints `label:tagged text` for
// every matching label, or `-` when nothing matches. With --best, prints only
// the most specific labels (Script::match_best).
//
// With --test, runs script test files instead (see script_tests.hpp) and
// exits with 1 if any case fails. With --lint, reports labels that match the
// same input and unused definitions, and exits with 1 if it finds any.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "nlu_script/nlu_script.hpp"
#include "script_tests.hpp"

namespace {

int usage(const char* program) {
  std::cerr << "usage: " << program << " [--best] SCRIPT\n"
            << "       " << program << " --test [--best] SCRIPT TESTS...\n"
            << "       " << program << " --lint SCRIPT\n";
  return 2;
}

int run_tests(const std::string& script_path, const std::vector<std::string>& tests, bool best) {
  nlu::Script script = nlu::Script::compile_file(script_path);
  nlu_tools::TestSummary total;
  for (const std::string& path : tests) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    nlu_tools::TestSummary s =
        nlu_tools::run_script_tests(script, script_path, in, path, std::cout, best);
    total.passed += s.passed;
    total.failed += s.failed;
  }
  std::cout << total.passed << " passed, " << total.failed << " failed\n";
  return total.failed == 0 ? 0 : 1;
}

int run_lint(const std::string& script_path) {
  nlu::LintReport report = nlu::Script::compile_file(script_path).lint();
  std::vector<std::pair<int, std::string>> findings;
  for (const nlu::UnusedDefinition& d : report.unused)
    findings.emplace_back(d.line, d.name + " is never used");
  for (const nlu::LintDirectiveError& e : report.directive_errors)
    findings.emplace_back(e.line, e.message);
  for (const nlu::Overlap& o : report.overlaps)
    findings.emplace_back(o.line, o.label + " and " + o.other_label + " (line " +
                                      std::to_string(o.other_line) + ") both match \"" +
                                      o.example + "\"");
  std::stable_sort(findings.begin(), findings.end(),
                   [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& f : findings) std::cout << script_path << ":" << f.first << ": " << f.second << "\n";
  if (!report.complete)
    std::cout << "note: the automaton is too large to search fully; more overlaps may exist\n";
  std::cout << findings.size() << (findings.size() == 1 ? " finding" : " findings");
  if (const std::size_t n = report.allowed_overlaps.size())
    std::cout << " (" << n << (n == 1 ? " intended overlap" : " intended overlaps")
              << " allowed by lint comments)";
  std::cout << "\n";
  return findings.empty() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  const bool test = !args.empty() && args[0] == "--test";
  const bool lint = !args.empty() && args[0] == "--lint";
  if (test || lint) args.erase(args.begin());
  const bool best = !lint && !args.empty() && args[0] == "--best";
  if (best) args.erase(args.begin());
  if (args.empty() || (test ? args.size() < 2 : args.size() != 1) ||
      args[0].compare(0, 2, "--") == 0)
    return usage(argv[0]);
  const std::string script_path = args[0];
  try {
    if (test) return run_tests(script_path, {args.begin() + 1, args.end()}, best);
    if (lint) return run_lint(script_path);
    nlu::Script script = nlu::Script::compile_file(script_path);
    std::string line;
    while (std::getline(std::cin, line)) {
      for (const std::string& out :
           nlu_tools::output_lines(best ? script.match_best(line) : script.match(line)))
        std::cout << out << "\n";
      std::cout.flush();
    }
  } catch (const nlu::CompileError& e) {
    for (const nlu::Diagnostic& d : e.errors())
      std::cerr << script_path << ":" << d.line << ":" << d.column << ": " << d.message << "\n";
    return 1;
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
  return 0;
}

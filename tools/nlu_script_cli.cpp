// Reads sentences from stdin (one per line) and prints `label:tagged text` for
// every matching label, or `-` when nothing matches.
//
// With --test, runs script test files instead (see script_tests.hpp) and
// exits with 1 if any case fails. With --lint, reports labels that match the
// same input and unused definitions, and exits with 1 if it finds any.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>

#include "nlu_script/nlu_script.hpp"
#include "script_tests.hpp"

namespace {

int usage(const char* program) {
  std::cerr << "usage: " << program << " SCRIPT\n"
            << "       " << program << " --test SCRIPT TESTS...\n"
            << "       " << program << " --lint SCRIPT\n";
  return 2;
}

int run_tests(const std::string& script_path, char** tests, int count) {
  nlu::Script script = nlu::Script::compile_file(script_path);
  nlu_tools::TestSummary total;
  for (int i = 0; i < count; ++i) {
    std::ifstream in(tests[i], std::ios::binary);
    if (!in) throw std::runtime_error(std::string("cannot open ") + tests[i]);
    nlu_tools::TestSummary s =
        nlu_tools::run_script_tests(script, script_path, in, tests[i], std::cout);
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
  for (const nlu::Overlap& o : report.overlaps)
    findings.emplace_back(o.line, o.label + " and " + o.other_label + " (line " +
                                      std::to_string(o.other_line) + ") both match \"" +
                                      o.example + "\"");
  std::stable_sort(findings.begin(), findings.end(),
                   [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& f : findings) std::cout << script_path << ":" << f.first << ": " << f.second << "\n";
  if (!report.complete)
    std::cout << "note: the automaton is too large to search fully; more overlaps may exist\n";
  std::cout << findings.size() << (findings.size() == 1 ? " finding\n" : " findings\n");
  return findings.empty() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc >= 2 ? argv[1] : "";
  const bool test = mode == "--test", lint = mode == "--lint";
  if (test ? argc < 4 : argc != (lint ? 3 : 2)) return usage(argv[0]);
  const std::string script_path = argv[test || lint ? 2 : 1];
  try {
    if (test) return run_tests(script_path, argv + 3, argc - 3);
    if (lint) return run_lint(script_path);
    nlu::Script script = nlu::Script::compile_file(script_path);
    std::string line;
    while (std::getline(std::cin, line)) {
      for (const std::string& out : nlu_tools::output_lines(script.match(line)))
        std::cout << out << "\n";
      std::cout.flush();
    }
  } catch (const nlu::CompileError& e) {
    std::cerr << script_path << ": " << e.what() << "\n";
    return 1;
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
  return 0;
}

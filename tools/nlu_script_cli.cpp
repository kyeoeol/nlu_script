// Reads sentences from stdin (one per line) and prints `label:tagged text` for
// every matching label, or `-` when nothing matches. With --best, prints only
// the most specific labels (Script::match_best). With --explain, a sentence
// that matches nothing is followed by the rules that got furthest into it.
//
// With --test, runs script test files instead (see script_tests.hpp) and
// exits with 1 if any case fails. With --lint, reports labels that match the
// same input and unused definitions, and exits with 1 if it finds any.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include "nlu_script/nlu_script.hpp"
#include "script_tests.hpp"

namespace {

int usage(const char* program) {
  std::cerr << "usage: " << program << " [--best | --explain] SCRIPT\n"
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
  // file:line: message, ordered by file (as found) and line.
  std::vector<std::string> files;
  std::vector<std::tuple<std::size_t, int, std::string>> findings;
  auto add = [&](const std::string& file, int line, const std::string& message) {
    std::size_t index = std::find(files.begin(), files.end(), file) - files.begin();
    if (index == files.size()) files.push_back(file);
    findings.emplace_back(index, line, message);
  };
  for (const nlu::UnusedDefinition& d : report.unused) add(d.file, d.line, d.name + " is never used");
  for (const nlu::LintDirectiveError& e : report.directive_errors) add(e.file, e.line, e.message);
  for (const nlu::Overlap& o : report.overlaps) {
    std::string other = o.other_file == o.file ? "line " + std::to_string(o.other_line)
                                               : o.other_file + ":" + std::to_string(o.other_line);
    add(o.file, o.line,
        o.label + " and " + o.other_label + " (" + other + ") both match \"" + o.example + "\"");
  }
  std::stable_sort(findings.begin(), findings.end(), [](const auto& a, const auto& b) {
    return std::tie(std::get<0>(a), std::get<1>(a)) < std::tie(std::get<0>(b), std::get<1>(b));
  });
  for (const auto& f : findings) {
    const std::string& file = files[std::get<0>(f)];
    std::cout << (file.empty() ? script_path : file) << ":" << std::get<1>(f) << ": "
              << std::get<2>(f) << "\n";
  }
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
  const bool explain = !test && !lint && !best && !args.empty() && args[0] == "--explain";
  if (explain) args.erase(args.begin());
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
      std::vector<nlu::Match> matches = best ? script.match_best(line) : script.match(line);
      for (const std::string& out : nlu_tools::output_lines(matches)) std::cout << out << "\n";
      if (explain && matches.empty()) {
        for (const nlu::Explanation& e : script.explain(line)) {
          std::cout << "  " << e.label << " (" << (e.file.empty() ? script_path : e.file) << ":"
                    << e.line << ") matched \"" << line.substr(0, e.matched) << "\"";
          if (!e.expected.empty()) {
            std::cout << ", then expected";
            for (std::size_t i = 0; i < e.expected.size(); ++i)
              std::cout << (i ? ", " : " ") << e.expected[i];
          }
          std::cout << "\n";
        }
      }
      std::cout.flush();
    }
  } catch (const nlu::CompileError& e) {
    for (const nlu::Diagnostic& d : e.errors())
      std::cerr << (d.file.empty() ? script_path : d.file) << ":" << d.line << ":" << d.column
                << ": " << d.message << "\n";
    return 1;
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
  return 0;
}

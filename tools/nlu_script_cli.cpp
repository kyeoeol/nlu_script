// Reads sentences from stdin (one per line) and prints `label:tagged text` for
// every matching label, or `-` when nothing matches. With --best, prints only
// the most specific labels (Script::match_best). With --explain, a sentence
// that matches nothing is followed by the rules that got furthest into it.
// With --search, each line is searched for rules inside it (Script::search).
// With --save, compiles the script and saves it for loading without
// compiling (Script::save); SCRIPT may be such a file anywhere.
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

// A script file, or a compiled one saved with --save.
nlu::Script open_script(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  char head[4] = {};
  in.read(head, 4);
  if (in.gcount() == 4 && std::string(head, 4) == "\x89NLU") return nlu::Script::load_file(path);
  return nlu::Script::compile_file(path);
}

int usage(const char* program) {
  std::cerr << "usage: " << program << " [--best | --explain | --search] SCRIPT\n"
            << "       " << program << " --test [--best] SCRIPT TESTS...\n"
            << "       " << program << " --lint [--best] [--verbose] SCRIPT\n"
            << "       " << program << " --save SCRIPT OUTPUT\n";
  return 2;
}

int run_tests(const std::string& script_path, const std::vector<std::string>& tests, bool best) {
  nlu::Script script = open_script(script_path);
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

int run_lint(const std::string& script_path, bool best, bool verbose) {
  nlu::LintReport report = open_script(script_path).lint();
  auto file_of = [&](const std::string& file) { return file.empty() ? script_path : file; };
  std::vector<nlu::LintFinding> findings = report.findings(best);
  for (const nlu::LintFinding& f : findings)
    std::cout << file_of(f.file) << ":" << f.line << ": " << f.message << "\n";
  std::size_t resolved = 0;
  for (const nlu::Overlap& o : report.overlaps) {
    if (!best || o.best_label.empty()) continue;
    ++resolved;
    if (verbose)
      std::cout << file_of(o.file) << ":" << o.line << ": note: " << o.label << " and "
                << o.other_label << " overlap, but match_best() always picks " << o.best_label
                << "\n";
  }
  if (verbose) {
    for (const nlu::Overlap& o : report.allowed_overlaps)
      std::cout << file_of(o.file) << ":" << o.line << ": note: " << o.label << " and "
                << o.other_label << " both match \"" << o.example
                << "\" (allowed by a lint comment)\n";
  }
  if (!report.complete)
    std::cout << "note: the automaton is too large to search fully; more overlaps may exist, "
                 "and dead rules were not checked\n";
  std::cout << findings.size() << (findings.size() == 1 ? " finding" : " findings");
  std::vector<std::string> notes;
  if (const std::size_t n = report.allowed_overlaps.size())
    notes.push_back(std::to_string(n) + (n == 1 ? " intended overlap" : " intended overlaps") +
                    " allowed by lint comments");
  if (resolved)
    notes.push_back(std::to_string(resolved) + (resolved == 1 ? " overlap" : " overlaps") +
                    " resolved by match_best()");
  for (std::size_t k = 0; k < notes.size(); ++k) std::cout << (k ? "; " : " (") << notes[k];
  std::cout << (notes.empty() ? "\n" : ")\n");
  return findings.empty() ? 0 : 1;
}

void print_errors(const nlu::CompileError& e, const std::string& script_path) {
  for (const nlu::Diagnostic& d : e.errors())
    std::cerr << (d.file.empty() ? script_path : d.file) << ":" << d.line << ":" << d.column
              << ": " << d.message << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  if (args.size() == 3 && args[0] == "--save") {
    try {
      open_script(args[1]).save_file(args[2]);
      return 0;
    } catch (const nlu::CompileError& e) {
      print_errors(e, args[1]);
    } catch (const std::exception& e) {
      std::cerr << e.what() << "\n";
    }
    return 1;
  }
  const bool test = !args.empty() && args[0] == "--test";
  const bool lint = !args.empty() && args[0] == "--lint";
  if (test || lint) args.erase(args.begin());
  bool best = false, verbose = false;
  while (!args.empty() && (args[0] == "--best" || (lint && args[0] == "--verbose"))) {
    (args[0] == "--best" ? best : verbose) = true;
    args.erase(args.begin());
  }
  const bool explain = !test && !lint && !best && !args.empty() && args[0] == "--explain";
  if (explain) args.erase(args.begin());
  const bool search = !test && !lint && !best && !explain && !args.empty() && args[0] == "--search";
  if (search) args.erase(args.begin());
  if (args.empty() || (test ? args.size() < 2 : args.size() != 1) ||
      args[0].compare(0, 2, "--") == 0)
    return usage(argv[0]);
  const std::string script_path = args[0];
  try {
    if (test) return run_tests(script_path, {args.begin() + 1, args.end()}, best);
    if (lint) return run_lint(script_path, best, verbose);
    nlu::Script script = open_script(script_path);
    std::string line;
    while (std::getline(std::cin, line)) {
      std::vector<nlu::Match> matches =
          search ? script.search(line) : best ? script.match_best(line) : script.match(line);
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
    print_errors(e, script_path);
    return 1;
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
  return 0;
}

// nlu_script: regex script for natural language understanding.
//
// A script maps labels to patterns written in the nlu_script pattern
// language. All rules are compiled into a single automaton, so classifying a
// sentence costs roughly the same regardless of how many rules there are.
// Slots (captured spans with attributes) are extracted only for the rules
// that matched.
#ifndef NLU_SCRIPT_NLU_SCRIPT_HPP
#define NLU_SCRIPT_NLU_SCRIPT_HPP

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace nlu {

// A span of the input captured by a slot, e.g. <cons_datetime>...</cons_datetime>.
struct Slot {
  std::string name;
  std::string text;    // original input text covered by the slot
  std::size_t begin;   // byte offset into the original input
  std::size_t end;     // byte offset one past the last byte of the slot
  // Attributes in the order they were first assigned.
  std::vector<std::pair<std::string, std::string>> attributes;
};

struct Match {
  std::string label;
  std::vector<Slot> slots;  // ordered by the position of their opening tag
  std::string tagged;       // original input with slot tags inserted
  int line;                 // 1-based script line of the rule that matched
  // Input characters, not counting spaces, that the rule matched with
  // pattern text (literals and entities) rather than wildcards.
  int specificity;
  // Script file of the rule: the path given to compile_file, or an included
  // file's path; empty for a script compiled from a string.
  std::string file;
};

// How far one rule got into an input, from Script::explain().
struct Explanation {
  std::string label;
  int line;
  std::string file;     // see Match::file
  std::size_t matched;  // bytes of the input the rule matched before it stopped
  bool complete;        // the rule matches the whole input
  // What the rule could have taken next where it stopped, sorted: characters
  // (lowercase), "space" or "any character".
  std::vector<std::string> expected;
};

// Two labels that both match some input, found by Script::lint().
struct Overlap {
  std::string label;        // the label whose rule comes first in the script
  int line;                 // script line of its first rule that matches `example`
  std::string other_label;
  int other_line;
  std::string example;      // a shortest normalized input that both labels match
  std::string file;         // script files of the two rules (see Match::file)
  std::string other_file;
};

// An entity or variable that no rule uses, directly or through other
// definitions.
struct UnusedDefinition {
  std::string name;  // as written in the script: "@card" or "@var.digit"
  int line;
  std::string file;  // see Match::file
};

// A `# lint:` comment that lint() could not apply.
struct LintDirectiveError {
  int line;
  std::string message;
  std::string file;  // see Match::file
};

struct LintReport {
  std::vector<Overlap> overlaps;          // ordered by line, then other_line
  // Overlaps that a `# lint: allow-overlap` comment marks as intended.
  std::vector<Overlap> allowed_overlaps;  // ordered by line, then other_line
  std::vector<UnusedDefinition> unused;   // ordered by line
  std::vector<LintDirectiveError> directive_errors;  // ordered by line
  bool complete;  // false if the overlap search stopped at its state limit
};

// One error in a script.
struct Diagnostic {
  int line;    // 1-based, 0 if unknown
  int column;  // 1-based, in code points
  std::string message;
  std::string file;  // see Match::file
};

// Thrown by Script::compile with every error found in the script, ordered by
// file (the main script, then included files as they are read) and position.
// what() lists them, one per line ("rules.nlu: line 3, column 5: ..."; the
// file is left out for the main script compiled from a string); line(),
// column() and message() describe the first.
class CompileError : public std::runtime_error {
 public:
  CompileError(const std::string& message, int line, int column);
  explicit CompileError(std::vector<Diagnostic> errors);  // ordered, not empty
  int line() const { return errors_[0].line; }
  int column() const { return errors_[0].column; }
  const std::string& message() const { return errors_[0].message; }
  const std::vector<Diagnostic>& errors() const { return errors_; }

 private:
  std::vector<Diagnostic> errors_;
};

class Script {
 public:
  // Throws CompileError on syntax or reference errors. `include` lines in
  // `source` are relative to the current directory.
  static Script compile(const std::string& source);
  // Like compile(), with `include` lines relative to the file's directory.
  // Throws std::runtime_error if the file cannot be read.
  static Script compile_file(const std::string& path);

  Script(Script&&) noexcept;
  Script& operator=(Script&&) noexcept;
  ~Script();

  // Returns one Match per label whose patterns match the whole input, in the
  // order the labels' first matching rule appears in the script.
  // Safe to call concurrently from multiple threads.
  std::vector<Match> match(const std::string& text) const;

  // Like match(), but scores every matching rule and keeps only the labels
  // whose most specific rule has the highest specificity; each Match comes
  // from that rule. Ties keep every such label, in script order.
  std::vector<Match> match_best(const std::string& text) const;

  std::size_t rule_count() const;

  // For debugging rules: the `limit` rules that got furthest into `text`
  // (rules that match none of it are left out), longest match first, then
  // complete matches, then script order.
  std::vector<Explanation> explain(const std::string& text, std::size_t limit = 3) const;

  // Static checks for script authors: labels that match the same input, and
  // definitions that no rule uses. The overlap search explores the automaton
  // of all rules, up to `max_states` states.
  LintReport lint(std::size_t max_states = 100000) const;

  struct Impl;

 private:
  explicit Script(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace nlu

#endif  // NLU_SCRIPT_NLU_SCRIPT_HPP

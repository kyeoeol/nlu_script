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
};

// Two labels that both match some input, found by Script::lint().
struct Overlap {
  std::string label;        // the label whose rule comes first in the script
  int line;                 // script line of its first rule that matches `example`
  std::string other_label;
  int other_line;
  std::string example;      // a shortest normalized input that both labels match
};

// An entity or variable that no rule uses, directly or through other
// definitions.
struct UnusedDefinition {
  std::string name;  // as written in the script: "@card" or "@var.digit"
  int line;
};

struct LintReport {
  std::vector<Overlap> overlaps;        // ordered by line, then other_line
  std::vector<UnusedDefinition> unused;  // ordered by line
  bool complete;  // false if the overlap search stopped at its state limit
};

class CompileError : public std::runtime_error {
 public:
  CompileError(const std::string& message, int line, int column);
  int line() const { return line_; }      // 1-based, 0 if unknown
  int column() const { return column_; }  // 1-based, in code points
  const std::string& message() const { return message_; }

 private:
  std::string message_;
  int line_;
  int column_;
};

class Script {
 public:
  // Throws CompileError on syntax or reference errors.
  static Script compile(const std::string& source);
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

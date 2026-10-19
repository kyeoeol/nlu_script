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
  // Byte offsets of the matched text: the whole input for match(), the span
  // found for search().
  std::size_t begin = 0;
  std::size_t end = 0;
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
  // The label match_best() returns on every input both labels match, since
  // one of its rules matches each such input without wildcards and the other
  // label always needs one; empty if they may tie.
  std::string best_label;
};

// A rule match() never uses: every input it matches is matched by an earlier
// rule of the same label first, or it matches nothing at all.
struct DeadRule {
  std::string label;
  int line;
  std::string file;
  bool matches_nothing;
  // The earlier rule that matches everything this one does; 0 when it takes
  // several earlier rules together (or matches_nothing).
  int covered_by_line;
  std::string covered_by_file;
};

// One lint finding, as `nlu_script_cli --lint` prints it.
struct LintFinding {
  std::string kind;  // "overlap", "dead-rule", "unused" or "directive"
  std::string file;
  int line;
  std::string message;
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
  std::vector<Overlap> overlaps;          // in script order of their rules
  // Overlaps that a `# lint: allow-overlap` comment marks as intended.
  std::vector<Overlap> allowed_overlaps;  // in script order of their rules
  // Found only when the search is complete.
  std::vector<DeadRule> dead_rules;       // in script order
  std::vector<UnusedDefinition> unused;   // ordered by file and line
  std::vector<LintDirectiveError> directive_errors;  // ordered by file and line
  bool complete;  // false if the search stopped at its state limit
  std::vector<std::string> files;  // script files: the main one, then included ones

  // Every finding, ordered by file and line. With `best`, overlaps that
  // match_best() resolves (best_label set) are left out.
  std::vector<LintFinding> findings(bool best = false) const;
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
// A local date and time; hour and minute are -1 when not given.
struct DateTimeValue {
  int year;
  int month;  // 1-12
  int day;    // 1-31
  int hour = -1;
  int minute = -1;
};

struct DateTimeRange {
  DateTimeValue start;
  DateTimeValue end;  // set when has_end
  bool has_end = false;
  // The finest unit the text gave: "year", "month", "week", "day", "hour" or
  // "minute".
  std::string grain;
};

// Resolves the attributes of a DATETIME slot against `now`, the caller's
// local date and time. Weeks start on Monday. A weekday, a month or a day of
// the month given alone means the next one, today included (금요일, 3월 5일).
// Fields the text leaves out come from the coarser ones: 다음 달 is the 1st
// of next month, 다음 주 its Monday. The end of a range takes the start's date
// when it gives none. Attributes whose values are out of range (month 13,
// offsets beyond 100,000) are ignored, and `now` is clamped into range.
DateTimeRange resolve_datetime(const std::vector<std::pair<std::string, std::string>>& attributes,
                               const DateTimeValue& now);

// The lazily built DFA that classifies inputs, from Script::cache_stats().
struct CacheStats {
  std::size_t states;       // DFA states cached now
  std::size_t transitions;  // cached transitions between them
  std::size_t bytes;        // approximate memory they take
  std::size_t evictions;    // times the cache was full and dropped unused states
};

// Limits of the DFA cache, for Script::set_cache_options(). When either
// limit is reached, the cache keeps the states used since the last eviction
// (up to half of each limit) and drops the rest.
struct CacheOptions {
  std::size_t max_states = 200000;
  std::size_t max_bytes = std::size_t{64} << 20;  // 64 MiB
  // One cache per thread instead of one shared cache: no contention between
  // threads, at the cost of memory per thread.
  bool per_thread = false;
};

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

  // A compiled script as bytes, which load() turns back into the same script
  // without parsing or compiling it again. The bytes are for this format
  // version of the library only (they start with "\x89NLU").
  std::string save() const;
  void save_file(const std::string& path) const;
  // Throws std::runtime_error if `bytes` is not a script saved by save()
  // (checking every count and index, so corrupt data is rejected).
  static Script load(const std::string& bytes);
  static Script load_file(const std::string& path);

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

  // Finds rules inside free text, without <?>*? around them: scanning left
  // to right, takes the longest text any rule matches at each position,
  // reports one Match per label with a rule matching it (`tagged` is that
  // text with its slots, `begin` and `end` its place), and goes on after it.
  // A match neither starts nor ends inside a run of ASCII letters and digits
  // ("card" is not found in "postcard"); in Hangul it may ("카드" in "카드를").
  std::vector<Match> search(const std::string& text) const;

  std::size_t rule_count() const;

  // The state of the DFA cache (the calling thread's with per_thread).
  CacheStats cache_stats() const;
  // Replaces the DFA cache with an empty one with these options. Not safe
  // while other threads match with this script.
  void set_cache_options(const CacheOptions& options);
  CacheOptions cache_options() const;

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

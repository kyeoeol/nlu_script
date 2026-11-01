// Script files: labels, definitions, options, includes and lint comments, and
// which definitions the rules reach.
#ifndef NLU_SCRIPT_SCRIPT_SOURCE_HPP
#define NLU_SCRIPT_SCRIPT_SOURCE_HPP

#include <algorithm>
#include <cstddef>
#include <deque>
#include <set>
#include <string>
#include <vector>

#include "ast.hpp"
#include "compiler.hpp"
#include "nlu_script/nlu_script.hpp"

namespace nlu {
namespace detail {

struct Rule {
  std::string label;
  SourcePos pos;  // line (and file) of the pattern
  NodePtr pattern;
};

// A `# lint: <command> <args...>` comment.
struct LintDirective {
  std::size_t file;  // index into the script's files
  int line;
  int column;
  std::string command;
  std::vector<std::string> args;
};

// Set by `option` lines at the top of the main script.
struct ScriptOptions {
  bool ignore_trailing_punctuation = false;
  bool fold_width = false;
};

struct ParsedScript {
  ScriptOptions options;
  // Script file names, the main script first, then included files in the
  // order they are read. Positions point into this; a deque keeps them put.
  std::deque<std::string> files;
  Definitions defs;
  std::vector<Rule> rules;
  std::vector<LintDirective> lint_directives;
  std::vector<Diagnostic> errors;
  // Definitions with errors in their values; rules using them aren't compiled.
  std::set<std::string> broken_entities, broken_variables;
};

// Parses `source`, read from `file` (empty for a string), and the files it
// includes. Errors are collected in the result instead of thrown.
ParsedScript parse_script(const std::string& source, const std::string& file);

// Whether `pattern` refers, directly or through other definitions, to a
// definition whose values had errors.
bool uses_broken_definition(const Node& pattern, const ParsedScript& parsed);

// Definitions that no rule reaches, directly or through other definitions.
std::vector<UnusedDefinition> unused_definitions(const ParsedScript& parsed);

// The first error of `e`.
Diagnostic diagnostic(const CompileError& e);

// The file of `pos`, or "" for none.
std::string file_name(const SourcePos& pos);

// The index of `file` in `files` (a script's files, the main script first,
// then included files in the order they are read), or files.size() if it
// isn't there. Positions are ordered by file this way.
template <typename Files>
std::size_t file_index(const Files& files, const std::string& file) {
  return static_cast<std::size_t>(std::find(files.begin(), files.end(), file) - files.begin());
}

bool read_file(const std::string& path, std::string& out);

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_SCRIPT_SOURCE_HPP

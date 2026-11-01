// Script files: labels, definitions, options, includes and lint comments.
#include "script_source.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>

#include "parser.hpp"
#include "unicode.hpp"

namespace nlu {
namespace detail {

Diagnostic diagnostic(const CompileError& e) { return e.errors()[0]; }

std::string file_name(const SourcePos& pos) { return pos.file ? *pos.file : std::string(); }

bool read_file(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  out = buffer.str();
  return true;
}

namespace {

// Reads a comment line, at `pos` in the script's file number `file`, as a
// lint directive if its text starts with "lint:".
bool parse_lint_directive(const std::string& comment, std::size_t file, const SourcePos& pos,
                          LintDirective& out) {
  std::istringstream words(comment.substr(comment.find('#') + 1));
  std::string head;
  if (!(words >> head) || head.compare(0, 5, "lint:") != 0) return false;
  out.file = file;
  out.line = pos.line;
  out.column = pos.column;
  out.command = head.substr(5);
  if (out.command.empty()) words >> out.command;
  out.args.clear();
  for (std::string word; words >> word;) out.args.push_back(word);
  return true;
}

std::u32string trim(const std::u32string& s, std::size_t* leading = nullptr) {
  std::size_t b = 0;
  while (b < s.size() && is_space(s[b])) ++b;
  std::size_t e = s.size();
  while (e > b && is_space(s[e - 1])) --e;
  if (leading) *leading = b;
  return s.substr(b, e - b);
}

// --- paths, for include lines -------------------------------------------------
// Plain string handling: std::filesystem is not available on every platform
// the Python wheels target.

bool is_separator(char c) { return c == '/' || c == '\\'; }

bool is_absolute(const std::string& path) {
  return (!path.empty() && is_separator(path[0])) ||
         (path.size() > 2 && path[1] == ':' && is_separator(path[2]));
}

// `path` relative to the directory of `from` (the current directory if
// `from` has none), with "." and ".." segments removed where possible. The
// result uses backslashes if `from` does (Windows paths), slashes otherwise.
std::string resolve(const std::string& from, const std::string& path) {
  const char separator = from.find('\\') != std::string::npos ? '\\' : '/';
  std::string joined = path;
  if (!is_absolute(path)) {
    std::size_t slash = from.find_last_of("/\\");
    if (slash != std::string::npos) joined = from.substr(0, slash + 1) + path;
  }
  std::vector<std::string> parts;
  std::string part;
  const bool absolute = !joined.empty() && is_separator(joined[0]);
  for (std::size_t i = 0; i <= joined.size(); ++i) {
    if (i < joined.size() && !is_separator(joined[i])) {
      part += joined[i];
      continue;
    }
    if (part == "..") {
      if (!parts.empty() && parts.back() != "..") {
        parts.pop_back();
      } else if (!absolute) {
        parts.push_back(part);
      }
    } else if (!part.empty() && part != ".") {
      parts.push_back(part);
    }
    part.clear();
  }
  std::string out = absolute ? std::string(1, separator) : "";
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += separator;
    out += parts[i];
  }
  return out;
}

// Compares paths whichever separators they use.
std::string path_key(std::string path) {
  for (char& c : path)
    if (c == '\\') c = '/';
  return path;
}

// --- script source ------------------------------------------------------------

// Parses a script and the files it includes, collecting errors instead of
// stopping at the first. A broken label or definition line hides the lines
// indented under it, so one mistake is reported once. Each file is read
// once, so including a file twice (or in a cycle) is harmless.
class ScriptParser {
 public:
  ParsedScript parse(const std::string& source, const std::string& main_file) {
    out_.files.push_back(main_file);
    if (!main_file.empty()) read_.insert(path_key(resolve("", main_file)));
    parse_file(source, 0);
    finish(out_.defs.entities, entity_values_, out_.broken_entities, "entity @");
    finish(out_.defs.variables, variable_values_, out_.broken_variables, "variable @var.");
    return std::move(out_);
  }

 private:
  enum class Block { None, Label, Entity, Variable, Broken };

  ParsedScript out_;
  std::map<std::string, std::vector<NodePtr>> entity_values_;
  std::map<std::string, std::vector<NodePtr>> variable_values_;
  std::set<std::string> read_;  // resolved paths of the files read so far
  bool content_ = false;         // a line other than an option was read

  // State of the file being parsed.
  struct File {
    const std::string* name;
    Block block = Block::None;
    std::string current;  // the label or definition being read
  };

  // Parses out_.files[index], whose text is `source`.
  void parse_file(const std::string& source, std::size_t index) {
    const std::string* name = &out_.files[index];
    File f{name, Block::None, ""};
    std::istringstream lines(source);
    std::string raw;
    int line_no = 0;
    while (std::getline(lines, raw)) {
      ++line_no;
      if (!raw.empty() && raw.back() == '\r') raw.pop_back();
      std::vector<DecodedChar> chars = decode_utf8(raw);
      normalize_nfc(chars);
      std::u32string line;
      for (const DecodedChar& d : chars) line += d.code;
      std::size_t first = 0;
      while (first < line.size() && is_space(line[first])) ++first;
      if (first == line.size()) continue;
      const SourcePos pos{line_no, static_cast<int>(first) + 1, name};
      if (line[first] == U'#') {
        LintDirective directive;
        if (parse_lint_directive(raw, index, pos, directive))
          out_.lint_directives.push_back(std::move(directive));
        continue;
      }
      try {
        parse_line(f, line, first, pos);
      } catch (const CompileError& e) {
        out_.errors.push_back(diagnostic(e));
        if (f.block == Block::Entity) out_.broken_entities.insert(f.current);
        if (f.block == Block::Variable) out_.broken_variables.insert(f.current);
        if (f.block == Block::None) f.block = Block::Broken;
      }
    }
  }

  // `option name` lines come before everything else in the main script, since
  // they change how the patterns after them are read.
  void read_option(const std::u32string& line, const SourcePos& pos) {
    if (content_) throw_at(pos, "option lines must come before rules and definitions");
    std::size_t leading = 0;
    const std::u32string rest = trim(line.substr(6), &leading);
    const std::size_t name_end = std::find_if(rest.begin(), rest.end(), is_space) - rest.begin();
    const std::string name = to_utf8(rest.substr(0, name_end));
    if (name_end == rest.size() && name == "ignore-trailing-punctuation") {
      out_.options.ignore_trailing_punctuation = true;
    } else if (name_end == rest.size() && name == "fold-width") {
      out_.options.fold_width = true;
    } else {
      throw_at(SourcePos{pos.line, 7 + static_cast<int>(leading), pos.file},
               "unknown option '" + name + "'; available: ignore-trailing-punctuation, fold-width");
    }
  }

  // Parses one non-blank, non-comment line whose text starts at `first`.
  // Header lines set the block to Broken until they are known to be valid.
  void parse_line(File& f, const std::u32string& line, std::size_t first, const SourcePos& pos) {
    const bool option =
        first == 0 && starts_with(line, 0, U"option") && line.size() > 6 && is_space(line[6]);
    if (!option) content_ = true;
    if (first > 0) {
      if (f.block == Block::Broken) return;
      std::u32string content = trim(line);
      if (f.block == Block::None)
        throw_at(pos, "indented pattern outside of a label or definition");
      if (f.block == Block::Label) {
        out_.rules.push_back({f.current, pos, parse_pattern(content, pos, out_.options.fold_width)});
      } else {
        add_values(f, content, pos);
      }
      return;
    }

    f.block = Block::Broken;
    if (option) {
      if (pos.file != &out_.files.front())
        throw_at(pos, "option lines belong in the main script, not an included file");
      read_option(line, pos);
      f.block = Block::None;
      return;
    }
    if (starts_with(line, 0, U"include") && line.size() > 7 && is_space(line[7])) {
      std::size_t lead = 0;
      std::u32string path = trim(line.substr(7), &lead);
      if (path.size() >= 2 && path.front() == U'"' && path.back() == U'"')
        path = path.substr(1, path.size() - 2);
      include(to_utf8(path), SourcePos{pos.line, 8 + static_cast<int>(lead), pos.file});
      f.block = Block::None;
      return;
    }
    if (line[0] == U'@') {
      const bool is_var = starts_with(line, 0, U"@var.");
      const std::size_t name_at = is_var ? 5 : 1;
      std::size_t colon = line.find(U':');
      if (colon == std::u32string::npos) throw_at(pos, "expected ':' after the definition name");
      std::u32string name32 = trim(line.substr(name_at, colon - name_at));
      bool valid = !name32.empty();
      for (char32_t c : name32) valid = valid && is_name_char(c);
      if (!valid)
        throw_at(SourcePos{pos.line, pos.column + static_cast<int>(name_at), pos.file},
                 "invalid definition name");
      f.current = to_utf8(name32);
      auto& table = is_var ? out_.defs.variables : out_.defs.entities;
      if (table.count(f.current))
        throw_at(pos, std::string(is_var ? "variable @var." : "entity @") + f.current +
                          " is already defined");
      table[f.current].pos = pos;
      f.block = is_var ? Block::Variable : Block::Entity;
      std::size_t lead = 0;
      std::u32string body = trim(line.substr(colon + 1), &lead);
      if (!body.empty())
        add_values(f, body, SourcePos{pos.line, static_cast<int>(colon + 2 + lead), pos.file});
      return;
    }

    std::u32string label = trim(line);
    for (char32_t c : label)
      if (is_space(c))
        throw_at(pos, "labels cannot contain spaces; indent patterns under their label");
    f.current = to_utf8(label);
    f.block = Block::Label;
  }

  // A variable takes one pattern per line, an entity a comma-separated list
  // of values. `pos` locates text[0].
  void add_values(const File& f, const std::u32string& text, const SourcePos& pos) {
    if (f.block == Block::Variable) {
      variable_values_[f.current].push_back(parse_pattern(text, pos, out_.options.fold_width));
      return;
    }
    for (const auto& part : split_top_level_commas(text)) {
      std::size_t lead = 0;
      std::u32string value = trim(part.second, &lead);
      const SourcePos at{pos.line, pos.column + static_cast<int>(part.first + lead), pos.file};
      if (value.empty()) throw_at(at, "empty entity value");
      entity_values_[f.current].push_back(parse_pattern(value, at, out_.options.fold_width));
    }
  }

  void include(const std::string& path, const SourcePos& pos) {
    if (path.empty()) throw_at(pos, "expected a file name after include");
    const std::string resolved = resolve(*pos.file, path);
    if (!read_.insert(path_key(resolved)).second) return;  // read already
    std::string source;
    if (!read_file(resolved, source)) throw_at(pos, "cannot open included file " + resolved);
    out_.files.push_back(resolved);
    parse_file(source, out_.files.size() - 1);
  }

  void finish(std::map<std::string, Definition>& table,
              std::map<std::string, std::vector<NodePtr>>& values, std::set<std::string>& broken,
              const char* kind) {
    for (auto& kv : table) {
      if (broken.count(kv.first)) continue;  // its errors are reported already
      std::vector<NodePtr>& options = values[kv.first];
      if (options.empty()) {
        out_.errors.push_back({kv.second.pos.line, kv.second.pos.column,
                               std::string(kind) + kv.first + " has no values",
                               file_name(kv.second.pos)});
        broken.insert(kv.first);
        continue;
      }
      if (options.size() == 1) {
        kv.second.body = options[0];
      } else {
        auto choice = std::make_shared<Node>();
        choice->kind = Node::Kind::Choice;
        choice->pos = kv.second.pos;
        choice->children = std::move(options);
        kv.second.body = choice;
      }
    }
  }
};

// Orders positions by file (the main script, then included files as they are
// read) and then by line and column.
std::tuple<std::size_t, int, int> position_key(const ParsedScript& parsed, const SourcePos& pos) {
  return {file_index(parsed.files, file_name(pos)), pos.line, pos.column};
}

// The entities and variables that `roots` refer to, directly or through
// other definitions.
struct Reached {
  std::set<std::string> entities, variables;
};

Reached reachable_definitions(std::vector<const Node*> pending, const ParsedScript& parsed) {
  Reached out;
  while (!pending.empty()) {
    const Node* n = pending.back();
    pending.pop_back();
    const bool entity = n->kind == Node::Kind::EntityRef;
    if (entity || n->kind == Node::Kind::VarRef) {
      const auto& table = entity ? parsed.defs.entities : parsed.defs.variables;
      auto it = table.find(n->name);
      if ((entity ? out.entities : out.variables).insert(n->name).second && it != table.end() &&
          it->second.body)
        pending.push_back(it->second.body.get());
    }
    for (const NodePtr& child : n->children) pending.push_back(child.get());
    if (n->exclusion) pending.push_back(n->exclusion.get());
  }
  return out;
}

}  // namespace

ParsedScript parse_script(const std::string& source, const std::string& file) {
  return ScriptParser().parse(source, file);
}

bool uses_broken_definition(const Node& pattern, const ParsedScript& parsed) {
  if (parsed.broken_entities.empty() && parsed.broken_variables.empty()) return false;
  const Reached reached = reachable_definitions({&pattern}, parsed);
  auto any_broken = [](const std::set<std::string>& used, const std::set<std::string>& broken) {
    return std::any_of(used.begin(), used.end(),
                       [&](const std::string& name) { return broken.count(name) > 0; });
  };
  return any_broken(reached.entities, parsed.broken_entities) ||
         any_broken(reached.variables, parsed.broken_variables);
}

std::vector<UnusedDefinition> unused_definitions(const ParsedScript& parsed) {
  std::vector<const Node*> roots;
  for (const Rule& rule : parsed.rules) roots.push_back(rule.pattern.get());
  const Reached reached = reachable_definitions(std::move(roots), parsed);
  const std::set<std::string>& entities = reached.entities;
  const std::set<std::string>& variables = reached.variables;

  std::vector<std::pair<SourcePos, std::string>> found;
  for (const auto& kv : parsed.defs.entities)
    if (!entities.count(kv.first)) found.emplace_back(kv.second.pos, "@" + kv.first);
  for (const auto& kv : parsed.defs.variables)
    if (!variables.count(kv.first)) found.emplace_back(kv.second.pos, "@var." + kv.first);
  std::sort(found.begin(), found.end(), [&](const auto& a, const auto& b) {
    return position_key(parsed, a.first) < position_key(parsed, b.first);
  });
  std::vector<UnusedDefinition> out;
  for (const auto& f : found) out.push_back({f.second, f.first.line, file_name(f.first)});
  return out;
}

}  // namespace detail
}  // namespace nlu

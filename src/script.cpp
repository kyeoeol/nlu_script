// Script parsing, compilation and matching.
//
// Matching runs in two phases:
//  1. A lazily built DFA over the union of all rules finds which rules accept
//     the whole input. Its cost per character does not grow with the number
//     of rules once the relevant DFA states are cached.
//  2. For each accepted rule, a Pike VM over that rule's NFA replays the
//     input with priorities (greedy groups, lazy wildcards) to recover slot
//     spans and attribute assignments.
#include <algorithm>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>

#include "compiler.hpp"
#include "datetime.hpp"
#include "lint.hpp"
#include "numbers.hpp"
#include "nlu_script/nlu_script.hpp"
#include "parser.hpp"
#include "unicode.hpp"

namespace nlu {

using namespace detail;

namespace {

std::string describe(const std::vector<Diagnostic>& errors) {
  std::string out;
  for (const Diagnostic& d : errors) {
    if (!out.empty()) out += "\n";
    if (!d.file.empty()) out += d.file + ": ";
    if (d.line > 0)
      out += "line " + std::to_string(d.line) + ", column " + std::to_string(d.column) + ": ";
    out += d.message;
  }
  return out;
}

}  // namespace

CompileError::CompileError(const std::string& message, int line, int column)
    : CompileError(std::vector<Diagnostic>{{line, column, message, ""}}) {}

CompileError::CompileError(std::vector<Diagnostic> errors)
    : std::runtime_error(describe(errors)), errors_(std::move(errors)) {}

namespace {

// --- script source ----------------------------------------------------------

struct Rule {
  std::string label;
  SourcePos pos;  // line (and file) of the pattern
  NodePtr pattern;
};

// A `# lint: <command> <args...>` comment.
struct LintDirective {
  SourcePos pos;
  std::string command;
  std::vector<std::string> args;
};

struct ParsedScript {
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

Diagnostic diagnostic(const CompileError& e) { return e.errors()[0]; }

std::string file_name(const SourcePos& pos) { return pos.file ? *pos.file : std::string(); }

// Reads a comment line as a lint directive if its text starts with "lint:".
bool parse_lint_directive(const std::string& comment, const SourcePos& pos, LintDirective& out) {
  std::istringstream words(comment.substr(comment.find('#') + 1));
  std::string head;
  if (!(words >> head) || head.compare(0, 5, "lint:") != 0) return false;
  out.pos = pos;
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

bool starts_with(const std::u32string& s, const char32_t* prefix) {
  for (std::size_t i = 0; prefix[i]; ++i)
    if (i >= s.size() || s[i] != prefix[i]) return false;
  return true;
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

bool read_file(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  out = buffer.str();
  return true;
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
    parse_file(source, &out_.files.back());
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

  // State of the file being parsed.
  struct File {
    const std::string* name;
    Block block = Block::None;
    std::string current;  // the label or definition being read
  };

  void parse_file(const std::string& source, const std::string* name) {
    File f{name, Block::None, ""};
    std::istringstream lines(source);
    std::string raw;
    int line_no = 0;
    while (std::getline(lines, raw)) {
      ++line_no;
      if (!raw.empty() && raw.back() == '\r') raw.pop_back();
      std::vector<DecodedChar> chars = decode_utf8(raw);
      compose_hangul(chars);
      std::u32string line;
      for (const DecodedChar& d : chars) line += d.code;
      std::size_t first = 0;
      while (first < line.size() && is_space(line[first])) ++first;
      if (first == line.size()) continue;
      const SourcePos pos{line_no, static_cast<int>(first) + 1, name};
      if (line[first] == U'#') {
        LintDirective directive;
        if (parse_lint_directive(raw, pos, directive))
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

  // Parses one non-blank, non-comment line whose text starts at `first`.
  // Header lines set the block to Broken until they are known to be valid.
  void parse_line(File& f, const std::u32string& line, std::size_t first, const SourcePos& pos) {
    if (first > 0) {
      if (f.block == Block::Broken) return;
      std::u32string content = trim(line);
      if (f.block == Block::None)
        throw_at(pos, "indented pattern outside of a label or definition");
      if (f.block == Block::Label) {
        out_.rules.push_back({f.current, pos, parse_pattern(content, pos)});
      } else {
        add_values(f, content, pos);
      }
      return;
    }

    f.block = Block::Broken;
    if (starts_with(line, U"include") && line.size() > 7 && is_space(line[7])) {
      std::size_t lead = 0;
      std::u32string path = trim(line.substr(7), &lead);
      if (path.size() >= 2 && path.front() == U'"' && path.back() == U'"')
        path = path.substr(1, path.size() - 2);
      include(to_utf8(path), SourcePos{pos.line, 8 + static_cast<int>(lead), pos.file});
      f.block = Block::None;
      return;
    }
    if (line[0] == U'@') {
      const bool is_var = starts_with(line, U"@var.");
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
      variable_values_[f.current].push_back(parse_pattern(text, pos));
      return;
    }
    for (const auto& part : split_top_level_commas(text)) {
      std::size_t lead = 0;
      std::u32string value = trim(part.second, &lead);
      const SourcePos at{pos.line, pos.column + static_cast<int>(part.first + lead), pos.file};
      if (value.empty()) throw_at(at, "empty entity value");
      entity_values_[f.current].push_back(parse_pattern(value, at));
    }
  }

  void include(const std::string& path, const SourcePos& pos) {
    if (path.empty()) throw_at(pos, "expected a file name after include");
    const std::string resolved = resolve(*pos.file, path);
    if (!read_.insert(path_key(resolved)).second) return;  // read already
    std::string source;
    if (!read_file(resolved, source)) throw_at(pos, "cannot open included file " + resolved);
    out_.files.push_back(resolved);
    parse_file(source, &out_.files.back());
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

// --- phase 1: lazy DFA ------------------------------------------------------

// Threads share one cache. Lookups of cached transitions run under a shared
// lock; a thread that needs a new transition switches to the exclusive lock
// for the rest of its input.
class LazyDfa {
 public:
  LazyDfa(const Nfa& nfa, int start) : nfa_(nfa), closure_(nfa), start_nfa_(start) { reset(); }

  // Returns the ids of the rules accepting the whole text, ascending.
  std::vector<int> accepted_rules(const std::u32string& text) {
    int d;
    std::size_t i = 0;
    unsigned epoch;
    {
      std::shared_lock<std::shared_mutex> lock(mutex_);
      d = start_;
      for (; i < text.size(); ++i) {
        auto it = states_[d].next.find(text[i]);
        if (it == states_[d].next.end()) break;
        d = it->second;
        if (states_[d].set.empty()) return {};
      }
      if (i == text.size()) return states_[d].accepts;
      epoch = epoch_;
    }
    std::lock_guard<std::shared_mutex> lock(mutex_);
    if (epoch != epoch_) {  // the cache was reset, so `d` is stale
      d = start_;
      i = 0;
    }
    for (; i < text.size(); ++i) {
      char32_t c = text[i];
      auto it = states_[d].next.find(c);
      if (it != states_[d].next.end()) {
        d = it->second;
      } else {
        std::vector<int> targets;
        for (int s : states_[d].set)
          for (const Edge& e : nfa_.states[s].edges)
            if (e.consumes() && nfa_.matches(e, c)) targets.push_back(e.to);
        std::vector<int> set = closure_(std::move(targets), c);
        if (states_.size() >= kMaxStates) {
          reset();
          d = intern(std::move(set));
        } else {
          int next = intern(std::move(set));
          states_[d].next.emplace(c, next);
          d = next;
        }
      }
      if (states_[d].set.empty()) return {};
    }
    return states_[d].accepts;
  }

 private:
  static constexpr std::size_t kMaxStates = 20000;

  struct DState {
    std::vector<int> set;      // NFA states with consuming edges or accepting
    std::vector<int> accepts;  // accepted rules
    std::unordered_map<char32_t, int> next;
  };

  const Nfa& nfa_;
  std::shared_mutex mutex_;
  // Everything below is guarded by mutex_.
  Closure closure_;
  int start_nfa_;
  int start_ = 0;
  unsigned epoch_ = 0;  // bumped by reset(), which invalidates state ids
  std::vector<DState> states_;
  std::map<std::vector<int>, int> index_;

  void reset() {
    states_.clear();
    index_.clear();
    ++epoch_;
    start_ = intern(closure_({start_nfa_}, 0));
  }

  int intern(std::vector<int> set) {
    auto it = index_.find(set);
    if (it != index_.end()) return it->second;
    DState d;
    for (int s : set)
      if (nfa_.states[s].accept_rule >= 0) d.accepts.push_back(nfa_.states[s].accept_rule);
    std::sort(d.accepts.begin(), d.accepts.end());
    d.set = set;
    int id = static_cast<int>(states_.size());
    states_.push_back(std::move(d));
    index_.emplace(std::move(set), id);
    return id;
  }
};

// --- phase 2: Pike VM -------------------------------------------------------

struct LogNode {
  int action;
  int pos;
  std::shared_ptr<const LogNode> prev;
};
using Log = std::shared_ptr<const LogNode>;

struct Event {
  int action;
  int pos;
};

class PikeVm {
 public:
  // A rule's states occupy the contiguous id range [first, last).
  PikeVm(const Nfa& nfa, int first, int last)
      : nfa_(nfa), first_(first), mark_(static_cast<std::size_t>(last - first), 0) {}

  // Runs the rule over the text and returns the actions on the highest
  // priority accepting path, and how many characters wildcards consumed on
  // it. Returns false if the rule does not match.
  bool run(int start, const std::u32string& text, std::vector<Event>& events, int& wildcard_chars) {
    std::vector<Thread> current;
    std::vector<Thread> next;
    add(current, start, nullptr, 0, 0, 0);
    for (std::size_t i = 0; i < text.size() && !current.empty(); ++i) {
      next.clear();
      ++generation_;
      for (const Thread& t : current) {
        if (t.edge < 0) continue;
        const Edge& e = nfa_.states[t.state].edges[t.edge];
        if (nfa_.matches(e, text[i]))
          add(next, e.to, t.log, static_cast<int>(i) + 1, text[i],
              t.wild + (e.kind == Edge::Kind::Any ? 1 : 0));
      }
      std::swap(current, next);
    }
    for (const Thread& t : current) {
      if (t.edge >= 0) continue;
      events.clear();
      for (const LogNode* n = t.log.get(); n; n = n->prev.get()) events.push_back({n->action, n->pos});
      std::reverse(events.begin(), events.end());
      wildcard_chars = t.wild;
      return true;
    }
    return false;
  }

 private:
  struct Thread {
    int state;
    int edge;  // consuming edge to follow, or -1 for an accepting thread
    Log log;
    int wild;  // characters consumed by wildcards so far
  };
  struct Item {
    bool visit;  // true: explore `state`; false: emit a thread
    int state;
    int edge;
    Log log;
  };

  const Nfa& nfa_;
  int first_;
  std::vector<unsigned> mark_;
  unsigned generation_ = 1;

  // Depth-first, in edge order, so threads are listed by priority. `prev` is
  // the character consumed last, which guard edges test.
  void add(std::vector<Thread>& list, int state, Log log, int pos, char32_t prev, int wild) {
    std::vector<Item> stack;
    stack.push_back({true, state, -1, std::move(log)});
    std::vector<Item> children;
    while (!stack.empty()) {
      Item item = std::move(stack.back());
      stack.pop_back();
      if (!item.visit) {
        list.push_back({item.state, item.edge, std::move(item.log), wild});
        continue;
      }
      unsigned& mark = mark_[item.state - first_];
      if (mark == generation_) continue;
      mark = generation_;
      const State& st = nfa_.states[item.state];
      children.clear();
      for (std::size_t k = 0; k < st.edges.size(); ++k) {
        const Edge& e = st.edges[k];
        if (e.kind == Edge::Kind::Epsilon) {
          children.push_back({true, e.to, -1, item.log});
        } else if (e.kind == Edge::Kind::Guard) {
          if (guard_holds(e.guard, prev)) children.push_back({true, e.to, -1, item.log});
        } else if (e.kind == Edge::Kind::Action) {
          children.push_back({true, e.to, -1, std::make_shared<const LogNode>(LogNode{e.action, pos, item.log})});
        } else {
          children.push_back({false, item.state, static_cast<int>(k), item.log});
        }
      }
      if (st.accept_rule >= 0) children.push_back({false, item.state, -1, item.log});
      for (auto it = children.rbegin(); it != children.rend(); ++it) stack.push_back(std::move(*it));
    }
  }
};

// --- building results -------------------------------------------------------

struct Instance {
  int slot;
  int open_pos;
  int close_pos;
  std::size_t open_event;
  std::vector<std::pair<std::string, std::string>> attributes;
  Normalizer normalizer;
};

void set_attribute(Instance& inst, const std::string& key, const std::string& value) {
  for (auto& kv : inst.attributes) {
    if (kv.first == key) {
      kv.second = value;
      return;
    }
  }
  inst.attributes.emplace_back(key, value);
}

std::string escape_attribute(const std::string& value) {
  std::string out;
  for (char c : value) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

Match build_match(const Nfa& nfa, const std::string& label, int line, int specificity,
                  const std::vector<Event>& events, const std::string& original,
                  const NormalizedText& norm) {
  std::vector<Instance> instances;
  std::vector<int> event_instance(events.size(), -1);
  std::vector<int> open;
  std::vector<std::size_t> deferred;  // qualified assignments
  for (std::size_t i = 0; i < events.size(); ++i) {
    const Action& a = nfa.actions[events[i].action];
    switch (a.kind) {
      case Action::Kind::Open:
        instances.push_back({a.slot, events[i].pos, events[i].pos, i, {}, a.normalizer});
        open.push_back(static_cast<int>(instances.size()) - 1);
        event_instance[i] = open.back();
        break;
      case Action::Kind::Close:
        instances[open.back()].close_pos = events[i].pos;
        event_instance[i] = open.back();
        open.pop_back();
        break;
      case Action::Kind::Assign:
        if (a.slot < 0) {
          set_attribute(instances[open.back()], a.key, a.value);
        } else {
          deferred.push_back(i);
        }
        break;
    }
  }
  // {slot.key=value} targets the latest such slot opened before it, or else
  // the first one opened after it.
  for (std::size_t i : deferred) {
    const Action& a = nfa.actions[events[i].action];
    int target = -1;
    for (std::size_t k = 0; k < instances.size(); ++k) {
      if (instances[k].slot != a.slot) continue;
      if (instances[k].open_event < i || target < 0) target = static_cast<int>(k);
      if (instances[k].open_event > i) break;
    }
    if (target >= 0) set_attribute(instances[target], a.key, a.value);
  }

  const std::size_t n = norm.text.size();
  auto byte_at = [&](int pos) -> std::size_t {
    if (static_cast<std::size_t>(pos) < n) return norm.begin[pos];
    return n ? norm.end[n - 1] : 0;
  };
  auto open_byte = [&](const Instance& inst) { return byte_at(inst.open_pos); };
  auto close_byte = [&](const Instance& inst) {
    return inst.close_pos > inst.open_pos ? norm.end[inst.close_pos - 1] : open_byte(inst);
  };

  std::string value;
  for (Instance& inst : instances) {
    const std::size_t begin = open_byte(inst);
    const std::string text = original.substr(begin, close_byte(inst) - begin);
    if (inst.normalizer == Normalizer::KoreanNumber && korean_number_value(text, value))
      set_attribute(inst, "value", value);
    if (inst.normalizer == Normalizer::DateTime)
      for (const auto& kv : datetime_attributes(text)) set_attribute(inst, kv.first, kv.second);
  }

  Match m;
  m.label = label;
  m.line = line;
  m.specificity = specificity;
  for (const Instance& inst : instances) {
    Slot s;
    s.name = nfa.slot_names[inst.slot];
    s.begin = open_byte(inst);
    s.end = close_byte(inst);
    s.text = original.substr(s.begin, s.end - s.begin);
    s.attributes = inst.attributes;
    m.slots.push_back(std::move(s));
  }

  std::size_t cursor = 0;
  for (std::size_t i = 0; i < events.size(); ++i) {
    if (event_instance[i] < 0) continue;
    const Instance& inst = instances[event_instance[i]];
    const bool is_open = nfa.actions[events[i].action].kind == Action::Kind::Open;
    std::size_t at = is_open ? open_byte(inst) : close_byte(inst);
    m.tagged.append(original, cursor, at - cursor);
    cursor = at;
    const std::string& name = nfa.slot_names[inst.slot];
    if (is_open) {
      m.tagged += "<" + name;
      for (const auto& kv : inst.attributes)
        m.tagged += " " + kv.first + "=\"" + escape_attribute(kv.second) + "\"";
      m.tagged += ">";
    } else {
      m.tagged += "</" + name + ">";
    }
  }
  m.tagged.append(original, cursor, std::string::npos);
  return m;
}

// Orders positions by file (the main script, then included files as they are
// read) and then by line and column.
std::tuple<std::size_t, int, int> position_key(const ParsedScript& parsed, const SourcePos& pos) {
  std::size_t file = 0;
  while (file < parsed.files.size() && &parsed.files[file] != pos.file) ++file;
  return {pos.file ? file : 0, pos.line, pos.column};
}

// Whether `pattern` refers, directly or through other definitions, to a
// definition whose values had errors.
bool uses_broken_definition(const Node& pattern, const ParsedScript& parsed) {
  std::set<std::string> entities, variables;
  std::vector<const Node*> pending{&pattern};
  while (!pending.empty()) {
    const Node* n = pending.back();
    pending.pop_back();
    const bool entity = n->kind == Node::Kind::EntityRef;
    if (entity || n->kind == Node::Kind::VarRef) {
      const auto& broken = entity ? parsed.broken_entities : parsed.broken_variables;
      if (broken.count(n->name)) return true;
      const auto& table = entity ? parsed.defs.entities : parsed.defs.variables;
      auto it = table.find(n->name);
      if (it != table.end() && it->second.body &&
          (entity ? entities : variables).insert(n->name).second)
        pending.push_back(it->second.body.get());
    }
    for (const NodePtr& child : n->children) pending.push_back(child.get());
    if (n->exclusion) pending.push_back(n->exclusion.get());
  }
  return false;
}

// --- lint ---------------------------------------------------------------------

// Definitions that no rule reaches, directly or through other definitions.
std::vector<UnusedDefinition> unused_definitions(const ParsedScript& parsed) {
  std::set<std::string> entities, variables;
  std::vector<const Node*> pending;
  for (const Rule& rule : parsed.rules) pending.push_back(rule.pattern.get());
  auto use = [&](std::set<std::string>& used, const std::map<std::string, Definition>& table,
                 const std::string& name) {
    auto it = table.find(name);
    if (it != table.end() && it->second.body && used.insert(name).second)
      pending.push_back(it->second.body.get());
  };
  while (!pending.empty()) {
    const Node* n = pending.back();
    pending.pop_back();
    if (n->kind == Node::Kind::EntityRef) use(entities, parsed.defs.entities, n->name);
    if (n->kind == Node::Kind::VarRef) use(variables, parsed.defs.variables, n->name);
    for (const NodePtr& child : n->children) pending.push_back(child.get());
    if (n->exclusion) pending.push_back(n->exclusion.get());
  }

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

}  // namespace

// --- Script -----------------------------------------------------------------

struct CompiledRule {
  std::string label;
  int line;
  std::string file;
  int start;  // start state
  int first;  // the rule's states occupy the id range [first, last)
  int last;
};

struct Script::Impl {
  Nfa nfa;
  std::vector<CompiledRule> rules;
  std::vector<UnusedDefinition> unused;
  std::deque<std::string> files;  // what lint_directives' positions point into
  std::vector<LintDirective> lint_directives;
  int start = 0;
  mutable std::unique_ptr<LazyDfa> dfa;

  std::vector<Match> match(const std::string& text, bool best) const;
};

Script::Script(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Script::Script(Script&&) noexcept = default;
Script& Script::operator=(Script&&) noexcept = default;
Script::~Script() = default;

// The automaton of a whole script stays below this many states (about 1.3 GB),
// so a script can't exhaust memory with many large rules.
constexpr std::size_t kMaxScriptStates = 10000000;

namespace {

// Compiles `source`, read from `file` (empty for a string).
std::unique_ptr<Script::Impl> compile_source(const std::string& source, const std::string& file) {
  ParsedScript parsed = ScriptParser().parse(source, file);
  auto impl = std::make_unique<Script::Impl>();
  impl->start = impl->nfa.add_state();
  std::vector<Diagnostic> errors = std::move(parsed.errors);
  for (const Rule& rule : parsed.rules) {
    if (impl->nfa.states.size() > kMaxScriptStates) {
      errors.push_back({rule.pos.line, 1,
                        "script is too large to compile (over " +
                            std::to_string(kMaxScriptStates) + " automaton states)",
                        file_name(rule.pos)});
      break;
    }
    if (uses_broken_definition(*rule.pattern, parsed)) continue;
    CompiledRule r;
    r.label = rule.label;
    r.line = rule.pos.line;
    r.file = file_name(rule.pos);
    r.first = static_cast<int>(impl->nfa.states.size());
    try {
      r.start = compile_rule(impl->nfa, parsed.defs, *rule.pattern,
                             static_cast<int>(impl->rules.size()));
    } catch (const CompileError& e) {
      errors.push_back(diagnostic(e));
      continue;
    }
    r.last = static_cast<int>(impl->nfa.states.size());
    impl->nfa.epsilon(impl->start, r.start);
    impl->rules.push_back(std::move(r));
  }
  if (!errors.empty()) {
    // Ordered by file and position; an error inside a definition is reported
    // once, however many rules use it.
    auto key = [&](const Diagnostic& d) {
      std::size_t file = 0;
      while (file < parsed.files.size() && parsed.files[file] != d.file) ++file;
      return std::make_tuple(file, d.line, d.column, d.message);
    };
    std::sort(errors.begin(), errors.end(),
              [&](const Diagnostic& a, const Diagnostic& b) { return key(a) < key(b); });
    errors.erase(std::unique(errors.begin(), errors.end(),
                             [&](const Diagnostic& a, const Diagnostic& b) {
                               return key(a) == key(b);
                             }),
                 errors.end());
    throw CompileError(std::move(errors));
  }
  impl->unused = unused_definitions(parsed);
  // Moving a deque keeps its elements in place, so the directives' file
  // pointers stay valid.
  impl->files = std::move(parsed.files);
  impl->lint_directives = std::move(parsed.lint_directives);
  impl->dfa = std::make_unique<LazyDfa>(impl->nfa, impl->start);
  return impl;
}

}  // namespace

Script Script::compile(const std::string& source) { return Script(compile_source(source, "")); }

Script Script::compile_file(const std::string& path) {
  std::string source;
  if (!read_file(path, source)) throw std::runtime_error("cannot open " + path);
  return Script(compile_source(source, path));
}

std::size_t Script::rule_count() const { return impl_->rules.size(); }

std::vector<Explanation> Script::explain(const std::string& text, std::size_t limit) const {
  const Impl& impl = *impl_;
  const NormalizedText norm = normalize(text);
  // The rule owning each NFA state: rules occupy contiguous, ascending ranges.
  auto rule_of = [&](int state) {
    auto it = std::upper_bound(impl.rules.begin(), impl.rules.end(), state,
                               [](int s, const CompiledRule& r) { return s < r.first; });
    if (it == impl.rules.begin()) return -1;
    --it;
    return state < it->last ? static_cast<int>(it - impl.rules.begin()) : -1;
  };

  // One pass over the input with every rule at once. For each rule, keep
  // the furthest position where it was still alive and its states there.
  const std::size_t n = impl.rules.size();
  std::vector<std::size_t> furthest(n, 0);
  std::vector<std::vector<int>> stopped(n);
  std::vector<bool> complete(n, false);
  Closure closure(impl.nfa);
  std::vector<int> states = closure({impl.start}, 0);
  for (std::size_t i = 0;; ++i) {
    std::vector<std::vector<int>> by_rule(n);
    for (int s : states) {
      int r = rule_of(s);
      if (r < 0) continue;
      by_rule[r].push_back(s);
      if (i == norm.text.size() && impl.nfa.states[s].accept_rule == r) complete[r] = true;
    }
    for (std::size_t r = 0; r < n; ++r) {
      if (by_rule[r].empty() || i == 0) continue;
      furthest[r] = i;
      stopped[r] = std::move(by_rule[r]);
    }
    if (i == norm.text.size()) break;
    std::vector<int> targets;
    for (int s : states)
      for (const Edge& e : impl.nfa.states[s].edges)
        if (e.consumes() && impl.nfa.matches(e, norm.text[i])) targets.push_back(e.to);
    if (targets.empty()) break;
    states = closure(std::move(targets), norm.text[i]);
  }

  std::vector<std::size_t> order;
  for (std::size_t r = 0; r < n; ++r)
    if (furthest[r] > 0) order.push_back(r);
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return std::make_pair(furthest[a], complete[a]) > std::make_pair(furthest[b], complete[b]);
  });
  if (order.size() > limit) order.resize(limit);

  std::vector<Explanation> out;
  for (std::size_t r : order) {
    const CompiledRule& rule = impl.rules[r];
    std::set<std::string> next;
    for (int s : stopped[r]) {
      for (const Edge& e : impl.nfa.states[s].edges) {
        if (e.kind == Edge::Kind::Char) next.insert(to_utf8(std::u32string(1, e.ch)));
        if (e.kind == Edge::Kind::Space) next.insert("space");
        if (e.kind == Edge::Kind::Any) next.insert("any character");
      }
    }
    out.push_back({rule.label, rule.line, rule.file, norm.end[furthest[r] - 1], complete[r],
                   std::vector<std::string>(next.begin(), next.end())});
  }
  return out;
}

LintReport Script::lint(std::size_t max_states) const {
  // The script's automaton plus, for each rule, a copy without wildcard edges
  // (accepting as rule n + r), which tells whether an input needs a wildcard.
  const Impl& impl = *impl_;
  const int n = static_cast<int>(impl.rules.size());
  Nfa nfa = impl.nfa;
  const int start = nfa.add_state();
  nfa.epsilon(start, impl.start);
  std::vector<RuleSource> sources;
  for (int r = 0; r < n; ++r) {
    const CompiledRule& rule = impl.rules[r];
    bool wildcard_free = true;
    const int base = static_cast<int>(nfa.states.size());
    for (int s = rule.first; s < rule.last; ++s) nfa.add_state();
    for (int s = rule.first; s < rule.last; ++s) {
      State& copy = nfa.states[base + (s - rule.first)];
      for (Edge e : impl.nfa.states[s].edges) {
        if (e.kind == Edge::Kind::Any) {
          wildcard_free = false;
          continue;
        }
        e.to = base + (e.to - rule.first);
        copy.edges.push_back(e);
      }
      if (impl.nfa.states[s].accept_rule == r) copy.accept_rule = n + r;
    }
    nfa.epsilon(start, base + (rule.start - rule.first));
    sources.push_back({rule.label, rule.line, rule.file, wildcard_free});
  }
  RuleAnalysis search = analyze_rules(nfa, start, sources, max_states);

  LintReport report;
  report.unused = impl.unused;
  report.dead_rules = std::move(search.dead_rules);
  report.complete = search.complete;
  report.files.assign(impl.files.begin(), impl.files.end());

  // `# lint: allow-overlap A B ...` allows overlaps among its labels; `*`
  // stands for every label.
  std::set<std::string> known;
  for (const RuleSource& r : sources) known.insert(r.label);
  std::vector<std::set<std::string>> allowed;
  for (const LintDirective& d : impl_->lint_directives) {
    auto error = [&](const std::string& message) {
      report.directive_errors.push_back({d.pos.line, message, file_name(d.pos)});
    };
    if (d.command != "allow-overlap") {
      error("unknown lint command '" + d.command + "'; expected allow-overlap");
      continue;
    }
    if (d.args.size() < 2) {
      error("allow-overlap needs two or more labels, or a label and *");
      continue;
    }
    bool valid = true;
    for (const std::string& label : d.args) {
      if (label != "*" && !known.count(label)) {
        error("allow-overlap names unknown label '" + label + "'");
        valid = false;
      }
    }
    if (valid) allowed.emplace_back(d.args.begin(), d.args.end());
  }
  auto is_allowed = [&](const Overlap& o) {
    for (const std::set<std::string>& group : allowed) {
      const bool star = group.count("*") > 0;
      const bool a = group.count(o.label) > 0, b = group.count(o.other_label) > 0;
      if ((a && b) || (star && (a || b))) return true;
    }
    return false;
  };
  for (Overlap& o : search.overlaps)
    (is_allowed(o) ? report.allowed_overlaps : report.overlaps).push_back(std::move(o));
  return report;
}

std::vector<LintFinding> LintReport::findings(bool best) const {
  std::vector<LintFinding> out;
  auto where = [](const std::string& file, int line, const std::string& from) {
    return file == from ? "line " + std::to_string(line) : file + ":" + std::to_string(line);
  };
  for (const Overlap& o : overlaps) {
    if (best && !o.best_label.empty()) continue;
    out.push_back({"overlap", o.file, o.line,
                   o.label + " and " + o.other_label + " (" + where(o.other_file, o.other_line, o.file) +
                       ") both match \"" + o.example + "\""});
  }
  for (const DeadRule& d : dead_rules) {
    std::string why = d.matches_nothing ? "it matches no input"
                      : d.covered_by_line
                          ? where(d.covered_by_file, d.covered_by_line, d.file) +
                                " matches everything it does first"
                          : "earlier rules of the label match everything it does first";
    out.push_back({"dead-rule", d.file, d.line,
                   "this rule of " + d.label + " is never used by match(): " + why});
  }
  for (const UnusedDefinition& d : unused)
    out.push_back({"unused", d.file, d.line, d.name + " is never used"});
  for (const LintDirectiveError& e : directive_errors)
    out.push_back({"directive", e.file, e.line, e.message});
  auto file_index = [&](const std::string& file) {
    return std::find(files.begin(), files.end(), file) - files.begin();
  };
  std::stable_sort(out.begin(), out.end(), [&](const LintFinding& a, const LintFinding& b) {
    return std::make_pair(file_index(a.file), a.line) < std::make_pair(file_index(b.file), b.line);
  });
  return out;
}

std::vector<Match> Script::match(const std::string& text) const {
  return impl_->match(text, false);
}

std::vector<Match> Script::match_best(const std::string& text) const {
  return impl_->match(text, true);
}

// Picks one rule per matching label: its first matching rule, or with `best`
// its most specific one. Labels keep the order of their first matching rule.
std::vector<Match> Script::Impl::match(const std::string& text, bool best) const {
  NormalizedText norm = normalize(text);
  std::vector<int> accepted = dfa->accepted_rules(norm.text);
  if (accepted.empty()) return {};
  const int letters = static_cast<int>(
      norm.text.size() - std::count(norm.text.begin(), norm.text.end(), U' '));

  struct Found {
    int rule;
    std::vector<Event> events;
    int specificity;
  };
  std::vector<Found> found;
  std::map<std::string, std::size_t> label_index;
  std::vector<Event> events;
  for (int id : accepted) {
    const CompiledRule& r = rules[id];
    auto it = label_index.find(r.label);
    if (it != label_index.end() && !best) continue;
    PikeVm vm(nfa, r.first, r.last);
    int wildcard_chars = 0;
    if (!vm.run(r.start, norm.text, events, wildcard_chars)) continue;
    Found f{id, events, letters - wildcard_chars};
    if (it == label_index.end()) {
      label_index.emplace(r.label, found.size());
      found.push_back(std::move(f));
    } else if (f.specificity > found[it->second].specificity) {
      found[it->second] = std::move(f);
    }
  }

  int top = 0;
  for (const Found& f : found) top = std::max(top, f.specificity);
  std::vector<Match> out;
  for (const Found& f : found) {
    if (best && f.specificity < top) continue;
    const CompiledRule& r = rules[f.rule];
    out.push_back(build_match(nfa, r.label, r.line, f.specificity, f.events, text, norm));
    out.back().file = r.file;
  }
  return out;
}

}  // namespace nlu

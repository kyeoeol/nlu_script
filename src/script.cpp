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
#include <atomic>
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
  bool content_ = false;         // a line other than an option was read

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
      normalize_nfc(chars);
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
// for the rest of its input. When the cache reaches its limits it keeps the
// states used since the last eviction, up to half of each limit, and drops
// the rest, so states every input passes through survive.
class LazyDfa {
 public:
  LazyDfa(const Nfa& nfa, int start, const CacheOptions& options)
      : nfa_(nfa),
        closure_(nfa),
        max_states_(std::max<std::size_t>(options.max_states, 2)),
        max_bytes_(options.max_bytes) {
    start_ = intern(closure_({start}, 0));
  }

  // Returns the ids of the rules accepting the whole text, ascending.
  std::vector<int> accepted_rules(const std::u32string& text) {
    int d;
    std::size_t i = 0;
    unsigned epoch;
    {
      std::shared_lock<std::shared_mutex> lock(mutex_);
      d = start_;
      touch(d);
      for (; i < text.size(); ++i) {
        auto it = states_[d].next.find(text[i]);
        if (it == states_[d].next.end()) break;
        d = it->second;
        touch(d);
        if (states_[d].set.empty()) return {};
      }
      if (i == text.size()) return states_[d].accepts;
      epoch = epoch_;
    }
    std::lock_guard<std::shared_mutex> lock(mutex_);
    if (epoch != epoch_) {  // states were evicted, so `d` is stale
      d = start_;
      i = 0;
    }
    for (; i < text.size(); ++i) {
      char32_t c = text[i];
      auto it = states_[d].next.find(c);
      if (it != states_[d].next.end()) {
        d = it->second;
      } else {
        std::vector<int> set = closure_.step(states_[d].set, c);
        if (states_.size() >= max_states_ || bytes_ >= max_bytes_) {
          evict();
          d = intern(std::move(set));
        } else {
          int next = intern(std::move(set));
          states_[d].next.emplace(c, next);
          ++transitions_;
          bytes_ += kTransitionBytes;
          d = next;
        }
      }
      touch(d);
      if (states_[d].set.empty()) return {};
    }
    return states_[d].accepts;
  }

  CacheStats stats() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return {states_.size(), transitions_, bytes_, evictions_};
  }

 private:
  static constexpr std::size_t kTransitionBytes = 32;  // an unordered_map node

  struct DState {
    std::vector<int> set;      // NFA states with consuming edges or accepting
    std::vector<int> accepts;  // accepted rules
    std::unordered_map<char32_t, int> next;
  };

  const Nfa& nfa_;
  std::shared_mutex mutex_;
  // Everything below is guarded by mutex_, except that readers holding the
  // shared lock update used_.
  Closure closure_;
  std::size_t max_states_, max_bytes_;
  int start_ = 0;
  unsigned epoch_ = 0;  // bumped by evict(), which renumbers states
  std::vector<DState> states_;
  std::unordered_multimap<std::size_t, int> index_;  // hash of a state's set -> state
  // The round in which each state was last used; a deque keeps the atomics put.
  std::deque<std::atomic<unsigned>> used_;
  unsigned round_ = 1;  // bumped by each eviction
  std::size_t transitions_ = 0;
  std::size_t bytes_ = 0;
  std::size_t evictions_ = 0;

  // Marks state `d` used in this round. Only the first use in a round writes,
  // so states every input passes through stay quiet.
  void touch(int d) {
    std::atomic<unsigned>& u = used_[d];
    if (u.load(std::memory_order_relaxed) != round_) u.store(round_, std::memory_order_relaxed);
  }

  static std::size_t hash(const std::vector<int>& set) {
    std::size_t h = set.size();
    for (int s : set) h = h * 1000003u ^ static_cast<std::size_t>(s);
    return h;
  }

  static std::size_t state_bytes(const DState& d) {
    return sizeof(DState) + (d.set.size() + d.accepts.size()) * sizeof(int) + 64;
  }

  int intern(std::vector<int> set) {
    const std::size_t h = hash(set);
    auto range = index_.equal_range(h);
    for (auto it = range.first; it != range.second; ++it)
      if (states_[it->second].set == set) return it->second;
    DState d;
    d.accepts = nfa_.accepted_rules(set);
    d.set = std::move(set);
    bytes_ += state_bytes(d);
    int id = static_cast<int>(states_.size());
    states_.push_back(std::move(d));
    used_.emplace_back(round_);
    index_.emplace(h, id);
    return id;
  }

  // Keeps the start state and the states used in this round, up to half of
  // each limit (in order of creation, so shallow states come first), with
  // the transitions between them; drops the rest.
  void evict() {
    ++evictions_;
    std::vector<int> renumber(states_.size(), -1);
    std::vector<int> kept;
    std::size_t kept_bytes = 0;
    for (int id = 0; id < static_cast<int>(states_.size()); ++id) {
      const bool wanted = id == start_ || used_[id].load(std::memory_order_relaxed) == round_;
      const std::size_t b = state_bytes(states_[id]);
      if (!wanted || (id != start_ && (kept.size() >= max_states_ / 2 ||
                                       kept_bytes + b > max_bytes_ / 2)))
        continue;
      renumber[id] = static_cast<int>(kept.size());
      kept.push_back(id);
      kept_bytes += b;
    }
    std::vector<DState> states;
    states.reserve(kept.size());
    transitions_ = 0;
    bytes_ = 0;
    for (int id : kept) {
      DState d = std::move(states_[id]);
      std::unordered_map<char32_t, int> next;
      for (const auto& kv : d.next) {
        if (renumber[kv.second] < 0) continue;
        next.emplace(kv.first, renumber[kv.second]);
      }
      d.next = std::move(next);
      transitions_ += d.next.size();
      bytes_ += state_bytes(d) + d.next.size() * kTransitionBytes;
      states.push_back(std::move(d));
    }
    states_ = std::move(states);
    index_.clear();
    used_.clear();
    ++round_;
    for (int id = 0; id < static_cast<int>(states_.size()); ++id) {
      index_.emplace(hash(states_[id].set), id);
      used_.emplace_back(0);
    }
    start_ = renumber[start_];
    ++epoch_;
  }
};

// --- phase 2: Pike VM -------------------------------------------------------

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
    add(current, start, -1, 0, 0, 0);
    for (std::size_t i = 0; i < text.size() && !current.empty(); ++i) {
      next.clear();
      ++generation_;
      for (const Thread& t : current) {
        if (t.edge < 0) continue;
        const Edge& e = nfa_.states[t.state].edges[t.edge];
        if (nfa_.matches(e, text[i]))
          add(next, e.to, t.log, static_cast<int>(i) + 1, text[i],
              t.wild + (e.wildcard ? 1 : 0));
      }
      std::swap(current, next);
      if (log_.size() >= compact_at_) {
        compact(current);
        compact_at_ = std::max(kMinCompact, 2 * log_.size());
      }
    }
    for (const Thread& t : current) {
      if (t.edge >= 0) continue;
      events.clear();
      for (int k = t.log; k >= 0; k = log_[k].prev) events.push_back({log_[k].action, log_[k].pos});
      std::reverse(events.begin(), events.end());
      wildcard_chars = t.wild;
      return true;
    }
    return false;
  }

 private:
  // The actions taken so far, as a list from the latest back; threads share
  // their common beginnings. Nodes are only appended during a run.
  struct LogNode {
    int action;
    int pos;
    int prev;  // index into log_, or -1
  };
  struct Thread {
    int state;
    int edge;  // consuming edge to follow, or -1 for an accepting thread
    int log;   // index into log_, or -1
    int wild;  // characters consumed by wildcards so far
  };
  struct Item {
    bool visit;  // true: explore `state`; false: emit a thread
    int state;
    int edge;
    int log;
  };

  const Nfa& nfa_;
  int first_;
  std::vector<unsigned> mark_;
  unsigned generation_ = 1;
  std::vector<LogNode> log_;
  // log_ is compacted when it reaches compact_at_ nodes, so the nodes of
  // threads that died don't pile up over long inputs.
  static constexpr std::size_t kMinCompact = 4096;
  std::size_t compact_at_ = kMinCompact;
  std::vector<Item> stack_, children_;  // add()'s, kept to reuse their memory

  // Keeps only the log nodes that `threads` reach, in their order (so a
  // node's prev still comes before it), and renumbers the threads' logs.
  void compact(std::vector<Thread>& threads) {
    std::vector<bool> keep(log_.size(), false);
    for (const Thread& t : threads)
      for (int k = t.log; k >= 0 && !keep[k]; k = log_[k].prev) keep[k] = true;
    std::vector<int> renumber(log_.size(), -1);
    int n = 0;
    for (std::size_t k = 0; k < log_.size(); ++k) {
      if (!keep[k]) continue;
      const int prev = log_[k].prev;
      log_[n] = {log_[k].action, log_[k].pos, prev < 0 ? -1 : renumber[prev]};
      renumber[k] = n++;
    }
    log_.resize(static_cast<std::size_t>(n));
    for (Thread& t : threads)
      if (t.log >= 0) t.log = renumber[t.log];
  }

  // Depth-first, in edge order, so threads are listed by priority. `prev` is
  // the character consumed last, which guard edges test.
  void add(std::vector<Thread>& list, int state, int log, int pos, char32_t prev, int wild) {
    stack_.clear();
    stack_.push_back({true, state, -1, log});
    while (!stack_.empty()) {
      const Item item = stack_.back();
      stack_.pop_back();
      if (!item.visit) {
        list.push_back({item.state, item.edge, item.log, wild});
        continue;
      }
      unsigned& mark = mark_[item.state - first_];
      if (mark == generation_) continue;
      mark = generation_;
      const State& st = nfa_.states[item.state];
      children_.clear();
      for (std::size_t k = 0; k < st.edges.size(); ++k) {
        const Edge& e = st.edges[k];
        if (e.kind == Edge::Kind::Epsilon) {
          children_.push_back({true, e.to, -1, item.log});
        } else if (e.kind == Edge::Kind::Guard) {
          if (guard_holds(e.guard, prev)) children_.push_back({true, e.to, -1, item.log});
        } else if (e.kind == Edge::Kind::Action) {
          log_.push_back({e.action, pos, item.log});
          children_.push_back({true, e.to, -1, static_cast<int>(log_.size()) - 1});
        } else {
          children_.push_back({false, item.state, static_cast<int>(k), item.log});
        }
      }
      if (st.accept_rule >= 0) children_.push_back({false, item.state, -1, item.log});
      stack_.insert(stack_.end(), children_.rbegin(), children_.rend());
    }
  }
};

// Whether the Pike VM can tell anything about a match of the rule with
// states [first, last) that the DFA doesn't: without actions it records no
// slots, and without wildcards their character count is 0.
bool needs_pike_vm(const Nfa& nfa, int first, int last) {
  for (int s = first; s < last; ++s)
    for (const Edge& e : nfa.states[s].edges)
      if (e.kind == Edge::Kind::Action || e.wildcard) return true;
  return false;
}

// --- building results -------------------------------------------------------

struct CompiledRule {
  std::string label;
  int line;
  std::string file;
  int start;  // start state
  int first;  // the rule's states occupy the id range [first, last)
  int last;
  bool needs_vm;  // see needs_pike_vm()
};


struct Instance {
  int slot;
  int open_pos;
  int close_pos;
  std::size_t open_event;
  Attributes attributes;
  Normalizer normalizer;
};

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

// The match of `rule` over the bytes [begin, end) of `original`, which
// contain every event.
Match build_match(const Nfa& nfa, const CompiledRule& rule, int specificity,
                  const std::vector<Event>& events, const std::string& original,
                  const NormalizedText& norm, std::size_t begin, std::size_t end) {
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
          set_attribute(instances[open.back()].attributes, a.key, a.value);
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
    if (target >= 0) set_attribute(instances[target].attributes, a.key, a.value);
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

  // System entity values are read from the text as normalized for matching
  // (composed, case-folded, and width-folded with `option fold-width`), the
  // text their grammars matched.
  std::string value;
  for (Instance& inst : instances) {
    if (inst.normalizer == Normalizer::None) continue;
    const std::string text = to_utf8(
        norm.text.substr(static_cast<std::size_t>(inst.open_pos),
                         static_cast<std::size_t>(inst.close_pos - inst.open_pos)));
    std::string currency;
    if ((inst.normalizer == Normalizer::KoreanNumber && korean_number_value(text, value)) ||
        (inst.normalizer == Normalizer::EnglishNumber && english_number_value(text, value)) ||
        (inst.normalizer == Normalizer::Ordinal && ordinal_value(text, value)))
      set_attribute(inst.attributes, "value", value);
    if (inst.normalizer == Normalizer::Money && money_value(text, value, currency)) {
      set_attribute(inst.attributes, "value", value);
      set_attribute(inst.attributes, "currency", currency);
    }
    if (inst.normalizer == Normalizer::DateTime)
      for (const auto& kv : datetime_attributes(text))
        set_attribute(inst.attributes, kv.first, kv.second);
  }

  Match m;
  m.label = rule.label;
  m.line = rule.line;
  m.file = rule.file;
  m.begin = begin;
  m.end = end;
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

  std::size_t cursor = begin;
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
  m.tagged.append(original, cursor, end - cursor);
  return m;
}

// Orders positions by file (the main script, then included files as they are
// read) and then by line and column.
std::tuple<std::size_t, int, int> position_key(const ParsedScript& parsed, const SourcePos& pos) {
  std::size_t file = 0;
  while (file < parsed.files.size() && &parsed.files[file] != pos.file) ++file;
  return {pos.file ? file : 0, pos.line, pos.column};
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

// Whether `pattern` refers, directly or through other definitions, to a
// definition whose values had errors.
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

// --- lint ---------------------------------------------------------------------

// Definitions that no rule reaches, directly or through other definitions.
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

}  // namespace

// --- Script -----------------------------------------------------------------

struct Script::Impl {
  Nfa nfa;
  bool fold_width = false;  // `option fold-width`: input is width-folded too
  std::vector<CompiledRule> rules;
  std::vector<UnusedDefinition> unused;
  std::deque<std::string> files;  // what lint_directives' positions point into
  std::vector<LintDirective> lint_directives;
  int start = 0;

  // The DFA cache: one shared, or one per thread (see cache()).
  CacheOptions cache_options;
  mutable std::unique_ptr<LazyDfa> dfa;
  // Per-thread caches are keyed by `id` and dropped once `alive` expires;
  // set_cache_options() replaces both, retiring the caches built before.
  std::uint64_t id = next_id();
  std::shared_ptr<const int> alive = std::make_shared<const int>(0);

  static std::uint64_t next_id() {
    static std::atomic<std::uint64_t> counter{0};
    return ++counter;
  }

  LazyDfa& cache() const {
    if (!cache_options.per_thread) return *dfa;
    struct ThreadCache {
      std::weak_ptr<const int> alive;
      std::unique_ptr<LazyDfa> dfa;
    };
    thread_local std::unordered_map<std::uint64_t, ThreadCache> caches;
    auto it = caches.find(id);
    if (it != caches.end()) return *it->second.dfa;
    for (auto i = caches.begin(); i != caches.end();)  // drop caches of scripts that are gone
      i = i->second.alive.expired() ? caches.erase(i) : std::next(i);
    ThreadCache& c = caches[id];
    c = {alive, std::make_unique<LazyDfa>(nfa, start, cache_options)};
    return *c.dfa;
  }

  std::vector<Match> match(const std::string& text, bool best) const;
  std::vector<Match> search(const std::string& text) const;
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
                             static_cast<int>(impl->rules.size()),
                             parsed.options.ignore_trailing_punctuation);
    } catch (const CompileError& e) {
      errors.push_back(diagnostic(e));
      continue;
    }
    r.last = static_cast<int>(impl->nfa.states.size());
    r.needs_vm = needs_pike_vm(impl->nfa, r.first, r.last);
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
  impl->fold_width = parsed.options.fold_width;
  impl->unused = unused_definitions(parsed);
  // Moving a deque keeps its elements in place, so the directives' file
  // pointers stay valid.
  impl->files = std::move(parsed.files);
  impl->lint_directives = std::move(parsed.lint_directives);
  impl->dfa = std::make_unique<LazyDfa>(impl->nfa, impl->start, impl->cache_options);
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

CacheStats Script::cache_stats() const { return impl_->cache().stats(); }

void Script::set_cache_options(const CacheOptions& options) {
  impl_->cache_options = options;
  impl_->dfa = std::make_unique<LazyDfa>(impl_->nfa, impl_->start, options);
  impl_->id = Impl::next_id();
  impl_->alive = std::make_shared<const int>(0);
}

CacheOptions Script::cache_options() const { return impl_->cache_options; }

std::vector<Explanation> Script::explain(const std::string& text, std::size_t limit) const {
  const Impl& impl = *impl_;
  const NormalizedText norm = normalize(text, impl.fold_width);
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
    // `states` is sorted, so each rule's states are next to each other.
    for (std::size_t k = 0; k < states.size();) {
      const int r = rule_of(states[k]);
      std::size_t end = k + 1;
      while (end < states.size() && rule_of(states[end]) == r) ++end;
      if (r >= 0) {
        if (i == norm.text.size())
          for (std::size_t j = k; j < end; ++j)
            if (impl.nfa.states[states[j]].accept_rule == r) complete[r] = true;
        if (i > 0) {
          furthest[r] = i;
          stopped[r].assign(states.begin() + k, states.begin() + end);
        }
      }
      k = end;
    }
    if (i == norm.text.size()) break;
    states = closure.step(states, norm.text[i]);
    if (states.empty()) break;
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
        if (e.wildcard) {
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
  NormalizedText norm = normalize(text, fold_width);
  std::vector<int> accepted = cache().accepted_rules(norm.text);
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
    int wildcard_chars = 0;
    events.clear();
    if (r.needs_vm && !PikeVm(nfa, r.first, r.last).run(r.start, norm.text, events, wildcard_chars))
      continue;
    Found f{id, std::move(events), letters - wildcard_chars};
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
    out.push_back(build_match(nfa, rules[f.rule], f.specificity, f.events, text, norm, 0, text.size()));
  }
  return out;
}

std::vector<Match> Script::search(const std::string& text) const { return impl_->search(text); }

std::vector<Match> Script::Impl::search(const std::string& text) const {
  const NormalizedText norm = normalize(text, fold_width);
  const std::u32string& t = norm.text;
  const std::size_t n = t.size();
  // Matches neither start nor end inside a run of ASCII letters and digits
  // (input is case-folded already).
  auto alnum = [](char32_t c) { return (c >= U'a' && c <= U'z') || (c >= U'0' && c <= U'9'); };
  auto boundary = [&](std::size_t at) {
    return at == 0 || at == n || t[at - 1] == U' ' || t[at] == U' ' ||
           !(alnum(t[at - 1]) && alnum(t[at]));
  };

  // Every rule at once, determinized as needed for this call; the shared
  // lazy DFA only answers whole-input matches.
  Closure closure(nfa);
  std::map<std::vector<int>, int> index;
  std::vector<std::vector<int>> sets, accepts;
  std::map<std::pair<int, char32_t>, int> next;
  auto intern = [&](std::vector<int> set) {
    if (set.empty()) return -1;
    auto it = index.find(set);
    if (it != index.end()) return it->second;
    int id = static_cast<int>(sets.size());
    index.emplace(set, id);
    accepts.push_back(nfa.accepted_rules(set));
    sets.push_back(std::move(set));
    return id;
  };
  auto step = [&](int d, char32_t c) {
    auto it = next.find({d, c});
    if (it != next.end()) return it->second;
    int to = intern(closure.step(sets[d], c));
    next.emplace(std::make_pair(d, c), to);
    return to;
  };
  const int first = intern(closure({start}, 0));

  std::vector<Match> out;
  std::vector<Event> events;
  for (std::size_t i = 0; i < n && first >= 0;) {
    if (t[i] == U' ' || !boundary(i)) {
      ++i;
      continue;
    }
    // The longest text from i that some rule matches, ending at a boundary.
    std::size_t end = i;
    int accepting = -1;
    int d = first;
    for (std::size_t j = i; j < n && d >= 0; ++j) {
      d = step(d, t[j]);
      if (d >= 0 && !accepts[d].empty() && t[j] != U' ' && boundary(j + 1)) {
        end = j + 1;
        accepting = d;
      }
    }
    if (accepting < 0) {
      ++i;
      continue;
    }
    const std::u32string span = t.substr(i, end - i);
    const int letters = static_cast<int>(span.size() - std::count(span.begin(), span.end(), U' '));
    std::set<std::string> seen;
    for (int id : accepts[accepting]) {
      const CompiledRule& r = rules[id];
      if (seen.count(r.label)) continue;
      int wildcard_chars = 0;
      events.clear();
      if (r.needs_vm && !PikeVm(nfa, r.first, r.last).run(r.start, span, events, wildcard_chars))
        continue;
      seen.insert(r.label);
      for (Event& e : events) e.pos += static_cast<int>(i);
      out.push_back(build_match(nfa, r, letters - wildcard_chars, events, text, norm,
                                norm.begin[i], norm.end[end - 1]));
    }
    i = end;
  }
  return out;
}

// --- saving and loading compiled scripts ----------------------------------------

namespace {

// "\x89NLU" then a line ending and a format version, so text never looks like
// a saved script and a saved script is never read by another format version.
const char kMagic[] = "\x89NLU\r\n\x1a\n";
constexpr std::uint32_t kFormatVersion = 2;  // 2: Edge::wildcard
static_assert(static_cast<unsigned>(Edge::Kind::Any) < 8, "edge kinds fit in 3 bits");
static_assert(static_cast<unsigned>(Guard::AfterNoFinalOrRieul) < 8, "guards fit in 3 bits");

// Numbers are LEB128 varints; signed ones are zigzag-encoded first, so small
// magnitudes (and -1) take one byte.
class Writer {
 public:
  void u8(unsigned v) { out_ += static_cast<char>(v & 0xff); }
  void u32(std::uint32_t v) {
    while (v >= 0x80) {
      u8((v & 0x7f) | 0x80);
      v >>= 7;
    }
    u8(v);
  }
  void i32(int v) {
    const std::uint32_t u = static_cast<std::uint32_t>(v);
    u32((u << 1) ^ (v < 0 ? 0xffffffffu : 0u));
  }
  void size(std::size_t v) { u32(static_cast<std::uint32_t>(v)); }
  void str(const std::string& s) {
    size(s.size());
    out_ += s;
  }
  std::string& out() { return out_; }

 private:
  std::string out_;
};

class Reader {
 public:
  explicit Reader(const std::string& in) : in_(in) {}
  [[noreturn]] static void fail(const std::string& what) {
    throw std::runtime_error("not a saved nlu_script script: " + what);
  }
  unsigned u8() {
    if (at_ >= in_.size()) fail("unexpected end");
    return static_cast<unsigned char>(in_[at_++]);
  }
  std::uint32_t u32() {
    std::uint32_t v = 0;
    for (int shift = 0;; shift += 7) {
      if (shift > 28) fail("bad number");
      const unsigned b = u8();
      v |= static_cast<std::uint32_t>(b & 0x7f) << shift;
      if (!(b & 0x80)) return v;
    }
  }
  int i32() {
    const std::uint32_t u = u32();
    return static_cast<int>((u >> 1) ^ (0u - (u & 1)));
  }
  // A count of items taking at least `item_bytes` each, checked against the
  // bytes left so corrupt counts can't ask for huge allocations.
  std::size_t count(std::size_t item_bytes) {
    std::size_t n = u32();
    if (n > (in_.size() - at_) / std::max<std::size_t>(item_bytes, 1)) fail("bad count");
    return n;
  }
  std::string str() {
    std::size_t n = count(1);
    std::string s = in_.substr(at_, n);
    at_ += n;
    return s;
  }
  bool done() const { return at_ == in_.size(); }

 private:
  const std::string& in_;
  std::size_t at_ = 0;
};

}  // namespace

std::string Script::save() const {
  const Impl& impl = *impl_;
  Writer w;
  w.out() = std::string(kMagic, sizeof kMagic - 1);
  w.u32(kFormatVersion);
  w.u8(impl.fold_width ? 1 : 0);
  w.u8(impl.nfa.has_guards ? 1 : 0);
  w.i32(impl.start);
  w.size(impl.nfa.states.size());
  for (std::size_t s = 0; s < impl.nfa.states.size(); ++s) {
    const State& st = impl.nfa.states[s];
    w.i32(st.accept_rule);
    w.size(st.edges.size());
    for (const Edge& e : st.edges) {
      w.u8(static_cast<unsigned>(e.kind) | static_cast<unsigned>(e.guard) << 3 |
           (e.wildcard ? 1u : 0u) << 6);
      w.u8(static_cast<unsigned>(e.char_class));
      w.u32(static_cast<std::uint32_t>(e.ch));
      w.i32(e.set);
      w.i32(e.action);
      w.i32(e.to - static_cast<int>(s));  // usually the next few states
    }
  }
  w.size(impl.nfa.sets.size());
  for (const auto& set : impl.nfa.sets) {
    w.size(set.size());
    for (char32_t c : set) w.u32(static_cast<std::uint32_t>(c));
  }
  w.size(impl.nfa.actions.size());
  for (const Action& a : impl.nfa.actions) {
    w.u8(static_cast<unsigned>(a.kind));
    w.i32(a.slot);
    w.str(a.key);
    w.str(a.value);
    w.u8(static_cast<unsigned>(a.normalizer));
  }
  w.size(impl.nfa.slot_names.size());
  for (const std::string& name : impl.nfa.slot_names) w.str(name);
  w.size(impl.rules.size());
  for (const CompiledRule& r : impl.rules) {
    w.str(r.label);
    w.i32(r.line);
    w.str(r.file);
    w.i32(r.start);
    w.i32(r.first);
    w.i32(r.last);
  }
  w.size(impl.unused.size());
  for (const UnusedDefinition& d : impl.unused) {
    w.str(d.name);
    w.i32(d.line);
    w.str(d.file);
  }
  w.size(impl.files.size());
  for (const std::string& f : impl.files) w.str(f);
  w.size(impl.lint_directives.size());
  for (const LintDirective& d : impl.lint_directives) {
    std::size_t file = 0;
    while (file < impl.files.size() && &impl.files[file] != d.pos.file) ++file;
    w.size(file);
    w.i32(d.pos.line);
    w.i32(d.pos.column);
    w.str(d.command);
    w.size(d.args.size());
    for (const std::string& a : d.args) w.str(a);
  }
  return std::move(w.out());
}

Script Script::load(const std::string& bytes) {
  if (bytes.compare(0, sizeof kMagic - 1, kMagic) != 0) Reader::fail("no header");
  const std::string body = bytes.substr(sizeof kMagic - 1);
  Reader r(body);
  if (r.u32() != kFormatVersion) Reader::fail("saved by another format version");
  auto impl = std::make_unique<Impl>();
  impl->fold_width = r.u8() != 0;
  impl->nfa.has_guards = r.u8() != 0;
  impl->start = r.i32();
  const std::size_t states = r.count(2);
  impl->nfa.states.resize(states);
  for (std::size_t s = 0; s < states; ++s) {
    State& st = impl->nfa.states[s];
    st.accept_rule = r.i32();
    const std::size_t edges = r.count(6);
    st.edges.reserve(edges);
    for (std::size_t k = 0; k < edges; ++k) {
      Edge e;
      const unsigned head = r.u8(), cls = r.u8();
      const unsigned kind = head & 7, guard = head >> 3 & 7, wildcard = head >> 6;
      if (kind > static_cast<unsigned>(Edge::Kind::Any) || wildcard > 1 ||
          guard > static_cast<unsigned>(Guard::AfterNoFinalOrRieul) ||
          cls > static_cast<unsigned>(CharClass::SyllableOtherFinal))
        Reader::fail("bad edge");
      e.kind = static_cast<Edge::Kind>(kind);
      e.guard = static_cast<Guard>(guard);
      e.char_class = static_cast<CharClass>(cls);
      e.wildcard = wildcard != 0;
      e.ch = static_cast<char32_t>(r.u32());
      e.set = r.i32();
      e.action = r.i32();
      e.to = static_cast<int>(s) + r.i32();
      st.edges.push_back(e);
    }
  }
  impl->nfa.sets.resize(r.count(1));
  for (auto& set : impl->nfa.sets) {
    set.resize(r.count(1));
    for (char32_t& c : set) c = static_cast<char32_t>(r.u32());
    if (!std::is_sorted(set.begin(), set.end())) Reader::fail("bad set");
  }
  impl->nfa.actions.resize(r.count(5));
  for (Action& a : impl->nfa.actions) {
    const unsigned kind = r.u8();
    if (kind > static_cast<unsigned>(Action::Kind::Assign)) Reader::fail("bad action");
    a.kind = static_cast<Action::Kind>(kind);
    a.slot = r.i32();
    a.key = r.str();
    a.value = r.str();
    const unsigned normalizer = r.u8();
    if (normalizer > static_cast<unsigned>(Normalizer::Money)) Reader::fail("bad action");
    a.normalizer = static_cast<Normalizer>(normalizer);
  }
  impl->nfa.slot_names.resize(r.count(1));
  for (std::string& name : impl->nfa.slot_names) name = r.str();
  impl->rules.resize(r.count(6));
  for (CompiledRule& rule : impl->rules) {
    rule.label = r.str();
    rule.line = r.i32();
    rule.file = r.str();
    rule.start = r.i32();
    rule.first = r.i32();
    rule.last = r.i32();
  }
  impl->unused.resize(r.count(3));
  for (UnusedDefinition& d : impl->unused) {
    d.name = r.str();
    d.line = r.i32();
    d.file = r.str();
  }
  const std::size_t files = r.count(1);
  for (std::size_t k = 0; k < files; ++k) impl->files.push_back(r.str());
  impl->lint_directives.resize(r.count(5));
  for (LintDirective& d : impl->lint_directives) {
    const std::size_t file = r.u32();
    if (file >= impl->files.size()) Reader::fail("bad lint comment");
    d.pos = SourcePos{r.i32(), r.i32(), &impl->files[file]};
    d.command = r.str();
    d.args.resize(r.count(1));
    for (std::string& a : d.args) a = r.str();
  }
  if (!r.done()) Reader::fail("trailing bytes");

  // Matching relies on these: the start state comes first, rules own
  // contiguous ranges of states after it in order, and edges stay within
  // their rule; every set, action and slot exists.
  const Nfa& nfa = impl->nfa;
  const int n = static_cast<int>(nfa.states.size());
  if (impl->start != 0 || n == 0) Reader::fail("bad start");
  int next_first = 1;
  for (std::size_t id = 0; id < impl->rules.size(); ++id) {
    CompiledRule& rule = impl->rules[id];
    if (rule.first != next_first || rule.last <= rule.first || rule.last > n ||
        rule.start < rule.first || rule.start >= rule.last)
      Reader::fail("bad rule");
    for (int s = rule.first; s < rule.last; ++s) {
      const State& st = nfa.states[s];
      if (st.accept_rule != -1 && st.accept_rule != static_cast<int>(id)) Reader::fail("bad state");
      for (const Edge& e : st.edges) {
        if (e.to < rule.first || e.to >= rule.last) Reader::fail("bad edge");
        if (e.kind == Edge::Kind::Any && e.set >= static_cast<int>(nfa.sets.size()))
          Reader::fail("bad edge");
        if (e.kind == Edge::Kind::Action &&
            (e.action < 0 || e.action >= static_cast<int>(nfa.actions.size())))
          Reader::fail("bad edge");
      }
    }
    rule.needs_vm = needs_pike_vm(nfa, rule.first, rule.last);
    next_first = rule.last;
  }
  if (next_first != n) Reader::fail("bad rule");
  for (const Edge& e : nfa.states[0].edges) {
    if (e.kind != Edge::Kind::Epsilon || e.to < 1 || e.to >= n) Reader::fail("bad start");
  }
  if (nfa.states[0].accept_rule != -1) Reader::fail("bad start");
  for (const Action& a : nfa.actions) {
    const bool unqualified = a.kind == Action::Kind::Assign && a.slot == -1;
    if (!unqualified && (a.slot < 0 || a.slot >= static_cast<int>(nfa.slot_names.size())))
      Reader::fail("bad action");
  }
  impl->dfa = std::make_unique<LazyDfa>(impl->nfa, impl->start, impl->cache_options);
  return Script(std::move(impl));
}

void Script::save_file(const std::string& path) const {
  std::ofstream out(path, std::ios::binary);
  out << save();
  if (!out) throw std::runtime_error("cannot write " + path);
}

Script Script::load_file(const std::string& path) {
  std::string bytes;
  if (!read_file(path, bytes)) throw std::runtime_error("cannot open " + path);
  return load(bytes);
}

}  // namespace nlu

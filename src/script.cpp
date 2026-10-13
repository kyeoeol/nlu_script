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
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>

#include "compiler.hpp"
#include "nlu_script/nlu_script.hpp"
#include "parser.hpp"
#include "unicode.hpp"

namespace nlu {

using namespace detail;

CompileError::CompileError(const std::string& message, int line, int column)
    : std::runtime_error(line > 0 ? "line " + std::to_string(line) + ", column " +
                                        std::to_string(column) + ": " + message
                                  : message),
      message_(message),
      line_(line),
      column_(column) {}

namespace {

// --- script source ----------------------------------------------------------

struct Rule {
  std::string label;
  NodePtr pattern;
};

struct ParsedScript {
  Definitions defs;
  std::vector<Rule> rules;
};

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

ParsedScript parse_script(const std::string& source) {
  enum class Block { None, Label, Entity, Variable };
  ParsedScript out;
  std::map<std::string, std::vector<NodePtr>> entity_values;
  std::map<std::string, std::vector<NodePtr>> variable_values;
  Block block = Block::None;
  std::string current;

  // A variable takes one pattern per line, an entity a comma-separated list
  // of values. `column` locates text[0].
  auto add_values = [&](const std::u32string& text, int line_no, int column) {
    if (block == Block::Variable) {
      variable_values[current].push_back(parse_pattern(text, line_no, column));
      return;
    }
    for (const auto& part : split_top_level_commas(text)) {
      std::size_t lead = 0;
      std::u32string value = trim(part.second, &lead);
      int at = column + static_cast<int>(part.first + lead);
      if (value.empty()) throw CompileError("empty entity value", line_no, at);
      entity_values[current].push_back(parse_pattern(value, line_no, at));
    }
  };

  std::istringstream lines(source);
  std::string raw;
  int line_no = 0;
  while (std::getline(lines, raw)) {
    ++line_no;
    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
    std::u32string line;
    for (const DecodedChar& d : decode_utf8(raw)) line += d.code;
    std::size_t first = 0;
    while (first < line.size() && is_space(line[first])) ++first;
    if (first == line.size() || line[first] == U'#') continue;
    const int column = static_cast<int>(first) + 1;

    if (first > 0) {
      std::u32string content = trim(line);
      if (block == Block::None)
        throw CompileError("indented pattern outside of a label or definition", line_no, column);
      if (block == Block::Label) {
        out.rules.push_back({current, parse_pattern(content, line_no, column)});
      } else {
        add_values(content, line_no, column);
      }
      continue;
    }

    if (line[0] == U'@') {
      const bool is_var = starts_with(line, U"@var.");
      const std::size_t name_at = is_var ? 5 : 1;
      std::size_t colon = line.find(U':');
      if (colon == std::u32string::npos)
        throw CompileError("expected ':' after the definition name", line_no, column);
      std::u32string name32 = trim(line.substr(name_at, colon - name_at));
      bool valid = !name32.empty();
      for (char32_t c : name32) valid = valid && is_name_char(c);
      if (!valid) throw CompileError("invalid definition name", line_no, column + static_cast<int>(name_at));
      current = to_utf8(name32);
      auto& table = is_var ? out.defs.variables : out.defs.entities;
      if (table.count(current))
        throw CompileError(std::string(is_var ? "variable @var." : "entity @") + current +
                               " is already defined",
                           line_no, column);
      table[current].pos = SourcePos{line_no, column};
      block = is_var ? Block::Variable : Block::Entity;
      std::size_t lead = 0;
      std::u32string body = trim(line.substr(colon + 1), &lead);
      if (!body.empty()) add_values(body, line_no, static_cast<int>(colon + 2 + lead));
      continue;
    }

    std::u32string label = trim(line);
    for (char32_t c : label)
      if (is_space(c))
        throw CompileError("labels cannot contain spaces; indent patterns under their label",
                           line_no, column);
    current = to_utf8(label);
    block = Block::Label;
  }

  auto finish = [](std::map<std::string, Definition>& table,
                   std::map<std::string, std::vector<NodePtr>>& values, const char* kind) {
    for (auto& kv : table) {
      std::vector<NodePtr>& options = values[kv.first];
      if (options.empty())
        throw CompileError(std::string(kind) + kv.first + " has no values", kv.second.pos.line,
                           kv.second.pos.column);
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
  };
  finish(out.defs.entities, entity_values, "entity @");
  finish(out.defs.variables, variable_values, "variable @var.");
  return out;
}

// --- phase 1: lazy DFA ------------------------------------------------------

class LazyDfa {
 public:
  LazyDfa(const Nfa& nfa, int start) : nfa_(nfa), closure_(nfa), start_nfa_(start) { reset(); }

  // Returns the ids of the rules accepting the whole text, ascending.
  std::vector<int> accepted_rules(const std::u32string& text) {
    int d = start_;
    for (char32_t c : text) {
      auto it = states_[d].next.find(c);
      if (it != states_[d].next.end()) {
        d = it->second;
      } else {
        std::vector<int> targets;
        for (int s : states_[d].set)
          for (const Edge& e : nfa_.states[s].edges)
            if (e.consumes() && nfa_.matches(e, c)) targets.push_back(e.to);
        std::vector<int> set = closure_(std::move(targets));
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
  Closure closure_;
  int start_nfa_;
  int start_ = 0;
  std::vector<DState> states_;
  std::map<std::vector<int>, int> index_;

  void reset() {
    states_.clear();
    index_.clear();
    start_ = intern(closure_({start_nfa_}));
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
  // priority accepting path. Returns false if the rule does not match.
  bool run(int start, const std::u32string& text, std::vector<Event>& events) {
    std::vector<Thread> current;
    std::vector<Thread> next;
    add(current, start, nullptr, 0);
    for (std::size_t i = 0; i < text.size() && !current.empty(); ++i) {
      next.clear();
      ++generation_;
      for (const Thread& t : current) {
        if (t.edge < 0) continue;
        const Edge& e = nfa_.states[t.state].edges[t.edge];
        if (nfa_.matches(e, text[i])) add(next, e.to, t.log, static_cast<int>(i) + 1);
      }
      std::swap(current, next);
    }
    for (const Thread& t : current) {
      if (t.edge >= 0) continue;
      events.clear();
      for (const LogNode* n = t.log.get(); n; n = n->prev.get()) events.push_back({n->action, n->pos});
      std::reverse(events.begin(), events.end());
      return true;
    }
    return false;
  }

 private:
  struct Thread {
    int state;
    int edge;  // consuming edge to follow, or -1 for an accepting thread
    Log log;
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

  // Depth-first, in edge order, so threads are listed by priority.
  void add(std::vector<Thread>& list, int state, Log log, int pos) {
    std::vector<Item> stack;
    stack.push_back({true, state, -1, std::move(log)});
    std::vector<Item> children;
    while (!stack.empty()) {
      Item item = std::move(stack.back());
      stack.pop_back();
      if (!item.visit) {
        list.push_back({item.state, item.edge, std::move(item.log)});
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

Match build_match(const Nfa& nfa, const std::string& label, const std::vector<Event>& events,
                  const std::string& original, const NormalizedText& norm) {
  std::vector<Instance> instances;
  std::vector<int> event_instance(events.size(), -1);
  std::vector<int> open;
  std::vector<std::size_t> deferred;  // qualified assignments
  for (std::size_t i = 0; i < events.size(); ++i) {
    const Action& a = nfa.actions[events[i].action];
    switch (a.kind) {
      case Action::Kind::Open:
        instances.push_back({a.slot, events[i].pos, events[i].pos, i, {}});
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

  Match m;
  m.label = label;
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

}  // namespace

// --- Script -----------------------------------------------------------------

struct CompiledRule {
  std::string label;
  int start;  // start state
  int first;  // the rule's states occupy the id range [first, last)
  int last;
};

struct Script::Impl {
  Nfa nfa;
  std::vector<CompiledRule> rules;
  int start = 0;
  mutable std::mutex mutex;
  mutable std::unique_ptr<LazyDfa> dfa;
};

Script::Script(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Script::Script(Script&&) noexcept = default;
Script& Script::operator=(Script&&) noexcept = default;
Script::~Script() = default;

Script Script::compile(const std::string& source) {
  ParsedScript parsed = parse_script(source);
  auto impl = std::make_unique<Impl>();
  impl->start = impl->nfa.add_state();
  for (const Rule& rule : parsed.rules) {
    CompiledRule r;
    r.label = rule.label;
    r.first = static_cast<int>(impl->nfa.states.size());
    r.start = compile_rule(impl->nfa, parsed.defs, *rule.pattern,
                           static_cast<int>(impl->rules.size()));
    r.last = static_cast<int>(impl->nfa.states.size());
    impl->nfa.epsilon(impl->start, r.start);
    impl->rules.push_back(std::move(r));
  }
  impl->dfa = std::make_unique<LazyDfa>(impl->nfa, impl->start);
  return Script(std::move(impl));
}

Script Script::compile_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + path);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return compile(buffer.str());
}

std::size_t Script::rule_count() const { return impl_->rules.size(); }

std::vector<Match> Script::match(const std::string& text) const {
  NormalizedText norm = normalize(text);
  std::vector<int> rules;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    rules = impl_->dfa->accepted_rules(norm.text);
  }
  std::vector<Match> out;
  if (rules.empty()) return out;
  std::set<std::string> seen;
  std::vector<Event> events;
  for (int id : rules) {
    const CompiledRule& r = impl_->rules[id];
    if (seen.count(r.label)) continue;
    PikeVm vm(impl_->nfa, r.first, r.last);
    if (!vm.run(r.start, norm.text, events)) continue;
    seen.insert(r.label);
    out.push_back(build_match(impl_->nfa, r.label, events, text, norm));
  }
  return out;
}

}  // namespace nlu

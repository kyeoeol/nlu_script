// Replaying accepted rules with a Pike VM, and building matches from them.
#include "replay.hpp"

#include <algorithm>

#include "datetime.hpp"
#include "numbers.hpp"
#include "script_impl.hpp"

namespace nlu {
namespace detail {

namespace {

class PikeVm {
 public:
  // A rule's states occupy the contiguous id range [first, last).
  PikeVm(const Nfa& nfa, int first, int last) : nfa_(nfa), first_(first), s_(scratch()) {
    const std::size_t size = static_cast<std::size_t>(last - first);
    if (s_.mark.size() < size) s_.mark.resize(size, 0);
    s_.log.clear();
  }

  // Runs the rule over the text and returns the actions on the highest
  // priority accepting path, and how many characters count as wildcard text
  // on it: those wildcards and typos consumed, and one per letter a typo left
  // out. Returns false if the rule does not match.
  bool run(int start, const std::u32string& text, std::vector<Event>& events, int& wildcard_chars) {
    std::vector<Thread>& current = s_.current;
    std::vector<Thread>& next = s_.next;
    current.clear();
    next_generation();
    add(current, start, -1, 0, 0, 0);
    std::size_t compact_at = kMinCompact;
    for (std::size_t i = 0; i < text.size() && !current.empty(); ++i) {
      next.clear();
      next_generation();
      const char32_t c = text[i];
      const int pos = static_cast<int>(i) + 1;
      for (const Thread& t : current) {
        if (t.begin < 0) continue;
        const std::vector<Edge>& edges = nfa_.states[t.state].edges;
        for (int k = t.begin; k < t.end; ++k) {
          const Edge& e = edges[k];
          if (nfa_.matches(e, c)) add(next, e.to, t.log, pos, c, t.wild + (e.wildcard ? 1 : 0));
        }
      }
      std::swap(current, next);
      if (s_.log.size() >= compact_at) {
        compact(current);
        compact_at = std::max(kMinCompact, 2 * s_.log.size());
      }
    }
    for (const Thread& t : current) {
      if (t.begin >= 0) continue;
      events.clear();
      for (int k = t.log; k >= 0; k = s_.log[k].prev)
        events.push_back({s_.log[k].action, s_.log[k].pos});
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
    int prev;  // index into log, or -1
  };
  // A state's consuming edges [begin, end) to follow, which come one after
  // another in its edge list, or an accepting thread (begin -1).
  struct Thread {
    int state;
    int begin;
    int end;
    int log;   // index into log, or -1
    int wild;  // wildcard characters so far (see run())
  };
  // Where add() goes on once it has explored the edges before `edge`.
  struct Item {
    int state;
    int edge;
    int log;
    int wild;
  };
  // add()'s edge index for a state it has just reached (kArrived), which it
  // visits from edge 0 unless this step has visited it already (kVisited).
  static constexpr int kArrived = -1, kVisited = -2;
  // Memory that a thread's runs reuse, so a run allocates nothing once the
  // vectors have grown (rules with system entities span thousands of
  // states). mark[s - first] is the generation of the step that last reached
  // state s; each step of each run gets a new one, so marks never need
  // clearing.
  struct Scratch {
    std::vector<unsigned> mark;
    unsigned generation = 0;
    std::vector<Thread> current, next;
    std::vector<LogNode> log;
    std::vector<Item> stack;
  };
  static Scratch& scratch() {
    thread_local Scratch s;
    return s;
  }

  const Nfa& nfa_;
  int first_;
  Scratch& s_;
  // The log is compacted when it reaches this many nodes, then twice the
  // nodes it kept, so the nodes of threads that died don't pile up over long
  // inputs.
  static constexpr std::size_t kMinCompact = 4096;

  void next_generation() {
    if (++s_.generation == 0) {  // wrapped around: old marks could match
      std::fill(s_.mark.begin(), s_.mark.end(), 0);
      s_.generation = 1;
    }
  }

  // Keeps only the log nodes that `threads` reach, in their order (so a
  // node's prev still comes before it), and renumbers the threads' logs.
  void compact(std::vector<Thread>& threads) {
    std::vector<LogNode>& log = s_.log;
    std::vector<bool> keep(log.size(), false);
    for (const Thread& t : threads)
      for (int k = t.log; k >= 0 && !keep[k]; k = log[k].prev) keep[k] = true;
    std::vector<int> renumber(log.size(), -1);
    int n = 0;
    for (std::size_t k = 0; k < log.size(); ++k) {
      if (!keep[k]) continue;
      const int prev = log[k].prev;
      log[n] = {log[k].action, log[k].pos, prev < 0 ? -1 : renumber[prev]};
      renumber[k] = n++;
    }
    log.resize(static_cast<std::size_t>(n));
    for (Thread& t : threads)
      if (t.log >= 0) t.log = renumber[t.log];
  }

  // Depth-first, in edge order, so threads are listed by priority. `prev` is
  // the character consumed last, which guard edges test. A state's wildcard
  // count is that of the first path to reach it. The first passable edge
  // that consumes nothing is followed at once; the stack keeps where to go
  // on in the states left behind.
  void add(std::vector<Thread>& list, int state, int log, int pos, char32_t prev, int wild) {
    std::vector<Item>& stack = s_.stack;
    unsigned* const mark = s_.mark.data();
    const unsigned generation = s_.generation;
    stack.clear();
    int k = kArrived;  // the edge of `state` to go on from
    for (;;) {
      if (k == kArrived) {
        unsigned& m = mark[state - first_];
        k = m == generation ? kVisited : 0;
        m = generation;
      }
      if (k != kVisited) {
        const State& st = nfa_.states[state];
        const int n = static_cast<int>(st.edges.size());
        while (k < n) {
          const Edge& e = st.edges[k];
          if (e.consumes()) {
            int end = k + 1;
            while (end < n && st.edges[end].consumes()) ++end;
            list.push_back({state, k, end, log, wild});
            k = end;
            continue;
          }
          if (e.kind == Edge::Kind::Guard && !guard_holds(e.guard, prev)) {
            ++k;
            continue;
          }
          if (k + 1 < n || st.accept_rule >= 0) stack.push_back({state, k + 1, log, wild});
          if (e.kind == Edge::Kind::Action) {
            s_.log.push_back({e.action, pos, log});
            log = static_cast<int>(s_.log.size()) - 1;
          } else if (e.kind == Edge::Kind::Epsilon && e.wildcard) {
            ++wild;
          }
          state = e.to;
          k = kArrived;
          break;
        }
        if (k == kArrived) continue;
        if (st.accept_rule >= 0) list.push_back({state, -1, -1, log, wild});
      }
      if (stack.empty()) return;
      const Item& item = stack.back();
      state = item.state;
      k = item.edge;
      log = item.log;
      wild = item.wild;
      stack.pop_back();
    }
  }
};

}  // namespace

bool replay(const Nfa& nfa, const CompiledRule& rule, const std::u32string& text, Replay& out) {
  int wildcard_chars = 0;
  out.events.clear();
  if (rule.needs_vm &&
      !PikeVm(nfa, rule.first, rule.last).run(rule.start, text, out.events, wildcard_chars))
    return false;
  const int letters = static_cast<int>(text.size() - std::count(text.begin(), text.end(), U' '));
  out.specificity = letters - wildcard_chars;
  return true;
}

bool needs_pike_vm(const Nfa& nfa, int first, int last) {
  for (int s = first; s < last; ++s)
    for (const Edge& e : nfa.states[s].edges)
      if (e.kind == Edge::Kind::Action || e.wildcard) return true;
  return false;
}

namespace {

struct Instance {
  int slot;
  int open_pos;
  int close_pos;
  std::size_t open_event;
  Attributes attributes;
  Normalizer normalizer;
  int parent;  // the instance this one is nested in, or -1
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

}  // namespace

Match build_match(const Nfa& nfa, const CompiledRule& rule, const Replay& replayed,
                  const std::string& original, const NormalizedText& norm, std::size_t begin,
                  std::size_t end) {
  const std::vector<Event>& events = replayed.events;
  std::vector<Instance> instances;
  std::vector<int> event_instance(events.size(), -1);
  std::vector<int> open;
  std::vector<std::size_t> deferred;  // qualified assignments
  for (std::size_t i = 0; i < events.size(); ++i) {
    const Action& a = nfa.actions[events[i].action];
    switch (a.kind) {
      case Action::Kind::Open:
        instances.push_back({a.slot, events[i].pos, events[i].pos, i, {}, a.normalizer,
                             open.empty() ? -1 : open.back()});
        open.push_back(static_cast<int>(instances.size()) - 1);
        event_instance[i] = open.back();
        break;
      case Action::Kind::Close:
        if (open.empty()) break;  // only in a corrupt loaded script
        instances[open.back()].close_pos = events[i].pos;
        event_instance[i] = open.back();
        open.pop_back();
        break;
      case Action::Kind::Assign:
        if (a.slot < 0) {
          if (!open.empty()) set_attribute(instances[open.back()].attributes, a.key, a.value);
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

  // A slot leaves out a space at either end, which a skipped optional group
  // leaves there (`<s>a (b)</s> c` takes `a` in `a c`), and stays within the
  // slot it is nested in (listed before it).
  for (Instance& inst : instances) {
    while (inst.open_pos < inst.close_pos && norm.text[inst.open_pos] == U' ') ++inst.open_pos;
    while (inst.close_pos > inst.open_pos && norm.text[inst.close_pos - 1] == U' ') --inst.close_pos;
    if (inst.parent >= 0) {
      const Instance& outer = instances[inst.parent];
      inst.open_pos = std::clamp(inst.open_pos, outer.open_pos, outer.close_pos);
      inst.close_pos = std::clamp(inst.close_pos, outer.open_pos, outer.close_pos);
    }
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
  m.specificity = replayed.specificity;
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

}  // namespace detail
}  // namespace nlu

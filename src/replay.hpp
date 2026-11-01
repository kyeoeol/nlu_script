// Phase 2 of matching: for a rule that the DFA found accepting the input, a
// Pike VM over that rule's NFA replays the input with priorities (greedy
// groups, lazy wildcards) to recover slot spans and attribute assignments,
// and build_match() turns them into a Match.
#ifndef NLU_SCRIPT_REPLAY_HPP
#define NLU_SCRIPT_REPLAY_HPP

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "nfa.hpp"
#include "nlu_script/nlu_script.hpp"
#include "unicode.hpp"

namespace nlu {
namespace detail {

struct CompiledRule;  // script_impl.hpp

struct Event {
  int action;
  int pos;
};

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

// Whether the Pike VM can tell anything about a match of the rule with
// states [first, last) that the DFA doesn't: without actions it records no
// slots, and without wildcards their character count is 0.
bool needs_pike_vm(const Nfa& nfa, int first, int last);

// The match of `rule` over the bytes [begin, end) of `original`, which
// contain every event.
Match build_match(const Nfa& nfa, const CompiledRule& rule, int specificity,
                  const std::vector<Event>& events, const std::string& original,
                  const NormalizedText& norm, std::size_t begin, std::size_t end);

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_REPLAY_HPP

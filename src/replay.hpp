// Phase 2 of matching: for a rule that the DFA found accepting the input,
// replay() runs a Pike VM over that rule's NFA, which replays the input with
// priorities (greedy groups, lazy wildcards) to recover slot spans and
// attribute assignments, and build_match() turns them into a Match.
#ifndef NLU_SCRIPT_REPLAY_HPP
#define NLU_SCRIPT_REPLAY_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "nfa.hpp"
#include "nlu_script/nlu_script.hpp"
#include "rule.hpp"
#include "unicode.hpp"

namespace nlu {
namespace detail {

struct Event {
  int action;
  int pos;
};

// A rule's replay over a text it accepts.
struct Replay {
  std::vector<Event> events;  // the actions on the highest priority accepting path
  int specificity = 0;        // see Match::specificity
};

// The states of rules that need the Pike VM whose consuming edges are many
// Char edges in a row, each for a different character, as in the
// determinized grammars of system entities: a hash table for each finds the
// one edge that takes a character, which the Pike VM looks up instead of
// testing every edge. Built with the script, and only read after.
class CharRuns {
 public:
  struct Run {
    int begin, end;  // the state's consuming edges
    int table;       // where its hash table starts in `slots_`
    unsigned mask;   // the table's size, less one
  };

  CharRuns() = default;
  CharRuns(const Nfa& nfa, const std::vector<CompiledRule>& rules);

  // The run of `state`, or nullptr if it has none.
  const Run* find(int state) const {
    const int r = run_of_[static_cast<std::size_t>(state)];
    return r < 0 ? nullptr : &runs_[static_cast<std::size_t>(r)];
  }
  // The edge of `run` that takes c, or -1.
  int edge(const Run& run, char32_t c) const {
    for (unsigned h = hash(c) & run.mask;; h = (h + 1) & run.mask) {
      const Slot& s = slots_[run.table + h];
      if (s.edge < 0 || s.ch == c) return s.edge;
    }
  }

 private:
  struct Slot {
    char32_t ch;
    int edge;  // -1: empty
  };
  static unsigned hash(char32_t c) { return (static_cast<unsigned>(c) * 0x9E3779B1u) >> 16; }

  std::vector<int> run_of_;  // for each state, its index in runs_, or -1
  std::vector<Run> runs_;
  std::vector<Slot> slots_;
};

// Replays `rule` over `text`, which the DFA found it accepts: runs the
// rule's Pike VM if it needs one (see needs_pike_vm()), and works out the
// specificity over text[0, counted) (the rest is trailing punctuation that
// doesn't count; see before_trailing_punctuation()). Returns false if the
// rule does not match after all. `runs` are those of `nfa`.
bool replay(const Nfa& nfa, const CharRuns& runs, const CompiledRule& rule,
            const std::u32string& text, std::size_t counted, Replay& out);

// Whether the Pike VM can tell anything about a match of the rule with
// states [first, last) that the DFA doesn't: without actions it records no
// slots, and without wildcards their character count is 0.
bool needs_pike_vm(const Nfa& nfa, int first, int last);

// The match of `rule` over the bytes [begin, end) of `original`, which
// contain every event of `replayed`; `norm` is `original` normalized.
Match build_match(const Nfa& nfa, const CompiledRule& rule, const Replay& replayed,
                  const std::string& original, const NormalizedText& norm, std::size_t begin,
                  std::size_t end);

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_REPLAY_HPP

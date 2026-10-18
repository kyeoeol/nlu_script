#include "compiler.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "nlu_script/nlu_script.hpp"
#include "system_entities.hpp"

namespace nlu {
namespace detail {

namespace {

constexpr std::size_t kMaxExclusionStates = 20000;

// Repetitions multiply a rule's automaton, so nested ones grow it fast
// ([[[[a]^(1,100)]^(1,100)]^(1,100)]^(1,100) would take gigabytes). Compiling
// stops with an error once a rule passes this many states.
constexpr std::size_t kMaxRuleStates = 500000;

[[noreturn]] void fail(const SourcePos& pos, const std::string& message) {
  throw_at(pos, message);
}

// --- determinization, used to compile exclusions ----------------------------

// Input characters are partitioned into classes, each standing for the
// characters that behave alike, and given by one of them: the space, then
// characters the automaton doesn't mention (one class, or one per CharClass
// when guards or class-limited wildcards tell syllables apart), then each
// mentioned character.
struct Dfa {
  std::vector<std::vector<int>> next;  // next[state][class], -1 = dead
  std::vector<bool> accept;
};

// The CharClasses that unmentioned characters split into.
const CharClass kOtherClasses[] = {CharClass::NotSyllable, CharClass::SyllableNoFinal,
                                   CharClass::SyllableRieul, CharClass::SyllableOtherFinal};

// A character of class `cls` that `chars` (sorted) doesn't mention.
char32_t unmentioned(const std::vector<char32_t>& chars, CharClass cls) {
  auto mentioned = [&](char32_t c) { return std::binary_search(chars.begin(), chars.end(), c); };
  if (cls == CharClass::NotSyllable || cls == CharClass::All) {
    for (char32_t c : std::u32string(U"xyzqjkvw"))
      if (!mentioned(c)) return c;
    char32_t c = 0x4E00;
    while (mentioned(c)) ++c;
    return c;
  }
  char32_t c = cls == CharClass::SyllableNoFinal ? 0xAC00 : cls == CharClass::SyllableRieul ? 0xAC08
                                                                                             : 0xAC01;
  while (mentioned(c)) c += 28;
  return c;
}

// The representative character of each class: see Dfa.
std::vector<char32_t> class_representatives(const std::vector<char32_t>& chars, bool by_class) {
  std::vector<char32_t> reps{U' '};
  if (by_class) {
    for (CharClass cls : kOtherClasses) reps.push_back(unmentioned(chars, cls));
  } else {
    reps.push_back(unmentioned(chars, CharClass::All));
  }
  reps.insert(reps.end(), chars.begin(), chars.end());
  return reps;
}

// Accepting states are the NFA states with an accept_rule. A closure after a
// class sees its representative as the previous character, for guards.
Dfa determinize(const Nfa& nfa, int start, const std::vector<char32_t>& reps,
                const SourcePos& pos) {
  const std::size_t classes = reps.size();
  Closure closure(nfa);
  Dfa dfa;
  std::map<std::vector<int>, int> index;
  std::vector<std::vector<int>> sets;
  auto intern = [&](std::vector<int> set) {
    auto it = index.find(set);
    if (it != index.end()) return it->second;
    if (sets.size() >= kMaxExclusionStates) fail(pos, "exclusion is too complex to compile");
    int id = static_cast<int>(sets.size());
    index.emplace(set, id);
    dfa.accept.push_back(std::any_of(set.begin(), set.end(),
                                     [&](int s) { return nfa.states[s].accept_rule >= 0; }));
    dfa.next.emplace_back(classes, -1);
    sets.push_back(std::move(set));
    return id;
  };
  intern(closure({start}, 0));
  for (std::size_t d = 0; d < sets.size(); ++d) {
    for (std::size_t cls = 0; cls < classes; ++cls) {
      std::vector<int> targets;
      for (int s : sets[d])
        for (const Edge& e : nfa.states[s].edges)
          if (e.consumes() && nfa.matches(e, reps[cls])) targets.push_back(e.to);
      if (targets.empty()) continue;
      int target = intern(closure(std::move(targets), reps[cls]));
      dfa.next[d][cls] = target;
    }
  }
  return dfa;
}

// --- compiler ---------------------------------------------------------------

struct Frag {
  int in;
  int out;
};

class Compiler {
 public:
  Compiler(Nfa& nfa, const Definitions& defs, bool with_actions)
      : nfa_(nfa), defs_(defs), with_actions_(with_actions), first_state_(nfa.states.size()) {}

  Frag compile(const Node& n) {
    if (nfa_.states.size() - first_state_ > kMaxRuleStates)
      fail(n.pos, "rule is too large to compile (over " + std::to_string(kMaxRuleStates) +
                      " automaton states); lower its repetition counts");
    Frag f = compile_body(n);
    if (!with_actions_) return f;
    for (const Assignment& a : n.assignments) {
      if (a.slot.empty() && slot_depth_ == 0)
        fail(a.pos, "attribute '" + a.key +
                        "' is assigned outside of a slot; use {slot." + a.key + "=...}");
      if (!a.slot.empty()) qualified_.push_back(a);
      int action = static_cast<int>(nfa_.actions.size());
      nfa_.actions.push_back(Action{Action::Kind::Assign,
                                    a.slot.empty() ? -1 : nfa_.slot_id(a.slot), a.key, a.value});
      int next = nfa_.add_state();
      nfa_.add_edge(f.out, Edge::Kind::Action, next, 0, -1, action);
      f.out = next;
    }
    return f;
  }

  // Qualified assignments may refer to slots appearing later in the rule, so
  // they are checked once the whole rule is compiled.
  void check_qualified_assignments() const {
    for (const Assignment& a : qualified_)
      if (!slots_seen_.count(a.slot))
        fail(a.pos, "attribute '" + a.key + "' targets slot <" + a.slot +
                        ">, which does not appear in this rule");
  }

 private:
  Nfa& nfa_;
  const Definitions& defs_;
  bool with_actions_;
  std::size_t first_state_;  // the rule's first state
  // The entities and variables being expanded, innermost last. A definition
  // that refers back to itself would expand forever.
  std::vector<std::string> ref_stack_;
  int slot_depth_ = 0;
  std::set<std::string> slots_seen_;
  std::vector<Assignment> qualified_;

  void enter(const SourcePos& pos, const std::string& ref) {
    if (std::find(ref_stack_.begin(), ref_stack_.end(), ref) != ref_stack_.end())
      fail(pos, ref + " refers to itself");
    ref_stack_.push_back(ref);
  }

  void join(int from, int to, bool hard) {
    if (!hard) nfa_.add_edge(from, Edge::Kind::Space, to);
    nfa_.epsilon(from, to);
  }

  Frag compile_body(const Node& n) {
    switch (n.kind) {
      case Node::Kind::Literal:
        return compile_literal(n.text);
      case Node::Kind::Sequence: {
        if (n.children.empty()) {
          int s = nfa_.add_state();
          return {s, s};
        }
        Frag whole = compile(*n.children[0]);
        for (std::size_t i = 1; i < n.children.size(); ++i) {
          Frag f = compile(*n.children[i]);
          join(whole.out, f.in, n.hard_joins[i - 1]);
          whole.out = f.out;
        }
        return whole;
      }
      case Node::Kind::Choice: {
        int in = nfa_.add_state();
        int out = nfa_.add_state();
        for (const NodePtr& child : n.children) {
          Frag f = compile(*child);
          nfa_.epsilon(in, f.in);
          nfa_.epsilon(f.out, out);
        }
        return {in, out};
      }
      case Node::Kind::Optional: {
        int in = nfa_.add_state();
        int out = nfa_.add_state();
        Frag f = compile(*n.children[0]);
        nfa_.epsilon(in, f.in);  // greedy: prefer matching the optional part
        nfa_.epsilon(in, out);
        nfa_.epsilon(f.out, out);
        return {in, out};
      }
      case Node::Kind::Repeat:
        return compile_repeat(n);
      case Node::Kind::Permute:
        return compile_permute(n);
      case Node::Kind::Wildcard:
        if (n.exclusion) return compile_excluded_wildcard(n);
        return compile_wildcard(n.wild);
      case Node::Kind::Slot:
        return compile_slot(n.name, *n.children[0]);
      case Node::Kind::EntityRef: {
        auto it = defs_.entities.find(n.name);
        if (it == defs_.entities.end()) fail(n.pos, "undefined entity @{" + n.name + "}");
        const std::string ref = "@{" + n.name + "}";
        enter(n.pos, ref);
        Frag f = compile_slot(n.name, *it->second.body);
        ref_stack_.pop_back();
        return f;
      }
      case Node::Kind::VarRef: {
        auto it = defs_.variables.find(n.name);
        if (it == defs_.variables.end()) fail(n.pos, "undefined variable @var.{" + n.name + "}");
        enter(n.pos, "@var.{" + n.name + "}");
        Frag f = compile(*it->second.body);
        ref_stack_.pop_back();
        return f;
      }
      case Node::Kind::Typo:
        return compile_typo(n);
      case Node::Kind::Chain: {
        // Each child is compiled once: any child can start the chain, and
        // after one the chain goes on with a later child (preferring the
        // next) or stops. Spaces only ever join two children.
        int in = nfa_.add_state();
        int out = nfa_.add_state();
        std::vector<Frag> parts;
        for (const NodePtr& child : n.children) parts.push_back(compile(*child));
        for (std::size_t i = 0; i < parts.size(); ++i) {
          nfa_.epsilon(in, parts[i].in);
          for (std::size_t j = i + 1; j < parts.size(); ++j) join(parts[i].out, parts[j].in, false);
          nfa_.epsilon(parts[i].out, out);
        }
        return {in, out};
      }
      case Node::Kind::Particle: {
        int in = nfa_.add_state();
        int out = nfa_.add_state();
        const bool ro = n.rieul_as_vowel;
        const std::pair<Guard, const std::u32string*> forms[] = {
            {ro ? Guard::AfterFinalExceptRieul : Guard::AfterFinal, &n.text},
            {ro ? Guard::AfterNoFinalOrRieul : Guard::AfterNoFinal, &n.vowel_text}};
        for (const auto& form : forms) {
          Frag f = compile_literal(*form.second);
          nfa_.guard(in, form.first, f.in);
          nfa_.epsilon(f.out, out);
        }
        return {in, out};
      }
      case Node::Kind::SystemRef: {
        const auto& system = system_entities();
        auto it = system.find(n.name);
        if (it == system.end()) {
          std::string known;
          for (const auto& kv : system) known += (known.empty() ? "" : ", ") + kv.first;
          fail(n.pos, "unknown system entity `@{" + n.name + "}; available: " + known);
        }
        return compile_slot(n.name, *it->second.body, it->second.normalizer);
      }
    }
    fail(n.pos, "internal error: unknown node");
  }

  // [body]&: the body's automaton, then a copy of it for after the one edit.
  // From each state that takes a letter, the edit leads into the copy: any
  // letter in its place (changed), nothing (left out), or any letter before
  // it (added). Exact paths come first, so they win when both match.
  Frag compile_typo(const Node& n) {
    const int lo = static_cast<int>(nfa_.states.size());
    Frag f = compile(*n.children[0]);
    const int hi = static_cast<int>(nfa_.states.size());
    const int offset = hi - lo;
    for (int s = lo; s < hi; ++s) nfa_.add_state();
    for (int s = lo; s < hi; ++s) {
      for (Edge e : nfa_.states[s].edges) {
        e.to += offset;
        nfa_.states[s + offset].edges.push_back(e);
      }
    }
    for (int s = lo; s < hi; ++s) {
      std::vector<int> letters;  // targets of the state's letter edges
      for (const Edge& e : nfa_.states[s].edges)
        if (e.kind == Edge::Kind::Char) letters.push_back(e.to);
      if (letters.empty()) continue;
      for (int to : letters) {
        nfa_.add_edge(s, Edge::Kind::Any, to + offset);
        nfa_.epsilon(s, to + offset);
      }
      nfa_.add_edge(s, Edge::Kind::Any, s + offset);
    }
    int out = nfa_.add_state();
    nfa_.epsilon(f.out, out);
    nfa_.epsilon(f.out + offset, out);
    return {f.in, out};
  }

  Frag compile_literal(const std::u32string& text) {
    int in = nfa_.add_state();
    int cur = in;
    for (char32_t c : text) {
      int next = nfa_.add_state();
      nfa_.add_edge(cur, c == U' ' ? Edge::Kind::Space : Edge::Kind::Char, next, c);
      cur = next;
    }
    return {in, cur};
  }

  Frag compile_slot(const std::string& name, const Node& body,
                    Normalizer normalizer = Normalizer::None) {
    if (!with_actions_) return compile(body);
    int id = nfa_.slot_id(name);
    slots_seen_.insert(name);
    int open = static_cast<int>(nfa_.actions.size());
    nfa_.actions.push_back(Action{Action::Kind::Open, id, "", "", normalizer});
    int close = open + 1;
    nfa_.actions.push_back(Action{Action::Kind::Close, id, "", ""});
    int in = nfa_.add_state();
    int out = nfa_.add_state();
    ++slot_depth_;
    Frag f = compile(body);
    --slot_depth_;
    nfa_.add_edge(in, Edge::Kind::Action, f.in, 0, -1, open);
    nfa_.add_edge(f.out, Edge::Kind::Action, out, 0, -1, close);
    return {in, out};
  }

  Frag compile_repeat(const Node& n) {
    const Node& body = *n.children[0];
    const bool hard = !n.soft_repeat;
    int in = nfa_.add_state();
    int out = nfa_.add_state();
    int cur = in;
    bool first = true;  // no space before the first repetition
    for (int i = 0; i < n.min; ++i) {
      Frag f = compile(body);
      join(cur, f.in, hard || first);
      first = false;
      cur = f.out;
    }
    if (n.max < 0) {
      Frag f = compile(body);
      int loop = nfa_.add_state();
      if (first) {
        nfa_.epsilon(cur, f.in);
        nfa_.epsilon(cur, out);
      } else {
        nfa_.epsilon(cur, loop);
      }
      join(loop, f.in, hard);  // greedy: prefer another repetition
      nfa_.epsilon(loop, out);
      nfa_.epsilon(f.out, loop);
      return {in, out};
    }
    for (int i = n.min; i < n.max; ++i) {
      Frag f = compile(body);
      join(cur, f.in, hard || first);
      nfa_.epsilon(cur, out);
      first = false;
      cur = f.out;
    }
    nfa_.epsilon(cur, out);
    return {in, out};
  }

  Frag compile_permute(const Node& n) {
    std::vector<std::size_t> order(n.children.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    int in = nfa_.add_state();
    int out = nfa_.add_state();
    do {
      Frag whole = compile(*n.children[order[0]]);
      for (std::size_t i = 1; i < order.size(); ++i) {
        Frag f = compile(*n.children[order[i]]);
        join(whole.out, f.in, false);
        whole.out = f.out;
      }
      nfa_.epsilon(in, whole.in);
      nfa_.epsilon(whole.out, out);
    } while (std::next_permutation(order.begin(), order.end()));
    return {in, out};
  }

  // Wildcards never start or end with a space, so the text they capture is
  // well defined next to the optional spaces around them. They are lazy.
  Frag compile_wildcard(Node::WildKind kind) {
    int in = nfa_.add_state();
    int out = nfa_.add_state();
    if (kind == Node::WildKind::One) {
      nfa_.add_edge(in, Edge::Kind::Any, out);
      return {in, out};
    }
    int word = nfa_.add_state();   // inside a word
    int space = nfa_.add_state();  // after a space between words
    if (kind == Node::WildKind::Star) nfa_.epsilon(in, out);
    nfa_.add_edge(in, Edge::Kind::Any, word);
    nfa_.epsilon(word, out);
    nfa_.add_edge(word, Edge::Kind::Any, word);
    nfa_.add_edge(word, Edge::Kind::Space, space);
    nfa_.add_edge(space, Edge::Kind::Any, word);
    return {in, out};
  }

  // Compiles (wildcard minus exclusion) as the product of the wildcard's DFA
  // and the complement of the exclusion's DFA, then embeds the result.
  Frag compile_excluded_wildcard(const Node& n) {
    Nfa scratch;
    Compiler sub(scratch, defs_, false);
    sub.ref_stack_ = ref_stack_;
    Frag x = sub.compile(*n.exclusion);
    Frag w = sub.compile_wildcard(n.wild);
    scratch.states[x.out].accept_rule = 0;
    scratch.states[w.out].accept_rule = 0;

    std::vector<char32_t> chars;
    for (const State& s : scratch.states) {
      for (const Edge& e : s.edges) {
        if (e.kind == Edge::Kind::Char) chars.push_back(e.ch);
        if (e.kind == Edge::Kind::Any && e.set >= 0)
          chars.insert(chars.end(), scratch.sets[e.set].begin(), scratch.sets[e.set].end());
      }
    }
    std::sort(chars.begin(), chars.end());
    chars.erase(std::unique(chars.begin(), chars.end()), chars.end());
    // Particle pairs in the exclusion tell syllables apart by their final
    // consonant, so unmentioned characters then form one class per CharClass.
    const bool by_class = scratch.has_guards;
    const std::vector<char32_t> reps = class_representatives(chars, by_class);
    const std::size_t others = by_class ? 4 : 1;  // classes 1 .. others
    const std::size_t classes = reps.size();

    Dfa dx = determinize(scratch, x.in, reps, n.pos);
    Dfa dw = determinize(scratch, w.in, reps, n.pos);

    // Product states: (exclusion state or -1 once it can no longer match, wildcard state).
    std::map<std::pair<int, int>, int> index;
    std::vector<std::pair<int, int>> pairs;
    std::vector<std::vector<int>> next;
    std::vector<bool> accept;
    auto intern = [&](std::pair<int, int> p) {
      auto it = index.find(p);
      if (it != index.end()) return it->second;
      if (pairs.size() >= kMaxExclusionStates) fail(n.pos, "exclusion is too complex to compile");
      int id = static_cast<int>(pairs.size());
      index.emplace(p, id);
      pairs.push_back(p);
      next.emplace_back(classes, -1);
      accept.push_back(dw.accept[p.second] && !(p.first >= 0 && dx.accept[p.first]));
      return id;
    };
    intern({0, 0});
    for (std::size_t i = 0; i < pairs.size(); ++i) {
      for (std::size_t cls = 0; cls < classes; ++cls) {
        int nw = dw.next[pairs[i].second][cls];
        if (nw < 0) continue;
        int nx = pairs[i].first < 0 ? -1 : dx.next[pairs[i].first][cls];
        int target = intern({nx, nw});
        next[i][cls] = target;
      }
    }

    // Keep only states from which an accepting state is reachable.
    std::vector<bool> live(accept);
    for (bool changed = true; changed;) {
      changed = false;
      for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (live[i]) continue;
        for (int t : next[i]) {
          if (t >= 0 && live[t]) {
            live[i] = true;
            changed = true;
            break;
          }
        }
      }
    }

    std::vector<int> state(pairs.size());
    for (std::size_t i = 0; i < pairs.size(); ++i) state[i] = nfa_.add_state();
    int out = nfa_.add_state();
    for (std::size_t i = 0; i < pairs.size(); ++i) {
      if (!live[i]) continue;
      auto target = [&](std::size_t cls) {
        int t = next[i][cls];
        return t >= 0 && live[t] ? t : -1;
      };
      if (accept[i]) nfa_.epsilon(state[i], out);  // lazy: prefer stopping
      if (target(0) >= 0) nfa_.add_edge(state[i], Edge::Kind::Space, state[target(0)]);
      // When every class of unmentioned characters goes to one state, one
      // wildcard edge covers them and the mentioned characters that go there
      // too; otherwise each class gets its own edge and every mentioned
      // character its own.
      bool same = true;
      for (std::size_t k = 2; k <= others; ++k) same = same && target(k) == target(1);
      const int other = same ? target(1) : -2;
      std::vector<char32_t> exceptions;
      for (std::size_t k = 0; k < chars.size(); ++k) {
        int t = target(others + 1 + k);
        if (t == other) continue;
        exceptions.push_back(chars[k]);
        if (t >= 0) nfa_.add_edge(state[i], Edge::Kind::Char, state[t], chars[k]);
      }
      int set = -1;
      if (!exceptions.empty()) {
        set = static_cast<int>(nfa_.sets.size());
        nfa_.sets.push_back(std::move(exceptions));
      }
      if (same) {
        if (other >= 0) nfa_.add_edge(state[i], Edge::Kind::Any, state[other], 0, set);
      } else {
        for (std::size_t k = 1; k <= others; ++k)
          if (target(k) >= 0) nfa_.any_of_class(state[i], state[target(k)], set, kOtherClasses[k - 1]);
      }
    }
    return {state[0], out};
  }
};

}  // namespace

int compile_rule(Nfa& nfa, const Definitions& defs, const Node& pattern, int rule,
                 bool trailing_punctuation) {
  Compiler compiler(nfa, defs, true);
  Frag f = compiler.compile(pattern);
  compiler.check_qualified_assignments();
  int accept = nfa.add_state();
  nfa.states[accept].accept_rule = rule;
  nfa.epsilon(f.out, accept);
  if (trailing_punctuation) {
    // [space] punctuation+ before the end: "are you there ?!", "거기 계세요~~".
    static const char32_t kPunctuation[] = U".?!~,;:\u2026\uFF0E\uFF1F\uFF01\uFF5E\u3002\u3001\uFF0C";
    int before = nfa.add_state();
    int marks = nfa.add_state();
    nfa.add_edge(f.out, Edge::Kind::Space, before);
    nfa.epsilon(f.out, before);
    for (const char32_t* p = kPunctuation; *p; ++p) {
      nfa.add_edge(before, Edge::Kind::Char, marks, *p);
      nfa.add_edge(marks, Edge::Kind::Char, marks, *p);
    }
    nfa.epsilon(marks, accept);
  }
  return f.in;
}

}  // namespace detail
}  // namespace nlu

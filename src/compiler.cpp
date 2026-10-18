#include "compiler.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "nlu_script/nlu_script.hpp"

namespace nlu {
namespace detail {

namespace {

constexpr std::size_t kMaxExclusionStates = 20000;

[[noreturn]] void fail(const SourcePos& pos, const std::string& message) {
  throw CompileError(message, pos.line, pos.column);
}

// --- system entities ------------------------------------------------------

std::shared_ptr<Node> node(Node::Kind kind) {
  auto n = std::make_shared<Node>();
  n->kind = kind;
  return n;
}

NodePtr literal(const std::u32string& text) {
  auto n = node(Node::Kind::Literal);
  n->text = text;
  return n;
}

NodePtr choice(std::vector<NodePtr> options) {
  auto n = node(Node::Kind::Choice);
  n->children = std::move(options);
  return n;
}

NodePtr optional(NodePtr body) {
  auto n = node(Node::Kind::Optional);
  n->children.push_back(std::move(body));
  return n;
}

NodePtr digit() {
  std::vector<NodePtr> digits;
  for (char32_t c = U'0'; c <= U'9'; ++c) digits.push_back(literal(std::u32string(1, c)));
  return choice(std::move(digits));
}

// `body` repeated `min` to `max` (-1: unbounded) times, without spaces.
NodePtr repeated(NodePtr body, int min, int max) {
  auto n = node(Node::Kind::Repeat);
  n->children.push_back(std::move(body));
  n->min = min;
  n->max = max;
  n->soft_repeat = false;
  return n;
}

NodePtr digits(int min, int max) { return repeated(digit(), min, max); }

// Tokens joined without spaces.
NodePtr glued(std::vector<NodePtr> items) {
  auto n = node(Node::Kind::Sequence);
  n->hard_joins.assign(items.size() - 1, true);
  n->children = std::move(items);
  return n;
}

// Tokens that may be separated by a space.
NodePtr spaced(std::vector<NodePtr> items) {
  auto n = node(Node::Kind::Sequence);
  n->hard_joins.assign(items.size() - 1, false);
  n->children = std::move(items);
  return n;
}

NodePtr literals(const char32_t* const* words) {
  std::vector<NodePtr> options;
  for (; *words; ++words) options.push_back(literal(*words));
  return choice(std::move(options));
}

// Numbers written with digits and Korean numerals: 35000, 35,000, 3만 5천,
// 삼만오천, 2,500만, 1억 2천만. Below 만 a number is either digits or
// numerals (with single digits allowed for numerals: 3천5백); 만, 억 and 조
// groups may be separated by spaces. A space only ever separates two parts,
// so the entity never ends in one.
NodePtr korean_number() {
  static const char32_t* const kSinoDigits[] = {U"일", U"이", U"삼", U"사", U"오",
                                                U"육", U"칠", U"팔", U"구", nullptr};
  static const char32_t* const kDigits[] = {U"1", U"2", U"3", U"4", U"5", U"6", U"7",
                                            U"8", U"9", nullptr};
  NodePtr sino = literals(kSinoDigits);
  NodePtr d = choice({sino, literals(kDigits)});
  NodePtr comma_groups =
      glued({digits(1, 3), repeated(glued({literal(U","), digits(3, 3)}), 1, -1)});
  NodePtr arabic = choice({comma_groups, digits(1, -1)});

  // Below 10,000 in numerals: [d]천 [d]백 [d]십 [d], highest unit first.
  NodePtr ten = glued({optional(d), literal(U"십"), optional(d)});
  NodePtr hundred = glued({optional(d), literal(U"백"), optional(choice({ten, d}))});
  NodePtr thousand = glued({optional(d), literal(U"천"), optional(choice({hundred, ten, d}))});
  NodePtr small = choice({arabic, thousand, hundred, ten, sino});

  // [small]만 [small], [small]억 [만 part | small], [small]조 [억 part | ...].
  auto group = [&](const char32_t* unit) { return glued({optional(small), literal(unit)}); };
  auto part = [&](const char32_t* unit, std::vector<NodePtr> lower) {
    std::vector<NodePtr> options;
    for (NodePtr& l : lower) options.push_back(spaced({group(unit), l}));
    options.push_back(group(unit));
    return choice(std::move(options));
  };
  NodePtr man = part(U"만", {small});
  NodePtr eok = part(U"억", {man, small});
  NodePtr jo = part(U"조", {eok, man, small});
  return choice({jo, eok, man, small});
}

struct SystemEntity {
  NodePtr body;
  Normalizer normalizer;
};

const std::map<std::string, SystemEntity>& system_entities() {
  static const std::map<std::string, SystemEntity> entities = [] {
    std::map<std::string, SystemEntity> m;
    m["NUMBER"] = {digits(1, -1), Normalizer::None};
    NodePtr separator = optional(choice({literal(U"-"), literal(U" ")}));
    m["TELNUM"] = {glued({literal(U"0"), digits(1, 2), separator, digits(3, 4), separator,
                          digits(4, 4)}),
                   Normalizer::None};
    m["KO_NUMBER"] = {korean_number(), Normalizer::KoreanNumber};
    return m;
  }();
  return entities;
}

// --- determinization, used to compile exclusions ----------------------------

// Input characters are partitioned into classes: 0 = space, 1 = any character
// not mentioned by the automaton, 2 + k = chars[k].
struct Dfa {
  std::vector<std::vector<int>> next;  // next[state][class], -1 = dead
  std::vector<bool> accept;
};

bool class_matches(const Nfa& nfa, const Edge& e, std::size_t cls,
                   const std::vector<char32_t>& chars) {
  if (cls == 0) return e.kind == Edge::Kind::Space;
  if (cls == 1) return e.kind == Edge::Kind::Any;
  return nfa.matches(e, chars[cls - 2]);
}

// Accepting states are the NFA states with an accept_rule. Exclusions contain
// no guard edges, so closures need no previous character.
Dfa determinize(const Nfa& nfa, int start, const std::vector<char32_t>& chars,
                const SourcePos& pos) {
  const std::size_t classes = chars.size() + 2;
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
          if (e.consumes() && class_matches(nfa, e, cls, chars)) targets.push_back(e.to);
      if (targets.empty()) continue;
      int target = intern(closure(std::move(targets), 0));
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
      : nfa_(nfa), defs_(defs), with_actions_(with_actions) {}

  Frag compile(const Node& n) {
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
  std::vector<std::string> var_stack_;
  bool in_exclusion_ = false;
  int slot_depth_ = 0;
  std::set<std::string> slots_seen_;
  std::vector<Assignment> qualified_;

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
        return compile_slot(n.name, *it->second.body);
      }
      case Node::Kind::VarRef: {
        auto it = defs_.variables.find(n.name);
        if (it == defs_.variables.end()) fail(n.pos, "undefined variable @var.{" + n.name + "}");
        if (std::find(var_stack_.begin(), var_stack_.end(), n.name) != var_stack_.end())
          fail(n.pos, "variable @var.{" + n.name + "} refers to itself");
        var_stack_.push_back(n.name);
        Frag f = compile(*it->second.body);
        var_stack_.pop_back();
        return f;
      }
      case Node::Kind::Particle: {
        // Exclusions are determinized over character classes that don't
        // tell syllables with a final consonant from those without.
        if (in_exclusion_) fail(n.pos, "particle pairs cannot be used inside an exclusion");
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
    sub.var_stack_ = var_stack_;
    sub.in_exclusion_ = true;
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
    const std::size_t classes = chars.size() + 2;

    Dfa dx = determinize(scratch, x.in, chars, n.pos);
    Dfa dw = determinize(scratch, w.in, chars, n.pos);

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
      int other = target(1);
      std::vector<char32_t> exceptions;
      for (std::size_t k = 0; k < chars.size(); ++k) {
        int t = target(k + 2);
        if (t == other) continue;
        exceptions.push_back(chars[k]);
        if (t >= 0) nfa_.add_edge(state[i], Edge::Kind::Char, state[t], chars[k]);
      }
      if (other >= 0) {
        int set = -1;
        if (!exceptions.empty()) {
          set = static_cast<int>(nfa_.sets.size());
          nfa_.sets.push_back(std::move(exceptions));
        }
        nfa_.add_edge(state[i], Edge::Kind::Any, state[other], 0, set);
      }
    }
    return {state[0], out};
  }
};

}  // namespace

int compile_rule(Nfa& nfa, const Definitions& defs, const Node& pattern, int rule) {
  Compiler compiler(nfa, defs, true);
  Frag f = compiler.compile(pattern);
  compiler.check_qualified_assignments();
  int accept = nfa.add_state();
  nfa.states[accept].accept_rule = rule;
  nfa.epsilon(f.out, accept);
  return f.in;
}

}  // namespace detail
}  // namespace nlu

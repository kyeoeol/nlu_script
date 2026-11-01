// Script compilation and matching.
//
// Matching runs in two phases:
//  1. A lazily built DFA over the union of all rules (lazy_dfa.hpp) finds
//     which rules accept the whole input. Its cost per character does not
//     grow with the number of rules once the relevant DFA states are cached.
//  2. For each accepted rule, a Pike VM over that rule's NFA (replay.hpp)
//     replays the input with priorities (greedy groups, lazy wildcards) to
//     recover slot spans and attribute assignments.
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "compiler.hpp"
#include "nlu_script/nlu_script.hpp"
#include "replay.hpp"
#include "script_impl.hpp"
#include "script_source.hpp"
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

// --- Script -----------------------------------------------------------------

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
  ParsedScript parsed = parse_script(source, file);
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
      return std::make_tuple(file_index(parsed.files, d.file), d.line, d.column, d.message);
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
  impl->trailing_punctuation = parsed.options.ignore_trailing_punctuation;
  impl->unused = unused_definitions(parsed);
  impl->files.assign(parsed.files.begin(), parsed.files.end());
  impl->lint_directives = std::move(parsed.lint_directives);
  impl->index_rules();
  impl->shared_dfa = impl->make_dfa(impl->cache_options);
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
  impl_->shared_dfa = impl_->make_dfa(options);
  impl_->cache_id = Impl::next_id();
  impl_->alive = std::make_shared<const int>(0);
}

CacheOptions Script::cache_options() const { return impl_->cache_options; }

std::vector<Explanation> Script::explain(const std::string& text, std::size_t limit) const {
  const Impl& impl = *impl_;
  const NormalizedText norm = normalize(text, impl.fold_width);
  const std::vector<int>& rule_of = impl.rule_of;

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
      const int r = rule_of[states[k]];
      std::size_t end = k + 1;
      while (end < states.size() && rule_of[states[end]] == r) ++end;
      if (r >= 0) {
        if (i == norm.text.size())
          for (std::size_t j = k; j < end; ++j)
            if (impl.nfa.states[states[j]].accept_rule == r) complete[r] = true;
        if (i > 0 || norm.text.empty()) {
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
  // A rule that matched none of the input is left out, unless the input is
  // empty and the rule matches it.
  for (std::size_t r = 0; r < n; ++r)
    if (furthest[r] > 0 || complete[r]) order.push_back(r);
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
    const std::size_t matched = furthest[r] > 0 ? norm.end[furthest[r] - 1] : 0;
    out.push_back({rule.label, rule.line, rule.file, matched, complete[r],
                   std::vector<std::string>(next.begin(), next.end())});
  }
  return out;
}

std::vector<Match> Script::match(const std::string& text) const {
  return impl_->match(text, false);
}

std::vector<Match> Script::match_best(const std::string& text) const {
  return impl_->match(text, true);
}

std::size_t Script::Impl::counted_length(const std::u32string& text) const {
  return trailing_punctuation ? before_trailing_punctuation(text) : text.size();
}

// Picks one rule per matching label: its first matching rule, or with `best`
// its most specific one. Labels keep the order of their first matching rule.
std::vector<Match> Script::Impl::match(const std::string& text, bool best) const {
  NormalizedText norm = normalize(text, fold_width);
  std::vector<int> accepted = cache().accepted_rules(norm.text);
  if (accepted.empty()) return {};

  struct Found {
    int rule;
    Replay replayed;
  };
  std::vector<Found> found;
  std::map<std::string, std::size_t> label_index;
  const std::size_t counted = counted_length(norm.text);
  Replay replayed;
  for (int id : accepted) {
    const CompiledRule& r = rules[id];
    auto it = label_index.find(r.label);
    if (it != label_index.end() && !best) continue;
    if (!replay(nfa, r, norm.text, counted, replayed)) continue;
    Found f{id, std::move(replayed)};
    if (it == label_index.end()) {
      label_index.emplace(r.label, found.size());
      found.push_back(std::move(f));
    } else if (f.replayed.specificity > found[it->second].replayed.specificity) {
      found[it->second] = std::move(f);
    }
  }

  int top = 0;
  for (const Found& f : found) top = std::max(top, f.replayed.specificity);
  std::vector<Match> out;
  for (const Found& f : found) {
    if (best && f.replayed.specificity < top) continue;
    out.push_back(build_match(nfa, rules[f.rule], f.replayed, text, norm, 0, text.size()));
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

  LazyDfa& dfa = cache();
  std::vector<Match> out;
  Replay replayed;
  std::vector<int> accepts;  // the rules accepting t[i, end)
  for (std::size_t i = 0; i < n;) {
    if (t[i] == U' ' || !boundary(i)) {
      ++i;
      continue;
    }
    // The longest text from i that some rule matches, ending at a boundary.
    std::size_t end = i;
    dfa.run(t, i, [&](std::size_t k, const std::vector<int>& rules_here) {
      if (k > i && !rules_here.empty() && t[k - 1] != U' ' && boundary(k)) {
        end = k;
        accepts = rules_here;
      }
      return true;
    });
    if (end == i) {
      ++i;
      continue;
    }
    const std::u32string span = t.substr(i, end - i);
    const std::size_t counted = counted_length(span);
    std::set<std::string> seen;
    for (int id : accepts) {
      const CompiledRule& r = rules[id];
      if (seen.count(r.label)) continue;
      if (!replay(nfa, r, span, counted, replayed)) continue;
      seen.insert(r.label);
      for (Event& e : replayed.events) e.pos += static_cast<int>(i);
      out.push_back(build_match(nfa, r, replayed, text, norm, norm.begin[i], norm.end[end - 1]));
    }
    i = end;
  }
  return out;
}

}  // namespace nlu

// The compiled script that script.cpp, lint.cpp and save_load.cpp share.
#ifndef NLU_SCRIPT_SCRIPT_IMPL_HPP
#define NLU_SCRIPT_SCRIPT_IMPL_HPP

#include <atomic>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "lazy_dfa.hpp"
#include "nfa.hpp"
#include "nlu_script/nlu_script.hpp"
#include "replay.hpp"
#include "rule.hpp"
#include "script_source.hpp"

namespace nlu {

struct Script::Impl {
  detail::Nfa nfa;
  bool fold_width = false;  // `option fold-width`: input is width-folded too
  // `option ignore-trailing-punctuation`: rules may end with punctuation,
  // which doesn't count towards specificity.
  bool trailing_punctuation = false;
  std::vector<detail::CompiledRule> rules;
  std::vector<int> rule_of;  // the rule owning each state, -1 for the start state
  detail::CharRuns char_runs;  // for the Pike VM
  std::vector<UnusedDefinition> unused;
  std::vector<std::string> files;  // the main script, then included files as they are read
  std::vector<detail::LintDirective> lint_directives;  // their `file` indexes `files`
  int start = 0;

  // The DFA cache: one shared, or one per thread (see cache()).
  CacheOptions cache_options;
  mutable std::unique_ptr<detail::LazyDfa> shared_dfa;
  // Per-thread caches are keyed by `cache_id` and dropped once `alive` expires;
  // set_cache_options() replaces both, retiring the caches built before.
  std::uint64_t cache_id = next_id();
  std::shared_ptr<const int> alive = std::make_shared<const int>(0);

  static std::uint64_t next_id() {
    static std::atomic<std::uint64_t> counter{0};
    return ++counter;
  }

  // A new DFA cache with these options.
  std::unique_ptr<detail::LazyDfa> make_dfa(const CacheOptions& options) const {
    return std::make_unique<detail::LazyDfa>(nfa, rule_of, start, options);
  }

  // Sets rule_of from the rules' state ranges, and char_runs, once they are
  // all compiled.
  void index_rules() {
    rule_of = detail::rule_owners(nfa.states.size(), rules);
    char_runs = detail::CharRuns(nfa, rules);
  }

  detail::LazyDfa& cache() const {
    if (!cache_options.per_thread) return *shared_dfa;
    struct ThreadCache {
      std::weak_ptr<const int> alive;
      std::unique_ptr<detail::LazyDfa> dfa;
    };
    thread_local std::unordered_map<std::uint64_t, ThreadCache> caches;
    auto it = caches.find(cache_id);
    if (it != caches.end()) return *it->second.dfa;
    for (auto i = caches.begin(); i != caches.end();)  // drop caches of scripts that are gone
      i = i->second.alive.expired() ? caches.erase(i) : std::next(i);
    ThreadCache& c = caches[cache_id];
    c = {alive, make_dfa(cache_options)};
    return *c.dfa;
  }

  // How much of `text` (normalized) counts towards specificity: all of it,
  // or with trailing_punctuation, what comes before the punctuation at its end.
  std::size_t counted_length(const std::u32string& text) const;

  std::vector<Match> match(const std::string& text, bool best) const;
  std::vector<Match> search(const std::string& text) const;
};

}  // namespace nlu

#endif  // NLU_SCRIPT_SCRIPT_IMPL_HPP

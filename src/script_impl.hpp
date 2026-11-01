// The compiled script that script.cpp, lint.cpp and save_load.cpp share.
#ifndef NLU_SCRIPT_SCRIPT_IMPL_HPP
#define NLU_SCRIPT_SCRIPT_IMPL_HPP

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "lazy_dfa.hpp"
#include "nfa.hpp"
#include "nlu_script/nlu_script.hpp"
#include "script_source.hpp"

namespace nlu {
namespace detail {

struct CompiledRule {
  std::string label;
  int line;
  std::string file;
  int start;  // start state
  int first;  // the rule's states occupy the id range [first, last)
  int last;
  bool needs_vm;  // see needs_pike_vm()
};

}  // namespace detail

struct Script::Impl {
  detail::Nfa nfa;
  bool fold_width = false;  // `option fold-width`: input is width-folded too
  std::vector<detail::CompiledRule> rules;
  std::vector<int> rule_of;  // the rule owning each state, -1 for the start state
  std::vector<UnusedDefinition> unused;
  std::deque<std::string> files;  // what lint_directives' positions point into
  std::vector<detail::LintDirective> lint_directives;
  int start = 0;

  // The DFA cache: one shared, or one per thread (see cache()).
  CacheOptions cache_options;
  mutable std::unique_ptr<detail::LazyDfa> dfa;
  // Per-thread caches are keyed by `id` and dropped once `alive` expires;
  // set_cache_options() replaces both, retiring the caches built before.
  std::uint64_t id = next_id();
  std::shared_ptr<const int> alive = std::make_shared<const int>(0);

  static std::uint64_t next_id() {
    static std::atomic<std::uint64_t> counter{0};
    return ++counter;
  }

  // A new DFA cache with these options.
  std::unique_ptr<detail::LazyDfa> make_dfa(const CacheOptions& options) const {
    return std::make_unique<detail::LazyDfa>(nfa, rule_of, start, options);
  }

  // Sets rule_of from the rules' state ranges, once they are all compiled.
  void index_rules() {
    rule_of.assign(nfa.states.size(), -1);
    for (std::size_t r = 0; r < rules.size(); ++r)
      std::fill(rule_of.begin() + rules[r].first, rule_of.begin() + rules[r].last, static_cast<int>(r));
  }

  detail::LazyDfa& cache() const {
    if (!cache_options.per_thread) return *dfa;
    struct ThreadCache {
      std::weak_ptr<const int> alive;
      std::unique_ptr<detail::LazyDfa> dfa;
    };
    thread_local std::unordered_map<std::uint64_t, ThreadCache> caches;
    auto it = caches.find(id);
    if (it != caches.end()) return *it->second.dfa;
    for (auto i = caches.begin(); i != caches.end();)  // drop caches of scripts that are gone
      i = i->second.alive.expired() ? caches.erase(i) : std::next(i);
    ThreadCache& c = caches[id];
    c = {alive, make_dfa(cache_options)};
    return *c.dfa;
  }

  std::vector<Match> match(const std::string& text, bool best) const;
  std::vector<Match> search(const std::string& text) const;
};

}  // namespace nlu

#endif  // NLU_SCRIPT_SCRIPT_IMPL_HPP

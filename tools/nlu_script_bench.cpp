// Times matching against generated scripts of growing size:
//
//   nlu_script_bench [--max-states N] [--threads N] [--per-thread] [RULES...]
//
// (default sizes: 10 1000 10000 50000 100000; --max-states caps the DFA cache,
// --threads matches from several threads at once and reports their combined
// throughput, --per-thread gives each its own cache)
//
// Each rule is "order <n> for customer <i> (please)" under its own label; the
// inputs cycle through them, so every match finds one rule. Reports compile
// time, the time per match() once the DFA cache is warm, the time per
// search() of free text around 200 of the inputs ("please ... today", warm
// as well), and the DFA cache's size after both.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "nlu_script/nlu_script.hpp"

namespace {

double seconds_since(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<int> sizes;
  nlu::CacheOptions options;
  int threads = 1;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--max-states" && i + 1 < argc) {
      options.max_states = static_cast<std::size_t>(std::atol(argv[++i]));
      continue;
    }
    if (std::string(argv[i]) == "--threads" && i + 1 < argc) {
      threads = std::max(1, std::atoi(argv[++i]));
      continue;
    }
    if (std::string(argv[i]) == "--per-thread") {
      options.per_thread = true;
      continue;
    }
    sizes.push_back(std::atoi(argv[i]));
  }
  if (sizes.empty()) sizes = {10, 1000, 10000, 50000, 100000};
  for (int rules : sizes) {
    std::string source;
    for (int i = 0; i < rules; ++i)
      source += "label" + std::to_string(i) + "\n    order " + std::to_string(i * 7919) +
                " for customer " + std::to_string(i) + " (please)\n";
    auto start = std::chrono::steady_clock::now();
    nlu::Script script = nlu::Script::compile(source);
    const double compile = seconds_since(start);
    script.set_cache_options(options);

    std::vector<std::string> inputs;
    for (int k = 0; k < 2000; ++k) {
      int i = (k * 7) % rules;
      inputs.push_back("order " + std::to_string(i * 7919) + " for customer " + std::to_string(i));
    }
    std::vector<std::string> texts;
    for (std::size_t k = 0; k < 200; ++k) texts.push_back("please " + inputs[k] + " today");
    const int rounds = 5;
    // Runs work(t) on every thread; returns the time per call of `calls`
    // calls per thread, across all threads.
    auto time_threads = [&](auto work, std::size_t calls) {
      auto begin = std::chrono::steady_clock::now();
      std::vector<std::thread> pool;
      for (int t = 0; t < threads; ++t) pool.emplace_back(work, t);
      for (std::thread& t : pool) t.join();
      return seconds_since(begin) / static_cast<double>(calls * static_cast<std::size_t>(threads));
    };
    // Warm-up included in both.
    std::vector<std::size_t> found(static_cast<std::size_t>(threads), 0);
    const double per_match = time_threads(
        [&](int t) {
          for (const std::string& text : inputs) script.match(text);  // warm the cache
          for (int r = 0; r < rounds; ++r)
            for (const std::string& text : inputs) found[t] += script.match(text).size();
        },
        (rounds + 1) * inputs.size());
    std::vector<std::size_t> searched(static_cast<std::size_t>(threads), 0);
    const double per_search = time_threads(
        [&](int t) {
          for (const std::string& text : texts) script.search(text);
          for (int r = 0; r < rounds; ++r)
            for (const std::string& text : texts) searched[t] += script.search(text).size();
        },
        (rounds + 1) * texts.size());
    nlu::CacheStats stats = script.cache_stats();
    std::cout << rules << " rules: compile " << compile * 1000 << " ms, match "
              << per_match * 1e6 << " us (" << found[0] / rounds << " found), search "
              << per_search * 1e6 << " us (" << searched[0] / rounds << " found), cache "
              << stats.states << " states, " << stats.bytes / 1024 << " KiB, " << stats.evictions
              << " evictions\n";
  }
  return 0;
}

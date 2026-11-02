// Behavior of Script::explain(): how far each rule got into an input.
#include <string>
#include <thread>
#include <vector>

#include "harness.hpp"
#include "nlu_script/nlu_script.hpp"

namespace {

const char* kScript =
    "card:lost\n"
    "    I lost my [credit|debit] card\n"
    "    체크카드 분실\n"
    "agent:connect\n"
    "    talk to an agent\n"
    "report:any\n"
    "    I <?>+? now\n";

std::string expected(const nlu::Explanation& e) {
  std::string out;
  for (const std::string& x : e.expected) out += (out.empty() ? "" : " ") + x;
  return out;
}

}  // namespace

TEST(explain_shows_how_far_rules_got_and_what_they_wanted_next) {
  nlu::Script s = nlu::Script::compile(kScript);
  std::vector<nlu::Explanation> found = s.explain("I lost my wallet");
  CHECK_EQ(found.size(), 2u);
  if (found.size() != 2) return;
  // report:any gets through the whole input but still wants " now".
  CHECK_EQ(found[0].label, std::string("report:any"));
  CHECK_EQ(found[0].matched, std::string("I lost my wallet").size());
  CHECK(!found[0].complete);
  // More wildcard text, or " now" (the space before it is optional).
  CHECK_EQ(expected(found[0]), std::string("any character n space"));
  CHECK_EQ(found[1].label, std::string("card:lost"));
  CHECK_EQ(found[1].line, 2);
  CHECK_EQ(found[1].matched, std::string("I lost my ").size());
  CHECK_EQ(expected(found[1]), std::string("c d"));
}

TEST(explain_reports_offsets_in_the_original_input) {
  nlu::Script s = nlu::Script::compile(kScript);
  const std::string input = "  체크카드   해지";
  std::vector<nlu::Explanation> found = s.explain(input);
  CHECK_EQ(found.size(), 1u);
  if (found.size() != 1) return;
  CHECK_EQ(found[0].line, 3);
  CHECK_EQ(input.substr(0, found[0].matched), std::string("  체크카드   "));
  CHECK_EQ(expected(found[0]), std::string("분"));
}

TEST(explain_marks_complete_matches_and_respects_the_limit) {
  nlu::Script s = nlu::Script::compile(kScript);
  std::vector<nlu::Explanation> found = s.explain("I lost my debit card now", 1);
  CHECK_EQ(found.size(), 1u);
  if (found.size() == 1) {
    CHECK_EQ(found[0].label, std::string("report:any"));
    CHECK(found[0].complete);
  }
  CHECK(s.explain("hello").empty());  // no rule gets past the first character
}

TEST(explain_reports_rules_that_match_the_empty_input) {
  nlu::Script s = nlu::Script::compile("greet\n    (hello)\nask\n    hi\n");
  CHECK_EQ(s.match("").size(), 1u);
  for (const char* input : {"", "  "}) {
    std::vector<nlu::Explanation> found = s.explain(input);
    CHECK_EQ(found.size(), 1u);
    if (found.size() != 1) continue;
    CHECK_EQ(found[0].label, std::string("greet"));
    CHECK(found[0].complete);
    CHECK_EQ(found[0].matched, 0u);
    CHECK_EQ(expected(found[0]), std::string("h"));
  }
  // A rule that matches none of a longer input is still left out.
  CHECK(s.explain("x").empty());
}

namespace {

std::string describe(const std::vector<nlu::Explanation>& found) {
  std::string out;
  for (const nlu::Explanation& e : found) {
    out += e.label + ":" + std::to_string(e.line) + "@" + std::to_string(e.matched) +
           (e.complete ? " complete" : "") + " [" + expected(e) + "]\n";
  }
  return out;
}

// kScript with a system entity and many similar rules, so explaining takes
// many DFA states.
std::string cache_script() {
  std::string source = std::string(kScript) + "agent:callback\n    `@{DATETIME}::(에) 전화 주세요\n";
  for (int i = 0; i < 60; ++i)
    source += "order" + std::to_string(i) + "\n    order " + std::to_string(i * 7919) +
              " for customer " + std::to_string(i) + "\n";
  return source;
}

std::vector<std::string> cache_texts() {
  std::vector<std::string> out = {"I lost my wallet",       "  체크카드   해지",
                                  "I lost my debit card now", "내일 오후 3시에 전화",
                                  "talk to an",             "",
                                  "hello"};
  for (int i = 0; i < 60; i += 7)
    out.push_back("order " + std::to_string(i * 7919) + " for customer " + std::to_string(i + 1));
  return out;
}

}  // namespace

TEST(explain_fills_and_reuses_the_shared_cache) {
  nlu::Script s = nlu::Script::compile(cache_script());
  const std::string text = "I lost my credit wallet";
  const std::string found = describe(s.explain(text, 5));
  const std::size_t states = s.cache_stats().states;
  CHECK(states > 1u);  // explain filled the cache
  CHECK_EQ(describe(s.explain(text, 5)), found);
  CHECK_EQ(s.cache_stats().states, states);  // and explaining it again added nothing
}

TEST(explain_is_unchanged_by_a_cache_that_evicts) {
  nlu::Script reference = nlu::Script::compile(cache_script());
  std::vector<std::string> expected_out;
  for (const std::string& text : cache_texts()) expected_out.push_back(describe(reference.explain(text, 5)));
  for (std::size_t max_states : {2, 3, 5, 17}) {
    nlu::Script s = nlu::Script::compile(cache_script());
    nlu::CacheOptions options;
    options.max_states = max_states;
    s.set_cache_options(options);
    for (int round = 0; round < 2; ++round)
      for (std::size_t k = 0; k < expected_out.size(); ++k)
        CHECK_EQ(describe(s.explain(cache_texts()[k], 5)), expected_out[k]);
    CHECK(s.cache_stats().evictions > 0);
  }
}

TEST(concurrent_explain_and_match_agree_with_one_thread) {
  const std::vector<std::string> texts = cache_texts();
  nlu::Script reference = nlu::Script::compile(cache_script());
  std::vector<std::string> explained, matched;
  for (const std::string& text : texts) {
    explained.push_back(describe(reference.explain(text, 5)));
    std::string labels;
    for (const nlu::Match& m : reference.match(text)) labels += m.label + " ";
    matched.push_back(labels);
  }
  for (int variant = 0; variant < 3; ++variant) {
    nlu::Script s = nlu::Script::compile(cache_script());
    nlu::CacheOptions options;
    options.max_states = variant == 1 ? 40 : 200000;  // 1: a shared cache that keeps evicting
    options.per_thread = variant == 2;
    s.set_cache_options(options);
    const int kThreads = 4;
    std::vector<int> wrong(kThreads, 0);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&, t] {
        for (int round = 0; round < 3; ++round) {
          for (std::size_t k = 0; k < texts.size(); ++k) {
            const std::size_t i = (k * (2 * t + 1) + t + round) % texts.size();
            if (describe(s.explain(texts[i], 5)) != explained[i]) ++wrong[t];
            std::string labels;
            for (const nlu::Match& m : s.match(texts[k])) labels += m.label + " ";
            if (labels != matched[k]) ++wrong[t];
          }
        }
      });
    }
    for (std::thread& t : threads) t.join();
    int total = 0;
    for (int w : wrong) total += w;
    CHECK_EQ(total, 0);
  }
}

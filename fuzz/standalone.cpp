// A small mutation driver for the fuzz targets, for toolchains without
// libFuzzer (such as Apple clang). Build it with the sanitizers:
//
//   fuzz_driver ITERATIONS SEED_FILE...
//
// Each iteration mutates a seed (or an earlier input) with byte flips,
// inserts, deletions and splices, and runs LLVMFuzzerTestOneInput on it. The
// input being run is saved to `crash-input` first, so a crash leaves it behind;
// inputs taking over a second are saved as `slow-input-N`.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: " << argv[0] << " ITERATIONS SEED_FILE...\n";
    return 2;
  }
  const long iterations = std::atol(argv[1]);
  std::vector<std::string> pool;
  for (int i = 2; i < argc; ++i) {
    std::ifstream in(argv[i], std::ios::binary);
    pool.emplace_back(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  const char* seed = std::getenv("FUZZ_SEED");
  std::mt19937 rng(seed ? static_cast<unsigned>(std::atol(seed)) : std::random_device{}());
  auto pick = [&](std::size_t n) { return n ? std::uniform_int_distribution<std::size_t>(0, n - 1)(rng) : 0; };
  // Bytes that matter to the pattern language, plus some Hangul.
  const std::string interesting = "()[]{}<>\\:+*^~=.|&-/`\"?@#, \n\t0123456789";
  const std::vector<std::string> words = {"<?>*?", "<?>+?", "@{", "@var.{", "`@{", "}",
                                          "::", "을/를", "으로/로", "^(1,3)", "~[", "]~",
                                          "\\[", "</", "]&", "만", "시", "내일", "카드", "\xEC"};
  for (long it = 0; it < iterations; ++it) {
    std::string input = pool[pick(pool.size())];
    const int edits = 1 + static_cast<int>(pick(8));
    for (int e = 0; e < edits; ++e) {
      const std::size_t at = pick(input.size() + 1);
      switch (pick(6)) {
        case 0: if (!input.empty()) input[pick(input.size())] ^= static_cast<char>(1 << pick(8)); break;
        case 1: input.insert(at, 1, interesting[pick(interesting.size())]); break;
        case 2: if (!input.empty()) input.erase(pick(input.size()), 1 + pick(8)); break;
        case 3: input.insert(at, words[pick(words.size())]); break;
        case 4: {
          const std::string& other = pool[pick(pool.size())];
          const std::size_t from = pick(other.size() + 1);
          input.insert(at, other.substr(from, pick(64)));
          break;
        }
        default: if (!input.empty()) input.insert(at, input.substr(pick(input.size()), pick(32)));
      }
    }
    {
      std::ofstream out("crash-input", std::ios::binary);
      out << input;
    }
    const auto start = std::chrono::steady_clock::now();
    LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (seconds > 1) {
      static int slow = 0;
      std::ofstream out("slow-input-" + std::to_string(++slow), std::ios::binary);
      out << input;
      std::cerr << "slow input (" << seconds << " s) saved as slow-input-" << slow << "\n";
    }
    if (pool.size() < 5000 && pick(16) == 0) pool.push_back(input);
    if ((it + 1) % 10000 == 0) std::cerr << it + 1 << " inputs\n";
  }
  std::remove("crash-input");
  std::cerr << "done: " << iterations << " inputs\n";
  return 0;
}

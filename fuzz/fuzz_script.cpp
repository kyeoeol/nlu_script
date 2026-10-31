// Fuzz target: compiles the input as a script. When it compiles, matches
// each of its first lines (patterns often match text close to themselves)
// and lints it with a small state limit.
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

#include "nlu_script/nlu_script.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > 16384) return 0;
  const std::string source(reinterpret_cast<const char*>(data), size);
  try {
    nlu::Script script = nlu::Script::compile(source);
    std::istringstream lines(source);
    std::string line;
    for (int i = 0; i < 8 && std::getline(lines, line); ++i) {
      script.match(line);
      script.match_best(line);
    }
    script.lint(2000);
  } catch (const nlu::CompileError&) {
  }
  return 0;
}

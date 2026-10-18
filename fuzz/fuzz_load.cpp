// Fuzz target: loads the input as a saved script (Script::load). Corrupt
// data must be rejected with an exception, never crash; what loads must
// match without crashing. The corpus starts from saved scripts.
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "nlu_script/nlu_script.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::string bytes(reinterpret_cast<const char*>(data), size);
  try {
    nlu::Script script = nlu::Script::load(bytes);
    script.match("I lost my debit card");
    script.match("체크카드를 분실했어요");
    script.search("내일 오후 3시에 전화 주세요");
  } catch (const std::runtime_error&) {
  }
  return 0;
}

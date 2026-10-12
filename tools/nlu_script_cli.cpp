// Reads sentences from stdin (one per line) and prints `label:tagged text` for
// every matching label, or `-` when nothing matches.
#include <iostream>
#include <string>

#include "nlu_script/nlu_script.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " SCRIPT\n";
    return 2;
  }
  try {
    nlu::Script script = nlu::Script::compile_file(argv[1]);
    std::string line;
    while (std::getline(std::cin, line)) {
      std::vector<nlu::Match> matches = script.match(line);
      if (matches.empty()) std::cout << "-\n";
      for (const nlu::Match& m : matches) std::cout << m.label << ":" << m.tagged << "\n";
      std::cout.flush();
    }
  } catch (const std::exception& e) {
    std::cerr << argv[1] << ": " << e.what() << "\n";
    return 1;
  }
  return 0;
}

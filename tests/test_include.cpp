// Behavior of `include` lines: rules and definitions spread over files.
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "harness.hpp"
#include "nlu_script/nlu_script.hpp"

namespace fs = std::filesystem;

namespace {

// A fresh directory for one test's files.
struct TempDir {
  fs::path path;
  TempDir() {
    static std::atomic<int> counter{0};
    path = fs::temp_directory_path() /
           ("nlu_script_include_" + std::to_string(++counter) + "_" +
            std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempDir() { fs::remove_all(path); }
  std::string write(const std::string& name, const std::string& text) const {
    fs::path file = path / name;
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
    return file.string();
  }
};

}  // namespace

TEST(include_brings_in_rules_and_definitions_from_other_files) {
  TempDir dir;
  dir.write("entities.nlu", "@card: [신용|체크]카드, [credit|debit] card\n");
  dir.write("rules/agent.nlu",
            "include ../entities.nlu\n"
            "\n"
            "agent:connect\n"
            "    상담원 연결 (해주세요)\n");
  const std::string main = dir.write(
      "main.nlu",
      "include entities.nlu\n"
      "include rules/agent.nlu\n"
      "card:lost\n"
      "    @{card}(을/를) 분실했어요\n");
  nlu::Script s = nlu::Script::compile_file(main);
  std::vector<nlu::Match> found = s.match("체크카드를 분실했어요");
  CHECK_EQ(found.size(), 1u);
  if (found.size() == 1) {
    CHECK_EQ(found[0].file, main);
    CHECK_EQ(found[0].line, 4);
  }
  found = s.match("상담원 연결해주세요");
  CHECK_EQ(found.size(), 1u);
  if (found.size() == 1) {
    CHECK_EQ(found[0].label, std::string("agent:connect"));
    CHECK(found[0].file.find("agent.nlu") != std::string::npos);
    CHECK_EQ(found[0].line, 4);
  }
  // entities.nlu is included twice but read once, so @card is not redefined.
  CHECK_EQ(s.rule_count(), 2u);
}

TEST(include_cycles_are_harmless) {
  TempDir dir;
  dir.write("a.nlu", "include b.nlu\na\n    one\n");
  dir.write("b.nlu", "include a.nlu\nb\n    two\n");
  nlu::Script s = nlu::Script::compile_file((dir.path / "a.nlu").string());
  CHECK_EQ(s.match("one").size(), 1u);
  CHECK_EQ(s.match("two").size(), 1u);
}

TEST(include_errors_name_the_file) {
  TempDir dir;
  const std::string broken = dir.write("broken.nlu", "x\n    [unclosed\n");
  const std::string main = dir.write("main.nlu",
                                     "include broken.nlu\n"
                                     "include missing.nlu\n"
                                     "y\n"
                                     "    fine\n");
  try {
    nlu::Script::compile_file(main);
    CHECK(false);
  } catch (const nlu::CompileError& e) {
    // Ordered by file (the main script, then included files as they are
    // read), then by position.
    CHECK_EQ(e.errors().size(), 2u);
    if (e.errors().size() == 2) {
      CHECK_EQ(e.errors()[0].file, main);
      CHECK_EQ(e.errors()[0].line, 2);
      CHECK(e.errors()[0].message.find("missing.nlu") != std::string::npos);
      CHECK_EQ(e.errors()[1].file, broken);
      CHECK_EQ(e.errors()[1].line, 2);
    }
    CHECK(std::string(e.what()).find(main + ": line 2,") == 0);
  }
}

TEST(include_in_a_source_string_is_relative_to_the_current_directory) {
  TempDir dir;
  const std::string other = dir.write("other.nlu", "greeting\n    hello\n");
  // An absolute path works from anywhere.
  nlu::Script s = nlu::Script::compile("include " + other + "\nbye\n    goodbye\n");
  CHECK_EQ(s.match("hello").size(), 1u);
  std::vector<nlu::Match> found = s.match("goodbye");
  CHECK_EQ(found.size(), 1u);
  if (found.size() == 1) CHECK_EQ(found[0].file, std::string());
}

TEST(lint_names_files) {
  TempDir dir;
  dir.write("any.nlu", "report:any\n    I <?>+?\n@unused: x\n");
  const std::string main = dir.write("main.nlu", "include any.nlu\ncard:lost\n    I lost my card\n");
  nlu::LintReport report = nlu::Script::compile_file(main).lint();
  CHECK_EQ(report.overlaps.size(), 1u);
  if (report.overlaps.size() == 1) {
    // any.nlu is included before card:lost, so its rule comes first.
    CHECK(report.overlaps[0].file.find("any.nlu") != std::string::npos);
    CHECK_EQ(report.overlaps[0].other_file, main);
  }
  CHECK_EQ(report.unused.size(), 1u);
  if (report.unused.size() == 1) CHECK(report.unused[0].file.find("any.nlu") != std::string::npos);
}

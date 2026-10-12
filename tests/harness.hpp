// Minimal test harness: no external dependencies.
#ifndef NLU_SCRIPT_TESTS_HARNESS_HPP
#define NLU_SCRIPT_TESTS_HARNESS_HPP

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace harness {

struct Case {
  const char* name;
  std::function<void()> body;
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

inline int& failures() {
  static int count = 0;
  return count;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> body) {
    registry().push_back({name, std::move(body)});
  }
};

inline void fail(const char* file, int line, const std::string& message) {
  ++failures();
  std::cerr << file << ":" << line << ": FAILED: " << message << "\n";
}

template <typename T>
std::string show(const T& value) {
  std::ostringstream out;
  out << value;
  return out.str();
}

inline std::string show(const std::vector<std::string>& values) {
  std::string out = "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) out += ", ";
    out += "\"" + values[i] + "\"";
  }
  return out + "]";
}

inline int run_all() {
  for (const Case& c : registry()) {
    int before = failures();
    try {
      c.body();
    } catch (const std::exception& e) {
      fail(c.name, 0, std::string("unexpected exception: ") + e.what());
    }
    std::cout << (failures() == before ? "[ OK ] " : "[FAIL] ") << c.name << "\n";
  }
  std::cout << registry().size() << " tests, " << failures() << " failed checks\n";
  return failures() == 0 ? 0 : 1;
}

}  // namespace harness

#define TEST(name)                                                   \
  static void name();                                                \
  static harness::Registrar registrar_##name(#name, name);           \
  static void name()

#define CHECK(cond)                                                  \
  do {                                                               \
    if (!(cond)) harness::fail(__FILE__, __LINE__, #cond);           \
  } while (0)

#define CHECK_EQ(actual, expected)                                   \
  do {                                                               \
    auto&& a_ = (actual);                                            \
    auto&& e_ = (expected);                                          \
    if (!(a_ == e_))                                                 \
      harness::fail(__FILE__, __LINE__,                              \
                    std::string(#actual) + " == " + harness::show(a_) + \
                        ", expected " + harness::show(e_));          \
  } while (0)

#define CHECK_THROWS(ExceptionType, expr)                            \
  do {                                                               \
    bool thrown_ = false;                                            \
    try {                                                            \
      (void)(expr);                                                  \
    } catch (const ExceptionType&) {                                 \
      thrown_ = true;                                                \
    }                                                                \
    if (!thrown_)                                                    \
      harness::fail(__FILE__, __LINE__,                              \
                    std::string(#expr) + " did not throw " #ExceptionType); \
  } while (0)

#endif  // NLU_SCRIPT_TESTS_HARNESS_HPP

#include "nqtest.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace nqtest {
namespace {

struct Case {
  const char* name;
  TestFn fn;
};

std::vector<Case>& cases() {
  static std::vector<Case>* v = new std::vector<Case>();  // 有意泄漏：避免静态析构顺序问题
  return *v;
}

struct State {
  int checks = 0;
  int failures = 0;
  const char* current_test = "";
  bool update_golden = false;
};

State& state() {
  static State s;
  return s;
}

}  // namespace

void register_test(const char* name, TestFn fn) { cases().push_back({name, fn}); }

void report_failure(const char* file, int line, const std::string& message) {
  State& s = state();
  ++s.failures;
  std::fprintf(stderr, "  [FAIL] %s\n         %s:%d: %s\n", s.current_test, file, line,
               message.c_str());
}

bool update_golden() { return state().update_golden; }

int run_all(int argc, char** argv) {
  std::string filter;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (std::strcmp(arg, "--update-golden") == 0) {
      state().update_golden = true;
    } else if (std::strncmp(arg, "--filter=", 9) == 0) {
      filter = arg + 9;
    } else if (std::strcmp(arg, "--list") == 0) {
      for (const Case& c : cases()) std::printf("%s\n", c.name);
      return 0;
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", arg);
      return 2;
    }
  }

  int ran = 0;
  int failed_tests = 0;
  for (const Case& c : cases()) {
    if (!filter.empty() && std::strstr(c.name, filter.c_str()) == nullptr) continue;
    ++ran;
    state().current_test = c.name;
    const int before = state().failures;
    c.fn();
    if (state().failures != before) {
      ++failed_tests;
      std::printf("[FAIL] %s\n", c.name);
    } else {
      std::printf("[ ok ] %s\n", c.name);
    }
  }

  std::printf("\n%d tests, %d failed, %d assertions failed\n", ran, failed_tests,
              state().failures);
  return (failed_tests == 0) ? 0 : 1;
}

}  // namespace nqtest

int main(int argc, char** argv) { return nqtest::run_all(argc, argv); }

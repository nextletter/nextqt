// NextQt · 自研测试框架（零依赖、无异常、支持 golden 与过滤器）
#pragma once

#include <cmath>
#include <string>
#include <type_traits>

namespace nqtest {

using TestFn = void (*)();

void register_test(const char* name, TestFn fn);
void report_failure(const char* file, int line, const std::string& message);

int run_all(int argc, char** argv);
[[nodiscard]] bool update_golden();

class Registrar {
 public:
  Registrar(const char* name, TestFn fn) { register_test(name, fn); }
};

template <class A, class B>
void expect_eq(const A& lhs, const B& rhs, const char* ea, const char* eb, const char* file,
               int line) {
  if (lhs == rhs) return;
  std::string msg = std::string("expect_eq: ") + ea + " == " + eb;
  if constexpr (std::is_arithmetic_v<A> && std::is_arithmetic_v<B>) {
    msg += "  (lhs=" + std::to_string(static_cast<long double>(lhs)) +
           ", rhs=" + std::to_string(static_cast<long double>(rhs)) + ")";
  }
  report_failure(file, line, msg);
}

template <class A, class B>
void expect_near(A lhs, B rhs, double tolerance, const char* ea, const char* eb, const char* file,
                 int line) {
  const double delta = std::fabs(static_cast<double>(lhs) - static_cast<double>(rhs));
  if (delta <= tolerance) return;
  std::string msg = "expect_near: |" + std::string(ea) + " - " + eb + "| = " +
                    std::to_string(delta) + " > " + std::to_string(tolerance) +
                    "  (lhs=" + std::to_string(static_cast<double>(lhs)) +
                    ", rhs=" + std::to_string(static_cast<double>(rhs)) + ")";
  report_failure(file, line, msg);
}

}  // namespace nqtest

#define NQ_TEST(name)                                                       \
  static void name##_test_impl();                                           \
  static ::nqtest::Registrar name##_test_reg(#name, &name##_test_impl);     \
  static void name##_test_impl()

#define NQ_FAIL(message) ::nqtest::report_failure(__FILE__, __LINE__, (message))

#define NQ_EXPECT(cond)                                                      \
  do {                                                                       \
    if (!(cond)) ::nqtest::report_failure(__FILE__, __LINE__, "expect: " #cond); \
  } while (0)

#define NQ_EXPECT_EQ(a, b) \
  ::nqtest::expect_eq((a), (b), #a, #b, __FILE__, __LINE__)

#define NQ_EXPECT_NEAR(a, b, tol) \
  ::nqtest::expect_near((a), (b), (tol), #a, #b, __FILE__, __LINE__)

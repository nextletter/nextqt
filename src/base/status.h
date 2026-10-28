// NextQt · 基础类型：错误码、Result、断言
// 约束：-fno-exceptions -fno-rtti，错误通过返回值传递
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace nq {

enum class Code : int32_t {
  kOk = 0,
  kInvalidArgument,
  kOutOfMemory,
  kCorrupt,
  kUnsupported,
  kNotFound,
  kLimitExceeded,
  kInternal,
};

constexpr std::string_view to_string(Code c) noexcept {
  switch (c) {
    case Code::kOk: return "ok";
    case Code::kInvalidArgument: return "invalid_argument";
    case Code::kOutOfMemory: return "out_of_memory";
    case Code::kCorrupt: return "corrupt";
    case Code::kUnsupported: return "unsupported";
    case Code::kNotFound: return "not_found";
    case Code::kLimitExceeded: return "limit_exceeded";
    case Code::kInternal: return "internal";
  }
  return "unknown";
}

struct Status {
  Code code = Code::kOk;
  const char* message = "";

  constexpr Status() noexcept = default;
  constexpr Status(Code c, const char* m = "") noexcept : code(c), message(m) {}

  [[nodiscard]] constexpr bool ok() const noexcept { return code == Code::kOk; }
  constexpr explicit operator bool() const noexcept { return ok(); }

  friend constexpr bool operator==(const Status& a, const Status& b) noexcept {
    return a.code == b.code;
  }
};

template <class T>
class [[nodiscard]] Result {
 public:
  Result(T v) noexcept : value_(std::move(v)) {}   // NOLINT(google-explicit-constructor)
  Result(Status s) noexcept : status_(s) {}        // NOLINT(google-explicit-constructor)

  [[nodiscard]] bool ok() const noexcept { return status_.ok(); }
  explicit operator bool() const noexcept { return ok(); }
  [[nodiscard]] const Status& status() const noexcept { return status_; }

  T& operator*() noexcept { return *value_; }
  const T& operator*() const noexcept { return *value_; }
  T* operator->() noexcept { return &*value_; }
  const T* operator->() const noexcept { return &*value_; }

  [[nodiscard]] T value_or(T fallback) const noexcept {
    return value_.has_value() ? *value_ : std::move(fallback);
  }

 private:
  std::optional<T> value_;
  Status status_;
};

[[noreturn]] void assert_fail(const char* cond, const char* file, int line) noexcept;

}  // namespace nq

#define NQ_CONCAT_INNER(a, b) a##b
#define NQ_CONCAT(a, b) NQ_CONCAT_INNER(a, b)

// 断言：不变量检查，release 下仍保留（成本极低），可被 NQ_NO_ASSERT 关闭
#ifndef NQ_NO_ASSERT
#define NQ_ASSERT(cond)                                                     \
  do {                                                                      \
    if (!(cond)) ::nq::assert_fail(#cond, __FILE__, __LINE__);               \
  } while (0)
#else
#define NQ_ASSERT(cond) ((void)0)
#endif

// NQ_TRY(var, expr)：expr 必须返回 Result<T>；失败则提前返回 Status
#define NQ_TRY(var, expr)                                                   \
  auto NQ_CONCAT(_nq_try_, __LINE__) = (expr);                              \
  if (!NQ_CONCAT(_nq_try_, __LINE__))                                       \
    return NQ_CONCAT(_nq_try_, __LINE__).status();                          \
  auto var = *std::move(NQ_CONCAT(_nq_try_, __LINE__))

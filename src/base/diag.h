// NextQt · 计数器与诊断（每个子系统必须暴露可断言的行为计数器）
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nq::diag {

class Counter {
 public:
  void add(uint64_t n = 1) noexcept { value_.fetch_add(n, std::memory_order_relaxed); }
  void set(uint64_t v) noexcept { value_.store(v, std::memory_order_relaxed); }
  void reset() noexcept { set(0); }
  [[nodiscard]] uint64_t value() const noexcept {
    return value_.load(std::memory_order_relaxed);
  }

 private:
  std::atomic<uint64_t> value_{0};
};

struct Snapshot {
  std::string name;
  std::string unit;
  uint64_t value = 0;
};

// 注册（幂等）并返回计数器；name 需为编译期常量字面量风格（"raster.pixels_shaded"）
Counter& counter(std::string_view name, std::string_view unit = "count");

// 按名称排序的快照（确定性输出）
[[nodiscard]] std::vector<Snapshot> snapshot();
[[nodiscard]] std::string dump_json();
void reset_all();

}  // namespace nq::diag

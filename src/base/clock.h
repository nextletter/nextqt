// NextQt · 时钟注入点（所有时间必须来自这里，禁止直调 OS）
#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

namespace nq {

class Clock {
 public:
  virtual ~Clock() = default;
  [[nodiscard]] virtual uint64_t now_micros() const noexcept = 0;
  virtual void sleep_micros(uint64_t us) const noexcept = 0;
};

class SteadyClock final : public Clock {
 public:
  [[nodiscard]] uint64_t now_micros() const noexcept override {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
  }
  void sleep_micros(uint64_t us) const noexcept override {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
  }
};

// 测试/回放用：时间只在显式推进时变化
class DeterministicClock final : public Clock {
 public:
  [[nodiscard]] uint64_t now_micros() const noexcept override { return now_; }
  void sleep_micros(uint64_t us) const noexcept override { now_ += us; }
  void advance_micros(uint64_t us) noexcept { now_ += us; }
  void set_micros(uint64_t us) noexcept { now_ = us; }

 private:
  mutable uint64_t now_ = 0;
};

}  // namespace nq

// NextQt · 定点布局单位：1 单位 = 1/64 px，饱和运算，跨平台确定性
#pragma once

#include <cstdint>
#include <limits>

namespace nq {

class LayoutUnit {
 public:
  static constexpr int kFracBits = 6;
  static constexpr int32_t kOne = int32_t{1} << kFracBits;
  static constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
  static constexpr int32_t kMin = std::numeric_limits<int32_t>::min();

  constexpr LayoutUnit() noexcept = default;

  static constexpr LayoutUnit from_raw(int32_t raw) noexcept {
    LayoutUnit u;
    u.v_ = raw;
    return u;
  }

  static constexpr LayoutUnit from_int(int32_t value) noexcept {
    return from_raw(sat_mul(value, kOne));
  }

  static constexpr LayoutUnit from_float(float px) noexcept {
    // 四舍五入（远离零），与布局度量入口一致
    const float scaled = px * static_cast<float>(kOne);
    const float rounded = scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f;
    if (rounded >= static_cast<float>(kMax)) return from_raw(kMax);
    if (rounded <= static_cast<float>(kMin)) return from_raw(kMin);
    return from_raw(static_cast<int32_t>(rounded));
  }

  [[nodiscard]] constexpr int32_t raw() const noexcept { return v_; }
  [[nodiscard]] constexpr float to_float() const noexcept {
    return static_cast<float>(v_) / static_cast<float>(kOne);
  }
  // floor 到整数像素（用于吸附）
  [[nodiscard]] constexpr int32_t floor_to_int() const noexcept {
    return v_ >= 0 ? (v_ >> kFracBits) : -(((-v_) + kOne - 1) >> kFracBits);
  }
  // 四舍五入到整数像素
  [[nodiscard]] constexpr int32_t round_to_int() const noexcept {
    return v_ >= 0 ? ((v_ + kOne / 2) >> kFracBits)
                   : -(((-v_) + kOne / 2) >> kFracBits);
  }

  [[nodiscard]] constexpr LayoutUnit operator+(LayoutUnit rhs) const noexcept {
    return from_raw(sat_add(v_, rhs.v_));
  }
  [[nodiscard]] constexpr LayoutUnit operator-(LayoutUnit rhs) const noexcept {
    return from_raw(sat_sub(v_, rhs.v_));
  }
  [[nodiscard]] constexpr LayoutUnit operator-() const noexcept {
    return from_raw(v_ == kMin ? kMax : -v_);
  }
  [[nodiscard]] constexpr LayoutUnit operator*(int32_t rhs) const noexcept {
    return from_raw(sat_mul(v_, rhs));
  }
  [[nodiscard]] constexpr LayoutUnit operator/(int32_t rhs) const noexcept {
    if (rhs == 0) return *this;  // 布局中除零为程序设计错误，返回自身而非 UB
    const int64_t num = static_cast<int64_t>(v_);
    const int64_t den = static_cast<int64_t>(rhs);
    const bool negative = (num < 0) != (den < 0);
    const int64_t abs_num = num < 0 ? -num : num;
    const int64_t abs_den = den < 0 ? -den : den;
    const int64_t q = (abs_num + abs_den / 2) / abs_den;  // 远离零舍入
    return from_raw(static_cast<int32_t>(negative ? -q : q));
  }

  constexpr LayoutUnit& operator+=(LayoutUnit rhs) noexcept {
    v_ = sat_add(v_, rhs.v_);
    return *this;
  }
  constexpr LayoutUnit& operator-=(LayoutUnit rhs) noexcept {
    v_ = sat_sub(v_, rhs.v_);
    return *this;
  }

  friend constexpr bool operator==(LayoutUnit a, LayoutUnit b) noexcept { return a.v_ == b.v_; }
  friend constexpr bool operator!=(LayoutUnit a, LayoutUnit b) noexcept { return a.v_ != b.v_; }
  friend constexpr bool operator<(LayoutUnit a, LayoutUnit b) noexcept { return a.v_ < b.v_; }
  friend constexpr bool operator<=(LayoutUnit a, LayoutUnit b) noexcept { return a.v_ <= b.v_; }
  friend constexpr bool operator>(LayoutUnit a, LayoutUnit b) noexcept { return a.v_ > b.v_; }
  friend constexpr bool operator>=(LayoutUnit a, LayoutUnit b) noexcept { return a.v_ >= b.v_; }

 private:
  static constexpr int32_t sat_add(int32_t a, int32_t b) noexcept {
    const int64_t s = static_cast<int64_t>(a) + b;
    return s > kMax ? kMax : (s < kMin ? kMin : static_cast<int32_t>(s));
  }
  static constexpr int32_t sat_sub(int32_t a, int32_t b) noexcept {
    const int64_t s = static_cast<int64_t>(a) - b;
    return s > kMax ? kMax : (s < kMin ? kMin : static_cast<int32_t>(s));
  }
  static constexpr int32_t sat_mul(int32_t a, int32_t b) noexcept {
    const int64_t p = static_cast<int64_t>(a) * b;
    return p > kMax ? kMax : (p < kMin ? kMin : static_cast<int32_t>(p));
  }

  int32_t v_ = 0;
};

constexpr LayoutUnit operator""_lu(unsigned long long v) noexcept {
  return LayoutUnit::from_raw(static_cast<int32_t>(v * LayoutUnit::kOne));
}

}  // namespace nq

// NextQt · 几何与颜色（布局用定点，光栅用 f32）
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nq {

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;

  friend constexpr Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
  friend constexpr Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
  friend constexpr Vec2 operator*(Vec2 a, float s) noexcept { return {a.x * s, a.y * s}; }
};

// 轴对齐矩形，单位为逻辑像素（f32 仅用于光栅阶段）
struct RectF {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;

  [[nodiscard]] constexpr float right() const noexcept { return x + w; }
  [[nodiscard]] constexpr float bottom() const noexcept { return y + h; }
  [[nodiscard]] constexpr bool is_empty() const noexcept { return !(w > 0.0f) || !(h > 0.0f); }

  friend constexpr RectF intersect(RectF a, RectF b) noexcept {
    const float x0 = a.x > b.x ? a.x : b.x;
    const float y0 = a.y > b.y ? a.y : b.y;
    const float x1 = a.right() < b.right() ? a.right() : b.right();
    const float y1 = a.bottom() < b.bottom() ? a.bottom() : b.bottom();
    if (!(x1 > x0) || !(y1 > y0)) return {};
    return {x0, y0, x1 - x0, y1 - y0};
  }

  friend constexpr RectF unite(RectF a, RectF b) noexcept {
    if (a.is_empty()) return b;
    if (b.is_empty()) return a;
    const float x0 = a.x < b.x ? a.x : b.x;
    const float y0 = a.y < b.y ? a.y : b.y;
    const float x1 = a.right() > b.right() ? a.right() : b.right();
    const float y1 = a.bottom() > b.bottom() ? a.bottom() : b.bottom();
    return {x0, y0, x1 - x0, y1 - y0};
  }

  [[nodiscard]] constexpr bool contains(float px, float py) const noexcept {
    return px >= x && px < right() && py >= y && py < bottom();
  }
};

// 预乘 alpha 的 8 位颜色（引擎内部统一格式）
struct Rgba8 {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 0;

  friend constexpr bool operator==(const Rgba8& l, const Rgba8& r) noexcept {
    return l.r == r.r && l.g == r.g && l.b == r.b && l.a == r.a;
  }
};

// 从未预乘的 sRGB(A) 构造预乘颜色
constexpr uint8_t mul_u8(uint8_t c, uint8_t a) noexcept {
  return static_cast<uint8_t>((static_cast<uint32_t>(c) * a + 127u) / 255u);
}

constexpr Rgba8 premul(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept {
  return {mul_u8(r, a), mul_u8(g, a), mul_u8(b, a), a};
}

constexpr Rgba8 kTransparent{0, 0, 0, 0};
constexpr Rgba8 kBlack = premul(0, 0, 0);
constexpr Rgba8 kWhite = premul(255, 255, 255);

inline float clampf(float v, float lo, float hi) noexcept {
  return v < lo ? lo : (v > hi ? hi : v);
}

inline int clampi(int v, int lo, int hi) noexcept {
  return v < lo ? lo : (v > hi ? hi : v);
}

constexpr double kPi = 3.14159265358979323846;

}  // namespace nq

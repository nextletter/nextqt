// NextQt · 光栅目标（预乘 RGBA8）与基础操作
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

#include "base/geom.h"
#include "base/hash.h"

namespace nq {

struct Surface {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;  // 预乘 RGBA8，行主序

  Surface() = default;
  Surface(int w, int h) : width(w > 0 ? w : 0), height(h > 0 ? h : 0) {
    pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
  }

  [[nodiscard]] bool valid() const noexcept {
    return width > 0 && height > 0 &&
           pixels.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  }

  [[nodiscard]] RectF bounds() const noexcept {
    return {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
  }

  void fill(Rgba8 c) noexcept;

  [[nodiscard]] uint8_t* row(int y) noexcept {
    return pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
  }
  [[nodiscard]] const uint8_t* row(int y) const noexcept {
    return pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
  }

  [[nodiscard]] Rgba8 at(int x, int y) const noexcept {
    if (x < 0 || y < 0 || x >= width || y >= height) return {};
    const uint8_t* p = row(y) + static_cast<size_t>(x) * 4;
    return {p[0], p[1], p[2], p[3]};
  }

  // 预乘 src-over：out = src*cov + dst*(1 - src.a*cov)，coverage ∈ [0,255]
  void blend_pixel(int x, int y, Rgba8 src, uint32_t coverage) noexcept {
    if (x < 0 || y < 0 || x >= width || y >= height || coverage == 0) return;
    uint8_t* p = row(y) + static_cast<size_t>(x) * 4;
    const uint32_t eff_alpha = static_cast<uint32_t>(src.a) * coverage / 255u;
    const uint32_t w_src = coverage;
    const uint32_t w_dst = 255u - eff_alpha;
    const auto mix = [w_src, w_dst](uint32_t s, uint32_t d) {
      const uint32_t v = (s * w_src + d * w_dst + 127u) / 255u;
      return static_cast<uint8_t>(v > 255u ? 255u : v);
    };
    p[0] = mix(src.r, p[0]);
    p[1] = mix(src.g, p[1]);
    p[2] = mix(src.b, p[2]);
    p[3] = mix(src.a, p[3]);
  }

  [[nodiscard]] uint64_t content_hash() const noexcept {
    return hash_bytes(pixels.data(), pixels.size());
  }
};

// 写 PPM(P6) 便于人工检查与 golden 对比（预乘 → 直通颜色）
[[nodiscard]] bool write_ppm(const Surface& surface, const char* path);

// 逐通道平均绝对差（0..255），用于图像对比
[[nodiscard]] double mean_abs_diff(const Surface& a, const Surface& b);

// 由文本构造 Surface 的最小占位图（M0 无字体栈：用块状字形画 "NQ" 风格图案）
void draw_placeholder_glyphs(Surface& surface, int x, int y, int scale, Rgba8 color);

}  // namespace nq

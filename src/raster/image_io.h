// NextQt · 图像写出（PNG 自研：stored deflate，零依赖；PPM 用于快速检查）
#pragma once

#include "raster/surface.h"

namespace nq {

[[nodiscard]] bool write_png(const Surface& surface, const char* path);
[[nodiscard]] bool write_ppm(const Surface& surface, const char* path);

// 预乘 → 与白色背景合成后的直通 sRGB 字节
inline void unpremul_on_white(uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t out[3]) {
  const uint32_t inv = 255u - a;
  out[0] = static_cast<uint8_t>(r + inv);
  out[1] = static_cast<uint8_t>(g + inv);
  out[2] = static_cast<uint8_t>(b + inv);
}

}  // namespace nq

#include "raster/surface.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace nq {

void Surface::fill(Rgba8 c) noexcept {
  if (!valid()) return;
  for (size_t i = 0; i < pixels.size(); i += 4) {
    pixels[i + 0] = c.r;
    pixels[i + 1] = c.g;
    pixels[i + 2] = c.b;
    pixels[i + 3] = c.a;
  }
}

bool write_ppm(const Surface& surface, const char* path) {
  if (!surface.valid()) return false;
  std::FILE* f = std::fopen(path, "wb");
  if (f == nullptr) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", surface.width, surface.height);
  std::vector<uint8_t> row(static_cast<size_t>(surface.width) * 3);
  for (int y = 0; y < surface.height; ++y) {
    const uint8_t* src = surface.row(y);
    for (int x = 0; x < surface.width; ++x) {
      const uint8_t* p = src + static_cast<size_t>(x) * 4;
      const uint32_t a = p[3];
      // 预乘 → 直通（与白底合成，便于目视）
      row[static_cast<size_t>(x) * 3 + 0] = static_cast<uint8_t>(p[0] + (255u - a) * 255u / 255u);
      row[static_cast<size_t>(x) * 3 + 1] = static_cast<uint8_t>(p[1] + (255u - a) * 255u / 255u);
      row[static_cast<size_t>(x) * 3 + 2] = static_cast<uint8_t>(p[2] + (255u - a) * 255u / 255u);
    }
    std::fwrite(row.data(), 1, row.size(), f);
  }
  std::fclose(f);
  return true;
}

double mean_abs_diff(const Surface& a, const Surface& b) {
  if (a.width != b.width || a.height != b.height || a.pixels.size() != b.pixels.size()) {
    return 255.0;
  }
  if (a.pixels.empty()) return 0.0;
  uint64_t sum = 0;
  for (size_t i = 0; i < a.pixels.size(); ++i) {
    const int lhs = a.pixels[i];
    const int rhs = b.pixels[i];
    sum += static_cast<uint64_t>(lhs > rhs ? lhs - rhs : rhs - lhs);
  }
  return static_cast<double>(sum) / static_cast<double>(a.pixels.size());
}

namespace {

// 5x7 点阵（M0 占位字形，后续由自研字体栈替代）
constexpr uint8_t kGlyphN[7] = {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001};
constexpr uint8_t kGlyphQ[7] = {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b01110, 0b00001};

void draw_glyph(Surface& s, const uint8_t rows[7], int x, int y, int scale, Rgba8 color) {
  for (int gy = 0; gy < 7; ++gy) {
    for (int gx = 0; gx < 5; ++gx) {
      if ((rows[gy] >> (4 - gx)) & 1u) {
        for (int sy = 0; sy < scale; ++sy) {
          for (int sx = 0; sx < scale; ++sx) {
            s.blend_pixel(x + gx * scale + sx, y + gy * scale + sy, color, 255);
          }
        }
      }
    }
  }
}

}  // namespace

void draw_placeholder_glyphs(Surface& surface, int x, int y, int scale, Rgba8 color) {
  if (scale < 1) scale = 1;
  draw_glyph(surface, kGlyphN, x, y, scale, color);
  draw_glyph(surface, kGlyphQ, x + 6 * scale, y, scale, color);
}

}  // namespace nq

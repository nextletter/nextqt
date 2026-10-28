#include "raster/surface.h"

#include <algorithm>
#include <cmath>

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

}  // namespace nq

#include "raster/raster.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "base/diag.h"

namespace nq::raster {
namespace {

using Counter = diag::Counter;

Counter& c_pixels() { return diag::counter("raster.pixels_shaded", "px"); }
Counter& c_ops() { return diag::counter("raster.ops", "op"); }
Counter& c_paths() { return diag::counter("raster.paths", "op"); }
Counter& c_clip_rejects() { return diag::counter("raster.clip_rejects", "op"); }
Counter& c_shadows() { return diag::counter("raster.shadows", "op"); }

struct Crossing {
  float x;
  int dir;
};

struct ClipStack {
  explicit ClipStack(RectF initial) : current(initial) {}

  void push(RectF r) {
    saved.push_back(current);
    current = intersect(current, r);
  }
  void pop() {
    if (saved.empty()) return;
    current = saved.back();
    saved.pop_back();
  }
  [[nodiscard]] RectF top() const { return current; }

 private:
  RectF current;
  std::vector<RectF> saved;
};

[[nodiscard]] RectF clip_to_surface(RectF clip, const Surface& target) {
  return intersect(clip, target.bounds());
}

uint8_t lerp_u8(uint8_t a, uint8_t b, float t) noexcept {
  const float v = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t;
  const int32_t i = static_cast<int32_t>(v + 0.5f);
  return static_cast<uint8_t>(i < 0 ? 0 : (i > 255 ? 255 : i));
}

// 三次盒式模糊 ≈ 高斯（比精确高斯快一个量级，视觉差异不可见）
void box_blur_mask(std::vector<uint8_t>& data, int width, int height, int radius) {
  if (radius < 1 || width <= 0 || height <= 0) return;
  // 水平/垂直两个方向复用同一条临时缓冲，长度取两者较大值
  std::vector<uint32_t> row(static_cast<size_t>(std::max(width, height)), 0);
  const uint32_t window = static_cast<uint32_t>(radius * 2 + 1);

  // 水平
  for (int y = 0; y < height; ++y) {
    uint8_t* line = data.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
    for (int x = 0; x < width; ++x) row[static_cast<size_t>(x)] = line[x];
    uint32_t sum = 0;
    for (int i = -radius; i <= radius; ++i) sum += row[static_cast<size_t>(clampi(i, 0, width - 1))];
    for (int x = 0; x < width; ++x) {
      line[x] = static_cast<uint8_t>(sum / window);
      sum -= row[static_cast<size_t>(clampi(x - radius, 0, width - 1))];
      sum += row[static_cast<size_t>(clampi(x + radius + 1, 0, width - 1))];
    }
  }
  // 垂直
  for (int x = 0; x < width; ++x) {
    for (int y = 0; y < height; ++y) {
      row[static_cast<size_t>(y)] = data[static_cast<size_t>(y) * static_cast<size_t>(width) +
                                        static_cast<size_t>(x)];
    }
    uint32_t sum = 0;
    for (int i = -radius; i <= radius; ++i) sum += row[static_cast<size_t>(clampi(i, 0, height - 1))];
    for (int y = 0; y < height; ++y) {
      data[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] =
          static_cast<uint8_t>(sum / window);
      sum -= row[static_cast<size_t>(clampi(y - radius, 0, height - 1))];
      sum += row[static_cast<size_t>(clampi(y + radius + 1, 0, height - 1))];
    }
  }
}

}  // namespace

void compute_coverage(const std::vector<Vec2>& points,
                      const std::vector<uint32_t>& contour_ends, int origin_x, int origin_y,
                      int width, int height, uint8_t* mask) {
  if (points.empty() || contour_ends.empty() || width <= 0 || height <= 0) return;
  std::memset(mask, 0, static_cast<size_t>(width) * static_cast<size_t>(height));

  constexpr int kSS = kSupersample;
  constexpr int kSamples = kSS * kSS;
  const float inv = 1.0f / static_cast<float>(kSS);

  std::vector<Crossing> crossings;
  crossings.reserve(points.size());
  std::vector<int> coverage(static_cast<size_t>(width), 0);

  for (int row = 0; row < height; ++row) {
    const int py = origin_y + row;
    std::fill(coverage.begin(), coverage.end(), 0);
    for (int j = 0; j < kSS; ++j) {
      const float sy = static_cast<float>(py) + (static_cast<float>(j) + 0.5f) * inv;
      crossings.clear();
      uint32_t begin = 0;
      for (uint32_t end : contour_ends) {
        if (end <= begin || end > points.size()) {
          begin = end;
          continue;
        }
        const bool closed = points[end - 1].x == points[begin].x &&
                            points[end - 1].y == points[begin].y;
        const uint32_t edge_count = closed ? (end - begin) : (end - begin + 1);
        for (uint32_t e = 0; e < edge_count; ++e) {
          const Vec2 a = points[begin + e];
          const Vec2 b = points[begin + (e + 1 == edge_count ? 0 : e + 1)];
          const bool a_below = a.y <= sy;
          const bool b_below = b.y <= sy;
          if (a_below == b_below) continue;
          const float t = (sy - a.y) / (b.y - a.y);
          crossings.push_back(Crossing{a.x + t * (b.x - a.x), b.y > a.y ? 1 : -1});
        }
        begin = end;
      }
      if (crossings.empty()) continue;
      std::sort(crossings.begin(), crossings.end(),
                [](const Crossing& l, const Crossing& r) { return l.x < r.x; });

      size_t k = 0;
      int winding = 0;
      for (int col = 0; col < width; ++col) {
        const int px = origin_x + col;
        int covered = 0;
        for (int i = 0; i < kSS; ++i) {
          const float sx = static_cast<float>(px) + (static_cast<float>(i) + 0.5f) * inv;
          while (k < crossings.size() && crossings[k].x <= sx) {
            winding += crossings[k].dir;
            ++k;
          }
          if (winding != 0) ++covered;
        }
        coverage[static_cast<size_t>(col)] += covered;
      }
    }
    uint8_t* line = mask + static_cast<size_t>(row) * static_cast<size_t>(width);
    for (int col = 0; col < width; ++col) {
      const int hits = coverage[static_cast<size_t>(col)];
      line[col] = static_cast<uint8_t>(hits * 255 / kSamples);
    }
  }
}

void fill_rect_analytic(Surface& target, RectF rect, Rgba8 color, RectF clip) {
  const RectF area = intersect(intersect(rect, clip), target.bounds());
  if (area.is_empty() || color.a == 0) {
    c_clip_rejects().add();
    return;
  }
  const int px0 = static_cast<int>(std::floor(area.x));
  const int px1 = static_cast<int>(std::ceil(area.right()));
  const int py0 = static_cast<int>(std::floor(area.y));
  const int py1 = static_cast<int>(std::ceil(area.bottom()));
  for (int py = py0; py < py1; ++py) {
    const float y0 = std::max(area.y, static_cast<float>(py));
    const float y1 = std::min(area.bottom(), static_cast<float>(py + 1));
    const float cov_y = y1 - y0;
    if (!(cov_y > 0.0f)) continue;
    for (int px = px0; px < px1; ++px) {
      const float x0 = std::max(area.x, static_cast<float>(px));
      const float x1 = std::min(area.right(), static_cast<float>(px + 1));
      const float cov_x = x1 - x0;
      if (!(cov_x > 0.0f)) continue;
      const uint32_t a = static_cast<uint32_t>(cov_x * cov_y * 255.0f + 0.5f);
      if (a == 0) continue;
      target.blend_pixel(px, py, color, std::min<uint32_t>(a, 255u));
      c_pixels().add();
    }
  }
}

void fill_rect_gradient(Surface& target, RectF rect, Rgba8 from, Rgba8 to, bool horizontal,
                        RectF clip) {
  const RectF area = intersect(intersect(rect, clip), target.bounds());
  if (area.is_empty()) {
    c_clip_rejects().add();
    return;
  }
  const float span = horizontal ? rect.w : rect.h;
  const float inv = span > 0.0f ? 1.0f / span : 0.0f;
  const int px0 = static_cast<int>(std::floor(area.x));
  const int px1 = static_cast<int>(std::ceil(area.right()));
  const int py0 = static_cast<int>(std::floor(area.y));
  const int py1 = static_cast<int>(std::ceil(area.bottom()));
  for (int py = py0; py < py1; ++py) {
    const float y0 = std::max(area.y, static_cast<float>(py));
    const float y1 = std::min(area.bottom(), static_cast<float>(py + 1));
    const float cov_y = y1 - y0;
    if (!(cov_y > 0.0f)) continue;
    for (int px = px0; px < px1; ++px) {
      const float x0 = std::max(area.x, static_cast<float>(px));
      const float x1 = std::min(area.right(), static_cast<float>(px + 1));
      const float cov_x = x1 - x0;
      if (!(cov_x > 0.0f)) continue;
      const float pos = horizontal ? (static_cast<float>(px) + 0.5f - rect.x)
                                   : (static_cast<float>(py) + 0.5f - rect.y);
      const float t = clampf(pos * inv, 0.0f, 1.0f);
      const Rgba8 c{lerp_u8(from.r, to.r, t), lerp_u8(from.g, to.g, t),
                    lerp_u8(from.b, to.b, t), lerp_u8(from.a, to.a, t)};
      const uint32_t a = static_cast<uint32_t>(cov_x * cov_y * 255.0f + 0.5f);
      if (a == 0) continue;
      target.blend_pixel(px, py, c, std::min<uint32_t>(a, 255u));
      c_pixels().add();
    }
  }
}

void fill_polylines(Surface& target, const std::vector<Vec2>& points,
                    const std::vector<uint32_t>& contour_ends, Rgba8 color, RectF clip) {
  if (points.empty() || contour_ends.empty() || color.a == 0) return;

  float min_x = points[0].x;
  float max_x = min_x;
  float min_y = points[0].y;
  float max_y = min_y;
  for (const Vec2& p : points) {
    min_x = std::min(min_x, p.x);
    max_x = std::max(max_x, p.x);
    min_y = std::min(min_y, p.y);
    max_y = std::max(max_y, p.y);
  }
  const RectF area = clip_to_surface(
      intersect(clip, RectF{min_x, min_y, max_x - min_x, max_y - min_y}), target);
  if (area.is_empty()) {
    c_clip_rejects().add();
    return;
  }
  const int px0 = static_cast<int>(std::floor(area.x));
  const int py0 = static_cast<int>(std::floor(area.y));
  const int w = static_cast<int>(std::ceil(area.right())) - px0;
  const int h = static_cast<int>(std::ceil(area.bottom())) - py0;
  if (w <= 0 || h <= 0) return;

  std::vector<uint8_t> mask(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
  compute_coverage(points, contour_ends, px0, py0, w, h, mask.data());

  for (int row = 0; row < h; ++row) {
    const uint8_t* line = mask.data() + static_cast<size_t>(row) * static_cast<size_t>(w);
    for (int col = 0; col < w; ++col) {
      const uint32_t cov = line[col];
      if (cov == 0) continue;
      target.blend_pixel(px0 + col, py0 + row, color, cov);
      c_pixels().add();
    }
  }
  c_paths().add();
}

void fill_shadow(Surface& target, RectF rect, float radius, float blur, Rgba8 color,
                 RectF clip) {
  if (rect.is_empty() || color.a == 0 || !(blur > 0.0f)) return;
  const int pad = static_cast<int>(std::ceil(blur * 2.0f)) + 1;
  const RectF padded{rect.x - static_cast<float>(pad), rect.y - static_cast<float>(pad),
                     rect.w + static_cast<float>(pad * 2), rect.h + static_cast<float>(pad * 2)};
  const RectF area = clip_to_surface(intersect(clip, padded), target);
  if (area.is_empty()) {
    c_clip_rejects().add();
    return;
  }
  const int px0 = static_cast<int>(std::floor(area.x));
  const int py0 = static_cast<int>(std::floor(area.y));
  const int w = static_cast<int>(std::ceil(area.right())) - px0;
  const int h = static_cast<int>(std::ceil(area.bottom())) - py0;
  if (w <= 0 || h <= 0) return;

  dl::PathBuilder path;
  path.add_rrect(rect, radius);
  std::vector<uint8_t> mask(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
  compute_coverage(path.points(), path.contour_ends(), px0, py0, w, h, mask.data());

  const int radius_px = std::max(1, static_cast<int>(blur * 0.5f + 0.5f));
  box_blur_mask(mask, w, h, radius_px);
  box_blur_mask(mask, w, h, radius_px);
  box_blur_mask(mask, w, h, radius_px);

  for (int row = 0; row < h; ++row) {
    const uint8_t* line = mask.data() + static_cast<size_t>(row) * static_cast<size_t>(w);
    for (int col = 0; col < w; ++col) {
      const uint32_t cov = line[col];  // 只给覆盖率；alpha 由 blend_pixel 统一处理
      if (cov == 0) continue;
      target.blend_pixel(px0 + col, py0 + row, color, cov);
      c_pixels().add();
    }
  }
  c_shadows().add();
}

void replay(const dl::DisplayList& list, Surface& target) {
  if (!target.valid() || list.ops.empty()) return;
  ClipStack clip(target.bounds());
  size_t offset = 0;
  while (offset + sizeof(dl::Header) <= list.ops.size()) {
    dl::Header header{};
    std::memcpy(&header, list.ops.data() + offset, sizeof(header));
    if (header.size < sizeof(dl::Header) || offset + header.size > list.ops.size()) break;
    const std::byte* payload = list.ops.data() + offset + sizeof(dl::Header);
    switch (static_cast<dl::Op>(header.op)) {
      case dl::Op::kClipRect: {
        dl::ClipRectOp op{};
        std::memcpy(&op, payload, sizeof(op));
        clip.push(RectF{op.x, op.y, op.w, op.h});
        break;
      }
      case dl::Op::kPopClip:
        clip.pop();
        break;
      case dl::Op::kFillRect: {
        dl::FillRectOp op{};
        std::memcpy(&op, payload, sizeof(op));
        fill_rect_analytic(target, RectF{op.x, op.y, op.w, op.h}, op.color, clip.top());
        break;
      }
      case dl::Op::kFillRectGradient: {
        dl::FillRectGradientOp op{};
        std::memcpy(&op, payload, sizeof(op));
        fill_rect_gradient(target, RectF{op.x, op.y, op.w, op.h}, op.from, op.to,
                           op.horizontal != 0, clip.top());
        break;
      }
      case dl::Op::kFillRRect: {
        dl::FillRRectOp op{};
        std::memcpy(&op, payload, sizeof(op));
        dl::PathBuilder path;
        path.add_rrect(RectF{op.x, op.y, op.w, op.h}, op.radius);
        fill_polylines(target, path.points(), path.contour_ends(), op.color, clip.top());
        break;
      }
      case dl::Op::kFillCircle: {
        dl::FillCircleOp op{};
        std::memcpy(&op, payload, sizeof(op));
        dl::PathBuilder path;
        path.add_circle(Vec2{op.cx, op.cy}, op.r);
        fill_polylines(target, path.points(), path.contour_ends(), op.color, clip.top());
        break;
      }
      case dl::Op::kFillPath: {
        dl::FillPathOp op{};
        std::memcpy(&op, payload, sizeof(op));
        const uint32_t end = op.first_point + op.point_count;
        if (end > list.points.size()) break;
        std::vector<Vec2> pts(list.points.begin() + op.first_point, list.points.begin() + end);
        const std::vector<uint32_t> ends{static_cast<uint32_t>(pts.size())};
        fill_polylines(target, pts, ends, op.color, clip.top());
        break;
      }
      default:
        break;
    }
    c_ops().add();
    offset += header.size;
  }
}

}  // namespace nq::raster

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

// 像素坐标 → 目标空间的裁剪
[[nodiscard]] RectF clip_to_surface(RectF clip, const Surface& target) {
  return intersect(clip, target.bounds());
}

}  // namespace

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
      const float coverage = cov_x * cov_y;  // 精确面积（像素单元内）
      const uint32_t a = static_cast<uint32_t>(coverage * 255.0f + 0.5f);
      if (a == 0) continue;
      target.blend_pixel(px, py, color, std::min<uint32_t>(a, 255u));
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
  const RectF target_rect = clip_to_surface(
      intersect(clip, RectF{min_x, min_y, max_x - min_x, max_y - min_y}), target);
  if (target_rect.is_empty()) {
    c_clip_rejects().add();
    return;
  }

  const int px0 = static_cast<int>(std::floor(target_rect.x));
  const int px1 = static_cast<int>(std::ceil(target_rect.right()));
  const int py0 = static_cast<int>(std::floor(target_rect.y));
  const int py1 = static_cast<int>(std::ceil(target_rect.bottom()));
  const int span = px1 - px0;
  if (span <= 0) return;

  constexpr int kSS = kSupersample;
  constexpr int kSamples = kSS * kSS;
  const float inv = 1.0f / static_cast<float>(kSS);

  std::vector<Crossing> crossings;
  crossings.reserve(points.size());
  std::vector<int> coverage(static_cast<size_t>(span), 0);

  for (int py = py0; py < py1; ++py) {
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
      for (int px = px0; px < px1; ++px) {
        int covered = 0;
        for (int i = 0; i < kSS; ++i) {
          const float sx = static_cast<float>(px) + (static_cast<float>(i) + 0.5f) * inv;
          while (k < crossings.size() && crossings[k].x <= sx) {
            winding += crossings[k].dir;
            ++k;
          }
          if (winding != 0) ++covered;
        }
        coverage[static_cast<size_t>(px - px0)] += covered;
      }
    }
    for (int px = px0; px < px1; ++px) {
      const int hits = coverage[static_cast<size_t>(px - px0)];
      if (hits == 0) continue;
      const uint32_t a = static_cast<uint32_t>(hits) * 255u / static_cast<uint32_t>(kSamples);
      if (a == 0) continue;
      target.blend_pixel(px, py, color, std::min<uint32_t>(a, 255u));
      c_pixels().add();
    }
  }
  c_paths().add();
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
        // 单轮廓路径：整段点视为一个闭合轮廓
        std::vector<Vec2> pts(list.points.begin() + op.first_point,
                              list.points.begin() + end);
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

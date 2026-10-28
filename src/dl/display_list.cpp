#include "dl/display_list.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "base/diag.h"
#include "base/hash.h"

namespace nq::dl {
namespace {

constexpr int kMaxFlattenDepth = 12;

float distance_to_line(Vec2 p, Vec2 a, Vec2 b) noexcept {
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  const float len2 = dx * dx + dy * dy;
  if (len2 <= 1e-12f) return std::sqrt((p.x - a.x) * (p.x - a.x) + (p.y - a.y) * (p.y - a.y));
  const float cross = (p.x - a.x) * dy - (p.y - a.y) * dx;
  return std::fabs(cross) / std::sqrt(len2);
}

constexpr int kCircleSegmentsMax = 720;

int circle_segments(float radius) noexcept {
  // ~2px 弦长，兼顾精度与开销
  const int n = static_cast<int>(kPi * static_cast<double>(radius) / 1.0);
  return clampi(n, 12, kCircleSegmentsMax);
}

}  // namespace

void PathBuilder::clear() noexcept {
  points_.clear();
  contour_ends_.clear();
  start_ = {};
  current_ = {};
  open_ = false;
}

void PathBuilder::move_to(Vec2 p) noexcept {
  if (open_) finish_contour();
  start_ = p;
  current_ = p;
  points_.push_back(p);
  open_ = true;
}

void PathBuilder::line_to(Vec2 p) noexcept {
  if (!open_) {
    move_to(p);
    return;
  }
  points_.push_back(p);
  current_ = p;
}

void PathBuilder::close() noexcept {
  if (!open_) return;
  if (!(current_.x == start_.x && current_.y == start_.y)) points_.push_back(start_);
  finish_contour();
  current_ = start_;
}

void PathBuilder::finish_contour() noexcept {
  contour_ends_.push_back(static_cast<uint32_t>(points_.size()));
  open_ = false;
}

void PathBuilder::flatten_quad(Vec2 p0, Vec2 p1, Vec2 p2, float tol, int depth) noexcept {
  if (depth >= kMaxFlattenDepth || distance_to_line(p1, p0, p2) <= tol) {
    line_to(p2);
    return;
  }
  const Vec2 a{(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
  const Vec2 b{(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f};
  const Vec2 m{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
  flatten_quad(p0, a, m, tol, depth + 1);
  flatten_quad(m, b, p2, tol, depth + 1);
}

void PathBuilder::flatten_cubic(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float tol, int depth) noexcept {
  const float d1 = distance_to_line(p1, p0, p3);
  const float d2 = distance_to_line(p2, p0, p3);
  if (depth >= kMaxFlattenDepth || std::max(d1, d2) <= tol) {
    line_to(p3);
    return;
  }
  const Vec2 p01{(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
  const Vec2 p12{(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f};
  const Vec2 p23{(p2.x + p3.x) * 0.5f, (p2.y + p3.y) * 0.5f};
  const Vec2 p012{(p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f};
  const Vec2 p123{(p12.x + p23.x) * 0.5f, (p12.y + p23.y) * 0.5f};
  const Vec2 mid{(p012.x + p123.x) * 0.5f, (p012.y + p123.y) * 0.5f};
  flatten_cubic(p0, p01, p012, mid, tol, depth + 1);
  flatten_cubic(mid, p123, p23, p3, tol, depth + 1);
}

void PathBuilder::quad_to(Vec2 control, Vec2 p, float tolerance) noexcept {
  if (!open_) move_to(control);
  flatten_quad(current_, control, p, tolerance, 0);
  current_ = p;
}

void PathBuilder::cubic_to(Vec2 c1, Vec2 c2, Vec2 p, float tolerance) noexcept {
  if (!open_) move_to(c1);
  flatten_cubic(current_, c1, c2, p, tolerance, 0);
  current_ = p;
}

void PathBuilder::add_circle(Vec2 center, float radius) noexcept {
  if (!(radius > 0.0f)) return;
  const int n = circle_segments(radius);
  for (int i = 0; i < n; ++i) {
    const double t = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(n);
    const Vec2 p{center.x + static_cast<float>(std::cos(t)) * radius,
                 center.y + static_cast<float>(std::sin(t)) * radius};
    if (i == 0) {
      move_to(p);
    } else {
      line_to(p);
    }
  }
  close();
}

void PathBuilder::add_rrect(RectF r, float radius) noexcept {
  if (r.is_empty()) return;
  const float max_r = std::min(r.w, r.h) * 0.5f;
  const float rad = clampf(radius, 0.0f, max_r);
  if (!(rad > 0.0f)) {
    move_to({r.x, r.y});
    line_to({r.right(), r.y});
    line_to({r.right(), r.bottom()});
    line_to({r.x, r.bottom()});
    close();
    return;
  }
  const int segments = clampi(static_cast<int>(rad), 2, 64);
  const double step = (kPi * 0.5) / static_cast<double>(segments);
  struct Corner {
    Vec2 center;
    double start;
  };
  const Corner corners[4] = {
      {{r.right() - rad, r.y + rad}, -kPi * 0.5},  // 右上
      {{r.right() - rad, r.bottom() - rad}, 0.0},  // 右下
      {{r.x + rad, r.bottom() - rad}, kPi * 0.5},  // 左下
      {{r.x + rad, r.y + rad}, kPi},               // 左上
  };
  bool first = true;
  for (const Corner& c : corners) {
    for (int i = 0; i <= segments; ++i) {
      const double a = c.start + step * static_cast<double>(i);
      const Vec2 p{c.center.x + static_cast<float>(std::cos(a)) * rad,
                   c.center.y + static_cast<float>(std::sin(a)) * rad};
      if (first) {
        move_to(p);
        first = false;
      } else {
        line_to(p);
      }
    }
  }
  close();
}

void Builder::clip_rect(RectF r) {
  push(Op::kClipRect, ClipRectOp{r.x, r.y, r.w, r.h});
  bounds_ = unite(bounds_, r);
  ++clip_depth_;
}

void Builder::pop_clip() {
  if (clip_depth_ <= 0) return;
  push(Op::kPopClip, std::array<uint32_t, 1>{0});
  --clip_depth_;
}

void Builder::fill_rect(RectF r, Rgba8 color) {
  if (r.is_empty()) return;
  push(Op::kFillRect, FillRectOp{r.x, r.y, r.w, r.h, color});
  bounds_ = unite(bounds_, r);
}

void Builder::fill_rrect(RectF r, float radius, Rgba8 color) {
  if (r.is_empty()) return;
  push(Op::kFillRRect, FillRRectOp{r.x, r.y, r.w, r.h, radius, color});
  bounds_ = unite(bounds_, r);
}

void Builder::fill_circle(Vec2 center, float radius, Rgba8 color) {
  if (!(radius > 0.0f)) return;
  push(Op::kFillCircle, FillCircleOp{center.x, center.y, radius, color});
  const RectF r{center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f};
  bounds_ = unite(bounds_, r);
}

void Builder::fill_path(const PathBuilder& path, Rgba8 color) {
  if (path.points().empty()) return;
  const uint32_t first = static_cast<uint32_t>(points_.size());
  points_.insert(points_.end(), path.points().begin(), path.points().end());
  push(Op::kFillPath,
       FillPathOp{first, static_cast<uint32_t>(path.points().size()), color});
  // 轮廓边界
  float minx = path.points()[0].x;
  float maxx = minx;
  float miny = path.points()[0].y;
  float maxy = miny;
  for (const Vec2& p : path.points()) {
    minx = std::min(minx, p.x);
    maxx = std::max(maxx, p.x);
    miny = std::min(miny, p.y);
    maxy = std::max(maxy, p.y);
  }
  bounds_ = unite(bounds_, RectF{minx, miny, maxx - minx, maxy - miny});
}

void Builder::fill_rect_gradient(RectF r, Rgba8 from, Rgba8 to, bool horizontal) {
  if (r.is_empty()) return;
  push(Op::kFillRectGradient,
       FillRectGradientOp{r.x, r.y, r.w, r.h, from, to, horizontal ? 1u : 0u, 0});
  bounds_ = unite(bounds_, r);
}

DisplayList Builder::finish() {
  DisplayList list;
  list.ops = std::move(ops_);
  list.points = std::move(points_);
  list.bounds = bounds_;
  list.op_count = op_count_;
  list.hash = hash_combine(hash_bytes(list.ops.data(), list.ops.size()),
                           hash_bytes(list.points.data(), list.points.size() * sizeof(Vec2)));
  ops_.clear();
  points_.clear();
  bounds_ = {};
  op_count_ = 0;
  clip_depth_ = 0;
  return list;
}

}  // namespace nq::dl

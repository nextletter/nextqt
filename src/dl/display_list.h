// NextQt · 显示列表 IR：不可变、可哈希、可序列化、可跨线程共享
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "base/geom.h"

namespace nq::dl {

inline constexpr uint16_t kAbiVersion = 1;

enum class Op : uint16_t {
  kClipRect = 1,
  kPopClip,
  kFillRect,
  kFillRRect,
  kFillCircle,
  kFillPath,
};

struct Header {
  uint32_t size;  // 含 header，4 字节对齐
  uint16_t op;
  uint16_t flags;
};
static_assert(sizeof(Header) == 8, "op header must be 8 bytes");

struct ClipRectOp {
  float x, y, w, h;
};
struct FillRectOp {
  float x, y, w, h;
  Rgba8 color;
};
struct FillRRectOp {
  float x, y, w, h;
  float radius;
  Rgba8 color;
};
struct FillCircleOp {
  float cx, cy, r;
  Rgba8 color;
};
struct FillPathOp {
  uint32_t first_point;
  uint32_t point_count;
  Rgba8 color;
};

// 路径构建：内部按展平容差把曲线转成折线（M0 阶段无曲线光栅）
class PathBuilder {
 public:
  void move_to(Vec2 p) noexcept;
  void line_to(Vec2 p) noexcept;
  void quad_to(Vec2 control, Vec2 p, float tolerance = 0.25f) noexcept;
  void cubic_to(Vec2 c1, Vec2 c2, Vec2 p, float tolerance = 0.25f) noexcept;
  void close() noexcept;
  void add_circle(Vec2 center, float radius) noexcept;
  void add_rrect(RectF r, float radius) noexcept;

  [[nodiscard]] const std::vector<Vec2>& points() const noexcept { return points_; }
  // 每个轮廓的结束索引（开区间）：[prev, end)
  [[nodiscard]] const std::vector<uint32_t>& contour_ends() const noexcept {
    return contour_ends_;
  }
  void clear() noexcept;

 private:
  void finish_contour() noexcept;
  void flatten_quad(Vec2 p0, Vec2 p1, Vec2 p2, float tol, int depth) noexcept;
  void flatten_cubic(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float tol, int depth) noexcept;

  std::vector<Vec2> points_;
  std::vector<uint32_t> contour_ends_;
  Vec2 start_{};
  Vec2 current_{};
  bool open_ = false;
};

struct DisplayList {
  std::vector<std::byte> ops;
  std::vector<Vec2> points;
  RectF bounds;
  uint64_t hash = 0;
  uint32_t op_count = 0;

  [[nodiscard]] bool empty() const noexcept { return ops.empty(); }
};

class Builder {
 public:
  void clip_rect(RectF r);
  void pop_clip();
  void fill_rect(RectF r, Rgba8 color);
  void fill_rrect(RectF r, float radius, Rgba8 color);
  void fill_circle(Vec2 center, float radius, Rgba8 color);
  void fill_path(const PathBuilder& path, Rgba8 color);

  [[nodiscard]] size_t byte_size() const noexcept { return ops_.size(); }
  [[nodiscard]] RectF bounds() const noexcept { return bounds_; }
  [[nodiscard]] int clip_depth() const noexcept { return clip_depth_; }

  DisplayList finish();

 private:
  template <class T>
  void push(Op op, const T& payload) {
    const uint32_t size = static_cast<uint32_t>(sizeof(Header) + sizeof(T));
    static_assert(sizeof(T) % 4 == 0, "payload must keep 4-byte alignment");
    const size_t offset = ops_.size();
    ops_.resize(offset + size);
    Header header{size, static_cast<uint16_t>(op), 0};
    std::memcpy(ops_.data() + offset, &header, sizeof(Header));
    std::memcpy(ops_.data() + offset + sizeof(Header), &payload, sizeof(T));
    ++op_count_;
  }

  std::vector<std::byte> ops_;
  std::vector<Vec2> points_;
  RectF bounds_{};
  uint32_t op_count_ = 0;
  int clip_depth_ = 0;
};

}  // namespace nq::dl

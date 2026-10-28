#include "dl/display_list.h"
#include "nqtest.h"

namespace {

using namespace nq;

dl::DisplayList build_scene() {
  dl::Builder b;
  b.fill_rect(RectF{0, 0, 100, 100}, premul(255, 255, 255));
  b.clip_rect(RectF{10, 10, 50, 50});
  b.fill_rrect(RectF{8, 8, 40, 40}, 8.0f, premul(0, 122, 255));
  b.fill_circle(Vec2{30, 30}, 10.0f, premul(255, 0, 0, 128));
  b.pop_clip();
  return b.finish();
}

}  // namespace

NQ_TEST(display_list_op_stream) {
  const dl::DisplayList list = build_scene();
  NQ_EXPECT(!list.empty());
  NQ_EXPECT_EQ(list.op_count, 5u);
  NQ_EXPECT(list.ops.size() > sizeof(dl::Header) * 5);
  // 操作流必须 4 字节对齐且可遍历
  size_t offset = 0;
  int seen = 0;
  while (offset + sizeof(dl::Header) <= list.ops.size()) {
    dl::Header header{};
    std::memcpy(&header, list.ops.data() + offset, sizeof(header));
    NQ_EXPECT(header.size >= sizeof(dl::Header));
    NQ_EXPECT_EQ(offset % 4, 0u);
    offset += header.size;
    ++seen;
  }
  NQ_EXPECT_EQ(seen, static_cast<int>(list.op_count));
  NQ_EXPECT_EQ(offset, list.ops.size());
}

NQ_TEST(display_list_bounds_cover_all_ops) {
  const dl::DisplayList list = build_scene();
  NQ_EXPECT_NEAR(list.bounds.x, 0.0f, 1e-6);
  NQ_EXPECT_NEAR(list.bounds.y, 0.0f, 1e-6);
  NQ_EXPECT_NEAR(list.bounds.right(), 100.0f, 1e-6);
  NQ_EXPECT_NEAR(list.bounds.bottom(), 100.0f, 1e-6);
}

NQ_TEST(display_list_hash_is_stable_and_content_sensitive) {
  const dl::DisplayList a = build_scene();
  const dl::DisplayList b = build_scene();
  NQ_EXPECT_EQ(a.hash, b.hash);
  NQ_EXPECT_EQ(a.ops, b.ops);

  dl::Builder other;
  other.fill_rect(RectF{0, 0, 100, 100}, premul(10, 20, 30));
  const dl::DisplayList c = other.finish();
  NQ_EXPECT(c.hash != a.hash);
}

NQ_TEST(path_flattening_closed_contour) {
  dl::PathBuilder p;
  p.add_circle(Vec2{50, 50}, 25.0f);
  NQ_EXPECT(p.points().size() >= 12);
  NQ_EXPECT_EQ(p.contour_ends().size(), 1u);
  NQ_EXPECT_EQ(p.contour_ends()[0], p.points().size());
  // 轮廓首尾重合（闭合）
  NQ_EXPECT_NEAR(p.points().front().x, p.points().back().x, 1e-4);
  NQ_EXPECT_NEAR(p.points().front().y, p.points().back().y, 1e-4);
}

NQ_TEST(path_curves_are_subdivided_by_tolerance) {
  dl::PathBuilder coarse;
  coarse.move_to({0, 0});
  coarse.cubic_to({0, 100}, {100, 100}, {100, 0}, 4.0f);
  dl::PathBuilder fine;
  fine.move_to({0, 0});
  fine.cubic_to({0, 100}, {100, 100}, {100, 0}, 0.25f);
  NQ_EXPECT(fine.points().size() > coarse.points().size());
}

NQ_TEST(clip_depth_is_balanced) {
  dl::Builder b;
  NQ_EXPECT_EQ(b.clip_depth(), 0);
  b.clip_rect(RectF{0, 0, 10, 10});
  b.clip_rect(RectF{5, 5, 10, 10});
  NQ_EXPECT_EQ(b.clip_depth(), 2);
  b.pop_clip();
  b.pop_clip();
  NQ_EXPECT_EQ(b.clip_depth(), 0);
  // 多余 pop 不产生下溢
  b.pop_clip();
  NQ_EXPECT_EQ(b.clip_depth(), 0);
}

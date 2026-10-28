#include <cmath>

#include "base/diag.h"
#include "dl/display_list.h"
#include "nqtest.h"
#include "raster/raster.h"
#include "raster/surface.h"

namespace {

using namespace nq;

const Rgba8 kClear = premul(0, 0, 0, 0);

double alpha_area(const Surface& s, int x0, int y0, int x1, int y1) {
  double sum = 0.0;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) sum += static_cast<double>(s.at(x, y).a);
  }
  return sum / 255.0;
}

}  // namespace

NQ_TEST(rect_integer_bounds_are_exact) {
  Surface s(8, 8);
  s.fill(kClear);
  raster::fill_rect_analytic(s, RectF{2, 3, 4, 2}, premul(255, 0, 0), s.bounds());

  for (int y = 3; y < 5; ++y) {
    for (int x = 2; x < 6; ++x) {
      NQ_EXPECT_EQ(s.at(x, y).a, 255);
      NQ_EXPECT_EQ(s.at(x, y).r, 255);
      NQ_EXPECT_EQ(s.at(x, y).g, 0);
    }
  }
  NQ_EXPECT_EQ(s.at(1, 3).a, 0);
  NQ_EXPECT_EQ(s.at(6, 3).a, 0);
  NQ_EXPECT_EQ(s.at(2, 2).a, 0);
  NQ_EXPECT_EQ(s.at(2, 5).a, 0);
  NQ_EXPECT_NEAR(alpha_area(s, 0, 0, 8, 8), 8.0, 1e-9);  // 4x2 像素 = 面积 8
}

NQ_TEST(rect_fractional_edges_have_exact_coverage) {
  Surface s(8, 8);
  s.fill(kClear);
  // 覆盖像素 (2,2) 的左半：x ∈ [2.5, 3.0)
  raster::fill_rect_analytic(s, RectF{2.5f, 2.0f, 0.5f, 1.0f}, premul(0, 0, 255), s.bounds());
  NQ_EXPECT_NEAR(s.at(2, 2).a, 128.0, 1.0);
  NQ_EXPECT_EQ(s.at(3, 2).a, 0);

  // 面积不变量：1.75 px² 的矩形
  Surface t(8, 8);
  t.fill(kClear);
  raster::fill_rect_analytic(t, RectF{1.5f, 1.5f, 1.75f, 1.0f}, premul(255, 255, 255), t.bounds());
  NQ_EXPECT_NEAR(alpha_area(t, 0, 0, 8, 8), 1.75, 0.01);
}

NQ_TEST(rect_clip_is_applied) {
  Surface s(8, 8);
  s.fill(kClear);
  raster::fill_rect_analytic(s, RectF{0, 0, 8, 8}, premul(255, 255, 255),
                             RectF{2, 2, 2, 2});
  NQ_EXPECT_EQ(s.at(1, 1).a, 0);
  NQ_EXPECT_EQ(s.at(2, 2).a, 255);
  NQ_EXPECT_EQ(s.at(3, 3).a, 255);
  NQ_EXPECT_EQ(s.at(4, 4).a, 0);
  NQ_EXPECT_NEAR(alpha_area(s, 0, 0, 8, 8), 4.0, 1e-9);
}

NQ_TEST(circle_area_matches_analytic) {
  Surface s(64, 64);
  s.fill(kClear);
  const float radius = 20.0f;
  dl::PathBuilder p;
  p.add_circle(Vec2{32, 32}, radius);
  raster::fill_polylines(s, p.points(), p.contour_ends(), premul(255, 255, 255), s.bounds());

  const double expected = kPi * radius * radius;
  const double actual = alpha_area(s, 0, 0, 64, 64);
  NQ_EXPECT_NEAR(actual / expected, 1.0, 0.01);  // 面积误差 < 1%
  // 圆心与边缘
  NQ_EXPECT_EQ(s.at(32, 32).a, 255);
  NQ_EXPECT_EQ(s.at(32, 32 - 30).a, 0);
}

NQ_TEST(rrect_via_display_list_has_antialiased_corners) {
  Surface s(48, 48);
  s.fill(kClear);
  dl::Builder b;
  b.fill_rrect(RectF{8, 8, 32, 32}, 10.0f, premul(0, 128, 255));
  const dl::DisplayList list = b.finish();
  raster::replay(list, s);

  NQ_EXPECT_EQ(s.at(24, 24).a, 255);   // 中心
  NQ_EXPECT_EQ(s.at(8, 24).a, 255);    // 左边中点
  NQ_EXPECT_EQ(s.at(8, 8).a, 0);       // 圆角外的角
  // 圆角区域存在部分覆盖率（抗锯齿）
  int partial = 0;
  for (int y = 8; y < 18; ++y) {
    for (int x = 8; x < 18; ++x) {
      const uint8_t a = s.at(x, y).a;
      if (a > 0 && a < 255) ++partial;
    }
  }
  NQ_EXPECT(partial > 0);
}

NQ_TEST(clip_stack_intersects_in_display_list) {
  Surface s(32, 32);
  s.fill(kClear);
  dl::Builder b;
  b.clip_rect(RectF{4, 4, 16, 16});
  b.clip_rect(RectF{12, 12, 16, 16});
  b.fill_rect(RectF{0, 0, 32, 32}, premul(255, 255, 255));
  b.pop_clip();
  b.pop_clip();
  b.fill_rect(RectF{0, 0, 2, 2}, premul(255, 0, 0));
  const dl::DisplayList list = b.finish();
  raster::replay(list, s);

  NQ_EXPECT_EQ(s.at(13, 13).a, 255);  // 交集内
  NQ_EXPECT_EQ(s.at(5, 5).a, 0);      // 仅第一个裁剪内
  NQ_EXPECT_EQ(s.at(0, 0).r, 255);    // 裁剪栈弹出后正常绘制
}

NQ_TEST(raster_is_deterministic) {
  auto render = [](Surface& out) {
    out = Surface(40, 40);
    out.fill(premul(20, 22, 26));
    dl::Builder b;
    b.fill_rrect(RectF{4, 4, 32, 32}, 9.0f, premul(0, 122, 255));
    b.fill_circle(Vec2{20, 20}, 9.0f, premul(255, 122, 0, 200));
    const dl::DisplayList list = b.finish();
    raster::replay(list, out);
  };
  Surface a;
  Surface b;
  render(a);
  render(b);
  NQ_EXPECT_EQ(a.content_hash(), b.content_hash());
  NQ_EXPECT_EQ(a.pixels, b.pixels);
}

NQ_TEST(raster_counters_record_work) {
  Surface s(16, 16);
  s.fill(kClear);
  auto& pixels = diag::counter("raster.pixels_shaded", "px");
  auto& ops = diag::counter("raster.ops", "op");
  const uint64_t pixels_before = pixels.value();
  const uint64_t ops_before = ops.value();

  dl::Builder b;
  b.fill_rect(RectF{0, 0, 4, 4}, premul(255, 255, 255));
  b.fill_circle(Vec2{10, 10}, 3.0f, premul(255, 0, 0));
  const dl::DisplayList list = b.finish();
  raster::replay(list, s);

  NQ_EXPECT(pixels.value() > pixels_before);
  NQ_EXPECT_EQ(ops.value(), ops_before + 2);
}

NQ_TEST(premultiplied_blending_over_opaque_background) {
  Surface s(4, 4);
  s.fill(premul(0, 0, 0));
  // 50% 白（预乘）覆盖在不透明黑上：颜色 128，结果仍然不透明
  raster::fill_rect_analytic(s, RectF{0, 0, 4, 4}, premul(255, 255, 255, 128), s.bounds());
  NQ_EXPECT_NEAR(s.at(1, 1).r, 128.0, 1.0);
  NQ_EXPECT_EQ(s.at(1, 1).a, 255);

  // 透明背景上的 50% 白应保留半透明预乘值
  Surface t(4, 4);
  t.fill(kClear);
  raster::fill_rect_analytic(t, RectF{0, 0, 4, 4}, premul(255, 255, 255, 128), t.bounds());
  NQ_EXPECT_NEAR(t.at(1, 1).r, 128.0, 1.0);
  NQ_EXPECT_NEAR(t.at(1, 1).a, 128.0, 1.0);
}

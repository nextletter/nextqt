#include "dl/display_list.h"
#include "golden_testing.h"
#include <memory>

#include "nqtest.h"
#include "raster/image_io.h"
#include "raster/raster.h"
#include "raster/surface.h"
#include "text/font.h"

namespace {

using namespace nq;

// M0 视觉基准场景：圆角卡片 + 圆形 + 裁剪 + 占位字形
Surface render_card_scene(text::Font& font) {
  Surface s(64, 64);
  s.fill(premul(245, 246, 248));

  dl::Builder b;
  b.fill_rrect(RectF{4, 4, 56, 56}, 12.0f, premul(255, 255, 255));
  b.fill_circle(Vec2{18, 18}, 8.0f, premul(0, 122, 255));
  b.clip_rect(RectF{8, 32, 48, 24});
  b.fill_rect(RectF{8, 32, 48, 12}, premul(0, 122, 255));
  b.fill_rect(RectF{8, 44, 48, 12}, premul(0, 200, 140));
  b.pop_clip();

  const dl::DisplayList list = b.finish();
  raster::replay(list, s);
  font.draw(s, 34, 26, "NQ", 15.0f, premul(30, 34, 40));
  return s;
}

}  // namespace

namespace {

text::Font& bundled_font() {
  static std::unique_ptr<text::Font> font = [] {
    auto result = text::Font::load_file(NQ_BUNDLED_FONT);
    return result.ok() ? std::move(*result) : nullptr;
  }();
  return *font;
}

}  // namespace

NQ_TEST(golden_card_scene) {
  const Surface s = render_card_scene(bundled_font());
  NQ_EXPECT(s.valid());
  nqtest::expect_golden(s, "card_scene");
}

NQ_TEST(golden_scene_is_reproducible) {
  const Surface a = render_card_scene(bundled_font());
  const Surface b = render_card_scene(bundled_font());
  NQ_EXPECT_EQ(a.content_hash(), b.content_hash());
}

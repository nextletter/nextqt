// NextQt · M0 示例：headless 渲染一张卡片并导出 PPM + 计数器
//
// 用法: ./nq_hello [输出路径]   （默认 out.ppm）
#include <cstdio>

#include "base/diag.h"
#include "dl/display_list.h"
#include "raster/raster.h"
#include "raster/surface.h"

namespace {

using namespace nq;

Surface render(int width, int height) {
  Surface surface(width, height);
  surface.fill(premul(245, 246, 248));

  dl::Builder builder;
  // 卡片
  builder.fill_rrect(RectF{16, 16, static_cast<float>(width) - 32,
                           static_cast<float>(height) - 32},
                     16.0f, premul(255, 255, 255));
  // 头像
  builder.fill_circle(Vec2{48, 48}, 18.0f, premul(0, 122, 255));
  // 分隔线
  builder.fill_rect(RectF{16, 80, static_cast<float>(width) - 32, 1}, premul(230, 232, 236));
  // 进度条（裁剪演示）
  builder.clip_rect(RectF{32, 100, static_cast<float>(width) - 64, 8});
  builder.fill_rrect(RectF{32, 100, static_cast<float>(width) - 64, 8}, 4.0f,
                     premul(232, 236, 242));
  builder.fill_rrect(RectF{32, 100, (static_cast<float>(width) - 64) * 0.62f, 8}, 4.0f,
                     premul(0, 122, 255));
  builder.pop_clip();

  const dl::DisplayList list = builder.finish();
  raster::replay(list, surface);

  draw_placeholder_glyphs(surface, 72, 34, 4, premul(24, 28, 34));
  return surface;
}

}  // namespace

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "out.ppm";
  const Surface surface = render(320, 160);
  if (!write_ppm(surface, path)) {
    std::fprintf(stderr, "failed to write %s\n", path);
    return 1;
  }
  std::printf("wrote %s (%dx%d)\n", path, surface.width, surface.height);
  std::printf("%s", diag::dump_json().c_str());
  return 0;
}

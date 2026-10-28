// NextQt · Showcase：8 个场景，全部由 M0 管线（显示列表 → CPU 光栅）渲染
//
// 用途：把设计文档里的"卖点"变成可执行、可对比、可回归的像素证据。
// 每个场景都是确定性的：同一份代码在不同机器上产出相同 PPM/PNG 与相同哈希。
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "base/diag.h"
#include "base/hash.h"
#include "dl/display_list.h"
#include "raster/image_io.h"
#include "raster/raster.h"
#include "raster/surface.h"
#include "text/bitmap_font.h"

namespace {

using namespace nq;

// ---------------------------------------------------------------- 主题与基础组件

struct Theme {
  Rgba8 bg;
  Rgba8 surface;
  Rgba8 surface_alt;
  Rgba8 border;
  Rgba8 text;
  Rgba8 text_dim;
  Rgba8 accent;
  Rgba8 accent_soft;
  Rgba8 ok;
  Rgba8 warn;
  Rgba8 danger;
  Rgba8 shadow;
};

Theme light_theme() {
  return Theme{premul(244, 246, 249), premul(255, 255, 255), premul(247, 249, 252),
               premul(229, 233, 240), premul(24, 28, 35),   premul(122, 131, 145),
               premul(0, 110, 245),  premul(226, 238, 255), premul(16, 163, 127),
               premul(240, 160, 32), premul(226, 76, 76),   premul(20, 30, 50)};
}

Theme dark_theme() {
  return Theme{premul(16, 18, 24),    premul(26, 29, 38),    premul(34, 38, 48),
               premul(48, 54, 66),    premul(238, 241, 246), premul(146, 155, 170),
               premul(76, 154, 255),  premul(30, 48, 74),    premul(58, 214, 166),
               premul(240, 176, 64),  premul(255, 107, 107), premul(0, 0, 0)};
}

void card(Surface& s, RectF r, float radius, Rgba8 fill, Rgba8 shadow, float blur) {
  if (blur > 0.0f) raster::fill_shadow(s, RectF{r.x, r.y + blur * 0.4f, r.w, r.h}, radius, blur,
                                       shadow, s.bounds());
  dl::Builder b;
  b.fill_rrect(r, radius, fill);
  const dl::DisplayList list = b.finish();
  raster::replay(list, s);
}

// 圆环（外圈 + 反向内圈 → 通过 nonzero 环绕规则挖洞）
void ring(Surface& s, Vec2 center, float r_outer, float thickness, Rgba8 color) {
  const float r_inner = std::max(0.0f, r_outer - thickness);
  dl::PathBuilder p;
  p.add_circle(center, r_outer);
  std::vector<Vec2> outer = p.points();
  const std::vector<uint32_t> outer_ends = p.contour_ends();
  dl::PathBuilder q;
  q.add_circle(center, r_inner);
  // 反向遍历内圈，使环绕数为 -1
  std::vector<Vec2> inner(q.points().rbegin(), q.points().rend());
  std::vector<Vec2> all = outer;
  all.insert(all.end(), inner.begin(), inner.end());
  std::vector<uint32_t> ends{outer_ends[0],
                             outer_ends[0] + static_cast<uint32_t>(inner.size())};
  raster::fill_polylines(s, all, ends, color, s.bounds());
}

// 进度圆弧（单轮廓：外弧 → 内弧反向 → 闭合）
void arc(Surface& s, Vec2 center, float r_outer, float thickness, float start, float sweep,
         Rgba8 color) {
  const float r_inner = r_outer - thickness;
  const int steps = std::max(8, static_cast<int>(std::fabs(sweep) * r_outer * 0.5f));
  dl::PathBuilder p;
  for (int i = 0; i <= steps; ++i) {
    const float a = start + sweep * static_cast<float>(i) / static_cast<float>(steps);
    const Vec2 pt{center.x + std::cos(a) * r_outer, center.y + std::sin(a) * r_outer};
    if (i == 0) {
      p.move_to(pt);
    } else {
      p.line_to(pt);
    }
  }
  for (int i = steps; i >= 0; --i) {
    const float a = start + sweep * static_cast<float>(i) / static_cast<float>(steps);
    p.line_to({center.x + std::cos(a) * r_inner, center.y + std::sin(a) * r_inner});
  }
  p.close();
  raster::fill_polylines(s, p.points(), p.contour_ends(), color, s.bounds());
}

// 折线描边：把中心线偏移成闭合多边形（近似 miter）
void stroke(Surface& s, const std::vector<Vec2>& pts, float width, Rgba8 color) {
  if (pts.size() < 2) return;
  const float hw = width * 0.5f;
  std::vector<Vec2> left;
  std::vector<Vec2> right;
  left.reserve(pts.size());
  right.reserve(pts.size());
  for (size_t i = 0; i < pts.size(); ++i) {
    Vec2 dir{0, 0};
    if (i == 0) {
      dir = pts[1] - pts[0];
    } else if (i + 1 == pts.size()) {
      dir = pts[i] - pts[i - 1];
    } else {
      const Vec2 d0 = pts[i] - pts[i - 1];
      const Vec2 d1 = pts[i + 1] - pts[i];
      const float l0 = std::sqrt(d0.x * d0.x + d0.y * d0.y);
      const float l1 = std::sqrt(d1.x * d1.x + d1.y * d1.y);
      dir = {(d0.x / (l0 > 0 ? l0 : 1) + d1.x / (l1 > 0 ? l1 : 1)),
             (d0.y / (l0 > 0 ? l0 : 1) + d1.y / (l1 > 0 ? l1 : 1))};
    }
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    const Vec2 n = len > 0 ? Vec2{-dir.y / len, dir.x / len} : Vec2{0, 0};
    left.push_back({pts[i].x + n.x * hw, pts[i].y + n.y * hw});
    right.push_back({pts[i].x - n.x * hw, pts[i].y - n.y * hw});
  }
  dl::PathBuilder p;
  p.move_to(left.front());
  for (size_t i = 1; i < left.size(); ++i) p.line_to(left[i]);
  for (size_t i = right.size(); i-- > 0;) p.line_to(right[i]);
  p.close();
  raster::fill_polylines(s, p.points(), p.contour_ends(), color, s.bounds());
}

// 圆角裁剪（M0 只有矩形裁剪 op）：先画到临时 surface，再用 rrect 覆盖率合成
void blit_masked(Surface& target, const Surface& source, RectF area, float radius) {
  const RectF clipped = intersect(area, target.bounds());
  if (clipped.is_empty()) return;
  const int x0 = static_cast<int>(std::floor(clipped.x));
  const int y0 = static_cast<int>(std::floor(clipped.y));
  const int w = static_cast<int>(std::ceil(clipped.right())) - x0;
  const int h = static_cast<int>(std::ceil(clipped.bottom())) - y0;
  if (w <= 0 || h <= 0) return;
  dl::PathBuilder path;
  path.add_rrect(area, radius);
  std::vector<uint8_t> mask(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
  raster::compute_coverage(path.points(), path.contour_ends(), x0, y0, w, h, mask.data());
  for (int row = 0; row < h; ++row) {
    for (int col = 0; col < w; ++col) {
      const uint32_t cov = mask[static_cast<size_t>(row) * static_cast<size_t>(w) +
                               static_cast<size_t>(col)];
      if (cov == 0) continue;
      target.blend_pixel(x0 + col, y0 + row, source.at(x0 + col, y0 + row), cov);
    }
  }
}

void pill(Surface& s, RectF r, Rgba8 fill, const char* label, Rgba8 label_color, int scale) {
  dl::Builder b;
  b.fill_rrect(r, r.h * 0.5f, fill);
  const dl::DisplayList list = b.finish();
  raster::replay(list, s);
  if (label != nullptr) {
    const int tw = text::measure(label, scale);
    const int th = text::kGlyphH * scale;
    text::draw_text(s, static_cast<int>(r.x + (r.w - static_cast<float>(tw)) * 0.5f),
                    static_cast<int>(r.y + (r.h - static_cast<float>(th)) * 0.5f), scale,
                    label_color, label);
  }
}

void hline(Surface& s, float x0, float y, float x1, Rgba8 color) {
  dl::Builder b;
  b.fill_rect(RectF{x0, y, x1 - x0, 1.0f}, color);
  const dl::DisplayList list = b.finish();
  raster::replay(list, s);
}

// ---------------------------------------------------------------- 场景

Surface scene_dashboard(bool dark) {
  const Theme t = dark ? dark_theme() : light_theme();
  Surface s(480, 300);
  s.fill(t.bg);

  // 左侧导航栏
  dl::Builder rail;
  rail.fill_rect(RectF{0, 0, 56, 300}, t.surface);
  const dl::DisplayList rail_list = rail.finish();
  raster::replay(rail_list, s);
  dl::Builder logo;
  logo.fill_rrect(RectF{16, 14, 24, 24}, 8.0f, t.accent);
  const dl::DisplayList logo_list = logo.finish();
  raster::replay(logo_list, s);
  for (int i = 0; i < 4; ++i) {
    const float y = 62.0f + static_cast<float>(i) * 40.0f;
    if (i == 0) {
      dl::Builder sel;
      sel.fill_rrect(RectF{12, y, 32, 28}, 9.0f, t.accent_soft);
      const dl::DisplayList sel_list = sel.finish();
      raster::replay(sel_list, s);
    }
    dl::Builder icon;
    icon.fill_rrect(RectF{22, y + 8, 12, 12}, 3.0f, i == 0 ? t.accent : t.text_dim);
    const dl::DisplayList icon_list = icon.finish();
    raster::replay(icon_list, s);
  }

  // 标题 + 状态
  text::draw_text(s, 72, 20, 2, t.text, "OVERVIEW");
  text::draw_text(s, 72, 40, 1, t.text_dim, "REALTIME METRICS / LAST 24 HOURS");
  pill(s, RectF{400, 16, 64, 20}, t.accent_soft, "LIVE", t.accent, 1);

  // 三张统计卡
  const char* labels[3] = {"REQUESTS", "LATENCY", "ERROR RATE"};
  const char* values[3] = {"128K", "24MS", "0.4%"};
  for (int i = 0; i < 3; ++i) {
    const RectF r{72 + static_cast<float>(i) * 132, 64, 120, 64};
    card(s, r, 12.0f, t.surface, premul(t.shadow.r, t.shadow.g, t.shadow.b, dark ? 120 : 26), 10.0f);
    text::draw_text(s, static_cast<int>(r.x + 12), static_cast<int>(r.y + 12), 1, t.text_dim,
                    labels[i]);
    text::draw_text(s, static_cast<int>(r.x + 12), static_cast<int>(r.y + 28), 2, t.text,
                    values[i]);
    dl::Builder spark;
    const Rgba8 c = i == 2 ? t.danger : t.accent;
    for (int k = 0; k < 12; ++k) {
      const float h = 4.0f + static_cast<float>((k * 7 + i * 5) % 10);
      spark.fill_rect(RectF{r.x + 12 + static_cast<float>(k) * 8, r.y + 52 - h, 5, h},
                      premul(c.r, c.g, c.b, 200));
    }
    const dl::DisplayList spark_list = spark.finish();
    raster::replay(spark_list, s);
  }

  // 图表卡
  const RectF chart{72, 140, 396, 96};
  card(s, chart, 12.0f, t.surface, premul(t.shadow.r, t.shadow.g, t.shadow.b, dark ? 120 : 24),
       10.0f);
  for (int i = 0; i < 4; ++i) {
    hline(s, chart.x + 12, chart.y + 18 + static_cast<float>(i) * 20, chart.right() - 12,
          t.border);
  }
  std::vector<Vec2> line;
  for (int i = 0; i < 32; ++i) {
    const float x = chart.x + 14 + static_cast<float>(i) * 11.6f;
    const float wave = std::sin(static_cast<float>(i) * 0.42f) * 12.0f +
                       std::sin(static_cast<float>(i) * 0.13f) * 8.0f;
    line.push_back({x, chart.y + 52 - wave});
  }
  // 面积（半透明渐变感：两层叠加）
  dl::PathBuilder area;
  area.move_to({line.front().x, chart.bottom() - 10});
  for (const Vec2& p : line) area.line_to(p);
  area.line_to({line.back().x, chart.bottom() - 10});
  area.close();
  raster::fill_polylines(s, area.points(), area.contour_ends(),
                         premul(t.accent.r, t.accent.g, t.accent.b, 40), s.bounds());
  stroke(s, line, 2.4f, t.accent);
  for (size_t i = 0; i < line.size(); i += 8) {
    dl::Builder dot;
    dot.fill_circle(line[i], 3.0f, t.surface);
    const dl::DisplayList dot_list = dot.finish();
    raster::replay(dot_list, s);
    dl::Builder dot2;
    dot2.fill_circle(line[i], 2.0f, t.accent);
    const dl::DisplayList dot2_list = dot2.finish();
    raster::replay(dot2_list, s);
  }

  // 底部列表
  const RectF list{72, 246, 396, 42};
  card(s, list, 12.0f, t.surface, premul(t.shadow.r, t.shadow.g, t.shadow.b, dark ? 120 : 20),
       8.0f);
  const char* rows[2] = {"API GATEWAY", "WORKER POOL"};
  for (int i = 0; i < 2; ++i) {
    const float y = list.y + 8 + static_cast<float>(i) * 18;
    dl::Builder dot;
    dot.fill_circle(Vec2{list.x + 16, y + 5}, 4.0f, i == 0 ? t.ok : t.warn);
    const dl::DisplayList dot_list = dot.finish();
    raster::replay(dot_list, s);
    text::draw_text(s, static_cast<int>(list.x + 28), static_cast<int>(y), 1, t.text, rows[i]);
    text::draw_text(s, static_cast<int>(list.right() - 54), static_cast<int>(y), 1, t.text_dim,
                    i == 0 ? "HEALTHY" : "DEGRADED");
  }
  return s;
}

Surface scene_controls() {
  const Theme t = light_theme();
  Surface s(480, 300);
  s.fill(t.bg);
  text::draw_text(s, 20, 16, 2, t.text, "CONTROLS");

  // 按钮行
  pill(s, RectF{20, 44, 96, 32}, t.accent, "PRIMARY", premul(255, 255, 255), 1);
  pill(s, RectF{126, 44, 96, 32}, t.surface, "SECONDARY", t.text, 1);
  dl::Builder outline;
  outline.fill_rrect(RectF{126.5f, 44.5f, 95, 31}, 16.0f, t.surface_alt);
  const dl::DisplayList outline_list = outline.finish();
  raster::replay(outline_list, s);
  text::draw_text(s, 148, 56, 1, t.text, "SECONDARY");
  pill(s, RectF{232, 44, 96, 32}, t.accent_soft, "GHOST", t.accent, 1);
  pill(s, RectF{338, 44, 96, 32}, t.surface_alt, "DISABLED", t.text_dim, 1);

  // 开关
  text::draw_text(s, 20, 96, 1, t.text_dim, "TOGGLES");
  for (int i = 0; i < 2; ++i) {
    const RectF track{20 + static_cast<float>(i) * 60, 112, 44, 24};
    dl::Builder b;
    b.fill_rrect(track, 12.0f, i == 0 ? t.ok : t.border);
    const dl::DisplayList l = b.finish();
    raster::replay(l, s);
    dl::Builder knob;
    const float kx = i == 0 ? track.right() - 21 : track.x + 3;
    knob.fill_circle(Vec2{kx + 9, track.y + 12}, 9.0f, premul(255, 255, 255));
    const dl::DisplayList kl = knob.finish();
    raster::replay(kl, s);
  }

  // 分段控件
  text::draw_text(s, 20, 152, 1, t.text_dim, "SEGMENTED");
  dl::Builder seg;
  seg.fill_rrect(RectF{20, 168, 176, 28}, 8.0f, t.surface_alt);
  const dl::DisplayList seg_l = seg.finish();
  raster::replay(seg_l, s);
  dl::Builder seg_sel;
  seg_sel.fill_rrect(RectF{22, 170, 56, 24}, 6.0f, t.surface);
  const dl::DisplayList seg_sel_l = seg_sel.finish();
  raster::replay(seg_sel_l, s);
  text::draw_text(s, 32, 178, 1, t.text, "DAY");
  text::draw_text(s, 96, 178, 1, t.text_dim, "WEEK");
  text::draw_text(s, 152, 178, 1, t.text_dim, "MONTH");

  // 滑块
  text::draw_text(s, 20, 210, 1, t.text_dim, "SLIDER");
  dl::Builder track;
  track.fill_rrect(RectF{20, 228, 176, 6}, 3.0f, t.border);
  const dl::DisplayList track_l = track.finish();
  raster::replay(track_l, s);
  dl::Builder fill;
  fill.fill_rrect(RectF{20, 228, 110, 6}, 3.0f, t.accent);
  const dl::DisplayList fill_l = fill.finish();
  raster::replay(fill_l, s);
  dl::Builder thumb;
  thumb.fill_circle(Vec2{130, 231}, 8.0f, t.accent);
  const dl::DisplayList thumb_l = thumb.finish();
  raster::replay(thumb_l, s);

  // 徽章 / chips
  text::draw_text(s, 20, 254, 1, t.text_dim, "BADGES");
  pill(s, RectF{20, 268, 52, 20}, t.accent_soft, "NEW", t.accent, 1);
  pill(s, RectF{80, 268, 62, 20}, premul(t.ok.r, t.ok.g, t.ok.b, 40), "DONE", t.ok, 1);
  pill(s, RectF{150, 268, 62, 20}, premul(t.warn.r, t.warn.g, t.warn.b, 48), "WARN", t.warn, 1);
  pill(s, RectF{220, 268, 54, 20}, premul(t.danger.r, t.danger.g, t.danger.b, 40), "ERR", t.danger,
       1);

  // 右侧：进度环
  const RectF panel{300, 96, 160, 192};
  card(s, panel, 14.0f, t.surface, premul(t.shadow.r, t.shadow.g, t.shadow.b, 26), 12.0f);
  const Vec2 center{panel.x + 80, panel.y + 70};
  ring(s, center, 46.0f, 12.0f, t.surface_alt);
  arc(s, center, 46.0f, 12.0f, -kPi * 0.5, kPi * 1.42, t.accent);
  text::draw_text(s, static_cast<int>(center.x - 14), static_cast<int>(center.y - 7), 2, t.text,
                  "71%");
  text::draw_text(s, static_cast<int>(panel.x + 52), static_cast<int>(panel.y + 138), 1,
                  t.text_dim, "CPU");
  return s;
}

Surface scene_chart() {
  const Theme t = dark_theme();
  Surface s(480, 300);
  s.fill(t.bg);
  text::draw_text(s, 20, 16, 2, t.text, "THROUGHPUT");
  text::draw_text(s, 20, 38, 1, t.text_dim, "REQUESTS/SEC · 5MIN BUCKETS");

  const RectF plot{24, 64, 432, 200};
  for (int i = 0; i <= 5; ++i) {
    hline(s, plot.x, plot.y + static_cast<float>(i) * 40, plot.right(),
          premul(t.border.r, t.border.g, t.border.b, 160));
  }
  std::vector<Vec2> a;
  std::vector<Vec2> b;
  for (int i = 0; i < 48; ++i) {
    const float x = plot.x + static_cast<float>(i) * (plot.w / 47.0f);
    const float ya = plot.y + 100 -
                     (std::sin(static_cast<float>(i) * 0.31f) * 30 +
                      std::sin(static_cast<float>(i) * 0.09f) * 22);
    const float yb = plot.y + 130 -
                     (std::sin(static_cast<float>(i) * 0.23f + 1.2f) * 26 +
                      std::sin(static_cast<float>(i) * 0.07f) * 14);
    a.push_back({x, ya});
    b.push_back({x, yb});
  }
  auto area = [&](const std::vector<Vec2>& line, Rgba8 color) {
    dl::PathBuilder p;
    p.move_to({line.front().x, plot.bottom()});
    for (const Vec2& pt : line) p.line_to(pt);
    p.line_to({line.back().x, plot.bottom()});
    p.close();
    raster::fill_polylines(s, p.points(), p.contour_ends(), color, s.bounds());
  };
  area(a, premul(t.accent.r, t.accent.g, t.accent.b, 34));
  area(b, premul(t.ok.r, t.ok.g, t.ok.b, 26));
  stroke(s, a, 2.0f, t.accent);
  stroke(s, b, 2.0f, t.ok);
  for (size_t i = 0; i < a.size(); i += 12) {
    dl::Builder dot;
    dot.fill_circle(a[i], 3.5f, t.accent);
    const dl::DisplayList l = dot.finish();
    raster::replay(l, s);
  }
  // 图例
  dl::Builder legend1;
  legend1.fill_rrect(RectF{24, 272, 10, 10}, 3.0f, t.accent);
  const dl::DisplayList l1 = legend1.finish();
  raster::replay(l1, s);
  text::draw_text(s, 40, 274, 1, t.text_dim, "READS");
  dl::Builder legend2;
  legend2.fill_rrect(RectF{104, 272, 10, 10}, 3.0f, t.ok);
  const dl::DisplayList l2 = legend2.finish();
  raster::replay(l2, s);
  text::draw_text(s, 120, 274, 1, t.text_dim, "WRITES");
  return s;
}

Surface scene_inbox() {
  const Theme t = light_theme();
  Surface s(480, 300);
  s.fill(t.bg);
  text::draw_text(s, 20, 16, 2, t.text, "INBOX");
  text::draw_text(s, 400, 22, 1, t.accent, "3 NEW");

  const char* names[5] = {"ADA LOVELACE", "ALAN TURING", "GRACE HOPPER", "LINUS T", "MARGARET H"};
  const char* msgs[5] = {"SHIPPED THE RENDERER PATCH", "REVIEWED YOUR TILE CACHE PR",
                         "FOUND A CLIPPING BUG IN 4K", "PERF RUN IS GREEN", "FONTS LOOK GREAT"};
  for (int i = 0; i < 5; ++i) {
    const RectF row{16, 48 + static_cast<float>(i) * 50, 448, 46};
    card(s, row, 10.0f, t.surface, premul(t.shadow.r, t.shadow.g, t.shadow.b, 20), 6.0f);
    dl::Builder av;
    av.fill_circle(Vec2{row.x + 28, row.y + 23}, 16.0f,
                   i % 2 == 0 ? t.accent : premul(t.ok.r, t.ok.g, t.ok.b, 220));
    const dl::DisplayList av_l = av.finish();
    raster::replay(av_l, s);
    const char initial[2] = {names[i][0], '\0'};
    text::draw_text(s, static_cast<int>(row.x + 24), static_cast<int>(row.y + 19), 1,
                    premul(255, 255, 255), initial);
    text::draw_text(s, static_cast<int>(row.x + 54), static_cast<int>(row.y + 10), 1,
                    i < 3 ? t.text : t.text_dim, names[i]);
    text::draw_text(s, static_cast<int>(row.x + 54), static_cast<int>(row.y + 26), 1,
                    t.text_dim, msgs[i]);
    if (i < 3) {
      dl::Builder dot;
      dot.fill_circle(Vec2{row.right() - 24, row.y + 23}, 4.0f, t.accent);
      const dl::DisplayList dot_l = dot.finish();
      raster::replay(dot_l, s);
    }
  }
  return s;
}

Surface scene_aa() {
  const Theme t = light_theme();
  Surface s(480, 300);
  s.fill(t.bg);
  text::draw_text(s, 20, 16, 2, t.text, "ANTIALIASING");
  text::draw_text(s, 20, 38, 1, t.text_dim,
                  "CIRCLES 1-20PX · RINGS · DIAGONALS · SUBPIXEL OFFSETS");

  // 尺寸阶梯
  for (int i = 0; i < 10; ++i) {
    dl::Builder b;
    b.fill_circle(Vec2{44 + static_cast<float>(i) * 40, 86}, 4.0f + static_cast<float>(i) * 1.7f,
                  t.accent);
    const dl::DisplayList l = b.finish();
    raster::replay(l, s);
  }
  // 半像素偏移的同一矩形：边缘灰阶应不同但面积一致
  for (int i = 0; i < 8; ++i) {
    const float off = static_cast<float>(i) * 0.125f;
    dl::Builder b;
    b.fill_rect(RectF{24 + static_cast<float>(i) * 54 + off, 130, 40, 40}, t.text);
    const dl::DisplayList l = b.finish();
    raster::replay(l, s);
  }
  // 同心圆环
  for (int i = 0; i < 6; ++i) {
    ring(s, Vec2{120, 236}, 44.0f - static_cast<float>(i) * 7.0f, 2.0f,
         i % 2 == 0 ? t.accent : t.ok);
  }
  // 细线扇（旋转矩形近似），检验非轴对齐边缘
  for (int i = 0; i < 12; ++i) {
    const float angle = -kPi * 0.5f + static_cast<float>(i) * (kPi / 12.0f);
    const float len = 62.0f;
    const Vec2 c{300, 244};
    const Vec2 d{std::cos(angle), std::sin(angle)};
    const Vec2 n{-d.y, d.x};
    dl::PathBuilder p;
    p.move_to({c.x + d.x * 0 - n.x * 0.6f, c.y + d.y * 0 - n.y * 0.6f});
    p.line_to({c.x + d.x * len - n.x * 0.6f, c.y + d.y * len - n.y * 0.6f});
    p.line_to({c.x + d.x * len + n.x * 0.6f, c.y + d.y * len + n.y * 0.6f});
    p.line_to({c.x + d.x * 0 + n.x * 0.6f, c.y + d.y * 0 + n.y * 0.6f});
    p.close();
    raster::fill_polylines(s, p.points(), p.contour_ends(), t.accent, s.bounds());
  }
  // 文本缩放阶梯
  const int scales[3] = {1, 2, 3};
  int y = 236;
  for (int i = 2; i >= 0; --i) {
    text::draw_text(s, 400, y - text::kGlyphH * scales[i], scales[i], t.text, "NQ");
    y -= text::kGlyphH * scales[i] + 4;
  }
  return s;
}

Surface scene_icons() {
  const Theme t = dark_theme();
  Surface s(480, 300);
  s.fill(t.bg);
  text::draw_text(s, 20, 16, 2, t.text, "ICON SET");
  text::draw_text(s, 20, 38, 1, t.text_dim, "PATHS ONLY · NO BITMAPS");

  auto tile = [&](int col, int row, Rgba8 accent) {
    const RectF r{20.0f + static_cast<float>(col) * 88.0f, 64.0f + static_cast<float>(row) * 78.0f,
                  76, 66};
    card(s, r, 12.0f, t.surface, premul(0, 0, 0, 90), 8.0f);
    return Vec2{r.x + 38, r.y + 33};
  };

  // 播放三角
  {
    const Vec2 c = tile(0, 0, t.accent);
    dl::PathBuilder p;
    p.move_to({c.x - 8, c.y - 11});
    p.line_to({c.x + 12, c.y});
    p.line_to({c.x - 8, c.y + 11});
    p.close();
    raster::fill_polylines(s, p.points(), p.contour_ends(), t.accent, s.bounds());
  }
  // 停止方块
  {
    const Vec2 c = tile(1, 0, t.ok);
    dl::Builder b;
    b.fill_rrect(RectF{c.x - 10, c.y - 10, 20, 20}, 4.0f, t.ok);
    const dl::DisplayList l = b.finish();
    raster::replay(l, s);
  }
  // 加号
  {
    const Vec2 c = tile(2, 0, t.warn);
    dl::Builder b;
    b.fill_rrect(RectF{c.x - 2, c.y - 11, 4, 22}, 2.0f, t.warn);
    b.fill_rrect(RectF{c.x - 11, c.y - 2, 22, 4}, 2.0f, t.warn);
    const dl::DisplayList l = b.finish();
    raster::replay(l, s);
  }
  // 勾
  {
    const Vec2 c = tile(3, 0, t.accent);
    dl::PathBuilder p;
    p.move_to({c.x - 11, c.y});
    p.line_to({c.x - 3, c.y + 8});
    p.line_to({c.x + 11, c.y - 8});
    stroke(s, p.points(), 4.0f, t.accent);
  }
  // 箭头
  {
    const Vec2 c = tile(4, 0, t.text);
    dl::PathBuilder p;
    p.move_to({c.x - 8, c.y - 8});
    p.line_to({c.x + 8, c.y});
    p.line_to({c.x - 8, c.y + 8});
    stroke(s, p.points(), 4.0f, t.text);
  }
  // 圆环
  {
    const Vec2 c = tile(0, 1, t.accent);
    ring(s, c, 12.0f, 4.0f, t.accent);
  }
  // 三横线（菜单）
  {
    const Vec2 c = tile(1, 1, t.text_dim);
    for (int i = -1; i <= 1; ++i) {
      dl::Builder b;
      b.fill_rrect(RectF{c.x - 11, c.y + static_cast<float>(i) * 6 - 1.5f, 22, 3}, 1.5f,
                   t.text_dim);
      const dl::DisplayList l = b.finish();
      raster::replay(l, s);
    }
  }
  // 心形（贝塞尔）
  {
    const Vec2 c = tile(2, 1, t.danger);
    dl::PathBuilder p;
    p.move_to({c.x, c.y + 10});
    p.cubic_to({c.x - 16, c.y - 2}, {c.x - 8, c.y - 14}, {c.x, c.y - 5});
    p.cubic_to({c.x + 8, c.y - 14}, {c.x + 16, c.y - 2}, {c.x, c.y + 10});
    p.close();
    raster::fill_polylines(s, p.points(), p.contour_ends(), t.danger, s.bounds());
  }
  // 网格
  {
    const Vec2 c = tile(3, 1, t.ok);
    for (int gy = 0; gy < 2; ++gy) {
      for (int gx = 0; gx < 2; ++gx) {
        dl::Builder b;
        b.fill_rrect(RectF{c.x - 10 + static_cast<float>(gx) * 11,
                           c.y - 10 + static_cast<float>(gy) * 11, 9, 9},
                     2.5f, t.ok);
        const dl::DisplayList l = b.finish();
        raster::replay(l, s);
      }
    }
  }
  // 放大镜
  {
    const Vec2 c = tile(0, 2, t.accent);
    ring(s, {c.x - 2, c.y - 2}, 10.0f, 3.0f, t.accent);
    dl::PathBuilder p;
    p.move_to({c.x + 5, c.y + 5});
    p.line_to({c.x + 12, c.y + 12});
    stroke(s, p.points(), 3.0f, t.accent);
  }
  // 铃铛
  {
    const Vec2 c = tile(1, 2, t.warn);
    dl::PathBuilder p;
    p.move_to({c.x - 9, c.y + 6});
    p.cubic_to({c.x - 9, c.y - 12}, {c.x + 9, c.y - 12}, {c.x + 9, c.y + 6});
    p.close();
    raster::fill_polylines(s, p.points(), p.contour_ends(), t.warn, s.bounds());
    dl::Builder b;
    b.fill_circle({c.x, c.y + 9}, 2.5f, t.warn);
    const dl::DisplayList l = b.finish();
    raster::replay(l, s);
  }
  // 星形
  {
    const Vec2 c = tile(2, 2, t.warn);
    dl::PathBuilder p;
    for (int i = 0; i < 10; ++i) {
      const float r = (i % 2 == 0) ? 13.0f : 5.5f;
      const float a = -kPi * 0.5f + static_cast<float>(i) * (kPi / 5.0f);
      const Vec2 pt{c.x + std::cos(a) * r, c.y + std::sin(a) * r};
      if (i == 0) {
        p.move_to(pt);
      } else {
        p.line_to(pt);
      }
    }
    p.close();
    raster::fill_polylines(s, p.points(), p.contour_ends(), t.warn, s.bounds());
  }
  // 时钟
  {
    const Vec2 c = tile(3, 2, t.text);
    ring(s, c, 12.0f, 2.5f, t.text);
    dl::PathBuilder p;
    p.move_to({c.x, c.y - 6});
    p.line_to({c.x, c.y});
    p.line_to({c.x + 5, c.y + 2});
    stroke(s, p.points(), 2.5f, t.text);
  }
  // 锁
  {
    const Vec2 c = tile(4, 2, t.ok);
    dl::Builder b;
    b.fill_rrect(RectF{c.x - 9, c.y - 2, 18, 14}, 3.0f, t.ok);
    const dl::DisplayList l = b.finish();
    raster::replay(l, s);
    dl::PathBuilder p;
    p.move_to({c.x - 6, c.y - 2});
    p.line_to({c.x - 6, c.y - 8});
    p.cubic_to({c.x - 6, c.y - 16}, {c.x + 6, c.y - 16}, {c.x + 6, c.y - 8});
    p.line_to({c.x + 6, c.y - 2});
    stroke(s, p.points(), 2.5f, t.ok);
  }
  // 柱状图图标
  {
    const Vec2 c = tile(4, 1, t.accent);
    const float heights[3] = {10, 18, 13};
    for (int i = 0; i < 3; ++i) {
      dl::Builder b;
      b.fill_rrect(RectF{c.x - 11 + static_cast<float>(i) * 8, c.y + 10 - heights[i], 5,
                         heights[i]},
                   2.0f, t.accent);
      const dl::DisplayList l = b.finish();
      raster::replay(l, s);
    }
  }
  return s;
}

Surface scene_clip_shadow() {
  const Theme t = dark_theme();
  Surface s(480, 300);
  s.fill(t.bg);
  text::draw_text(s, 20, 16, 2, t.text, "CLIP & SHADOW");
  text::draw_text(s, 20, 38, 1, t.text_dim, "NESTED ROUNDED CLIPS · MASK BLUR SHADOWS");

  // 嵌套圆角裁剪：外层 20px 圆角，内层再裁一个 60px 圆角方块
  const RectF panel{24, 64, 200, 200};
  dl::Builder clip_b;
  clip_b.fill_rrect(panel, 20.0f, premul(40, 46, 60));
  const dl::DisplayList clip_l = clip_b.finish();
  raster::replay(clip_l, s);

  Surface stripes(s.width, s.height);  // 默认全透明
  for (int i = 0; i < 12; ++i) {
    dl::Builder stripe;
    stripe.fill_rect(RectF{panel.x, panel.y + static_cast<float>(i) * 18, panel.w, 9},
                     premul(t.accent.r, t.accent.g, t.accent.b, 120));
    const dl::DisplayList l = stripe.finish();
    raster::replay(l, stripes);
  }
  blit_masked(s, stripes, panel, 20.0f);

  Surface inner_source(s.width, s.height);
  dl::Builder inner;
  inner.fill_circle(Vec2{124, 164}, 52.0f, premul(0, 0, 0, 110));
  const dl::DisplayList inner_l = inner.finish();
  raster::replay(inner_l, inner_source);
  blit_masked(s, inner_source, panel, 20.0f);
  dl::Builder inner2;
  inner2.fill_circle(Vec2{124, 164}, 38.0f, premul(t.ok.r, t.ok.g, t.ok.b, 220));
  const dl::DisplayList inner2_l = inner2.finish();
  raster::replay(inner2_l, s);

  // 阴影模糊阶梯
  const float blurs[3] = {4.0f, 10.0f, 20.0f};
  for (int i = 0; i < 3; ++i) {
    const RectF r{252, 70 + static_cast<float>(i) * 62, 200, 44};
    card(s, r, 10.0f, t.surface, premul(0, 0, 0, 170), blurs[i]);
    char label[32];
    std::snprintf(label, sizeof(label), "BLUR %d", static_cast<int>(blurs[i]));
    text::draw_text(s, static_cast<int>(r.x + 12), static_cast<int>(r.y + 16), 1, t.text_dim,
                    label);
  }

  // 圆角进度条
  const RectF bar{24, 276, 428, 10};
  dl::Builder track;
  track.fill_rrect(bar, 5.0f, premul(48, 54, 66));
  const dl::DisplayList track_l = track.finish();
  raster::replay(track_l, s);
  dl::Builder progress;
  progress.fill_rrect(RectF{bar.x, bar.y, bar.w * 0.63f, bar.h}, 5.0f, t.accent);
  const dl::DisplayList progress_l = progress.finish();
  raster::replay(progress_l, s);
  return s;
}

// ---------------------------------------------------------------- 驱动

struct Scene {
  const char* name;
  Surface (*render)();
};

Surface render_dashboard_light() { return scene_dashboard(false); }
Surface render_dashboard_dark() { return scene_dashboard(true); }

const Scene kScenes[] = {
    {"dashboard_light", &render_dashboard_light},
    {"dashboard_dark", &render_dashboard_dark},
    {"controls", &scene_controls},
    {"chart", &scene_chart},
    {"inbox", &scene_inbox},
    {"aa", &scene_aa},
    {"icons", &scene_icons},
    {"clip_shadow", &scene_clip_shadow},
};

int usage() {
  std::printf(
      "usage: nq_showcase [--out DIR] [--scene NAME|all] [--list] [--no-png]\n"
      "scenes: ");
  for (const Scene& s : kScenes) std::printf("%s ", s.name);
  std::printf("\n");
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  std::string out_dir = "showcase";
  std::string only = "all";
  bool write_png_files = true;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.rfind("--out=", 6) == 0) {
      out_dir = arg.substr(6);
    } else if (arg == "--out" && i + 1 < argc) {
      out_dir = argv[++i];
    } else if (arg.rfind("--scene=", 8) == 0) {
      only = arg.substr(8);
    } else if (arg == "--scene" && i + 1 < argc) {
      only = argv[++i];
    } else if (arg == "--no-png") {
      write_png_files = false;
    } else if (arg == "--list") {
      for (const Scene& s : kScenes) std::printf("%s\n", s.name);
      return 0;
    } else {
      return usage();
    }
  }

  std::string mkdir_cmd = "mkdir -p '" + out_dir + "'";
  if (std::system(mkdir_cmd.c_str()) != 0) {
    std::fprintf(stderr, "cannot create %s\n", out_dir.c_str());
    return 1;
  }

  int count = 0;
  for (const Scene& scene : kScenes) {
    if (only != "all" && only != scene.name) continue;
    const Surface surface = scene.render();
    const std::string base = out_dir + "/" + scene.name;
    const std::string ppm_path = base + ".ppm";
    if (!write_ppm(surface, ppm_path.c_str())) {
      std::fprintf(stderr, "failed to write %s\n", ppm_path.c_str());
      return 1;
    }
    if (write_png_files && !write_png(surface, (base + ".png").c_str())) {
      std::fprintf(stderr, "failed to write %s.png\n", base.c_str());
      return 1;
    }
    std::printf("%-16s %dx%d  hash=%016llx  -> %s.ppm%s\n", scene.name, surface.width,
                surface.height,
                static_cast<unsigned long long>(surface.content_hash()),
                base.c_str(), write_png_files ? " + .png" : "");
    ++count;
  }
  std::printf("\n%d scene(s) rendered\n%s", count, diag::dump_json().c_str());
  return 0;
}

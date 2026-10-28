// 边界与错误路径测试：这些是 example 与 golden 场景永远覆盖不到的分支
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "base/arena.h"
#include "base/diag.h"
#include "base/geom.h"
#include "base/hash.h"
#include "base/layout_unit.h"
#include "dl/display_list.h"
#include "nqtest.h"
#include "raster/image_io.h"
#include "raster/raster.h"
#include "raster/surface.h"
#include "text/bitmap_font.h"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#define NQ_HAS_FORK 1
#endif

namespace {

using namespace nq;

std::string tmp(const char* name) { return std::string(NQ_BUILD_DIR) + "/" + name; }

std::vector<uint8_t> read_all(const std::string& path) {
  std::vector<uint8_t> out;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return out;
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size > 0) {
    out.resize(static_cast<size_t>(size));
    const size_t got = std::fread(out.data(), 1, out.size(), f);
    out.resize(got);
  }
  std::fclose(f);
  return out;
}

uint32_t be32(const std::vector<uint8_t>& d, size_t at) {
  return (static_cast<uint32_t>(d[at]) << 24) | (static_cast<uint32_t>(d[at + 1]) << 16) |
         (static_cast<uint32_t>(d[at + 2]) << 8) | static_cast<uint32_t>(d[at + 3]);
}

// 极简 PNG 解析：只支持我们自己写的 stored-deflate，用于验证编码器确实产出合法 PNG
struct DecodedPng {
  bool ok = false;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgb;
};

DecodedPng decode_our_png(const std::vector<uint8_t>& data) {
  DecodedPng out;
  const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (data.size() < 8 || std::memcmp(data.data(), sig, 8) != 0) return out;
  size_t pos = 8;
  std::vector<uint8_t> idat;
  bool saw_ihdr = false;
  while (pos + 12 <= data.size()) {
    const uint32_t len = be32(data, pos);
    if (pos + 12 + len > data.size()) return out;
    const char* type = reinterpret_cast<const char*>(data.data() + pos + 4);
    const uint8_t* body = data.data() + pos + 8;
    if (std::memcmp(type, "IHDR", 4) == 0 && len >= 13) {
      out.width = be32(data, pos + 8);
      out.height = be32(data, pos + 12);
      saw_ihdr = true;
    } else if (std::memcmp(type, "IDAT", 4) == 0) {
      idat.insert(idat.end(), body, body + len);
    } else if (std::memcmp(type, "IEND", 4) == 0) {
      break;
    }
    pos += 12 + len;
  }
  if (!saw_ihdr || idat.size() < 6) return out;
  // zlib 头 2 字节，尾部 4 字节 adler
  size_t p = 2;
  std::vector<uint8_t> raw;
  while (p + 5 <= idat.size()) {
    const uint8_t bfinal = idat[p] & 1u;
    const uint8_t btype = (idat[p] >> 1) & 0x3u;
    if (btype != 0) return out;  // 我们只写 stored 块
    const uint32_t len = static_cast<uint32_t>(idat[p + 1]) |
                         (static_cast<uint32_t>(idat[p + 2]) << 8);
    const uint32_t nlen = static_cast<uint32_t>(idat[p + 3]) |
                          (static_cast<uint32_t>(idat[p + 4]) << 8);
    if ((len ^ 0xFFFFu) != nlen) return out;
    p += 5;
    if (p + len > idat.size()) return out;
    raw.insert(raw.end(), idat.begin() + static_cast<long>(p),
               idat.begin() + static_cast<long>(p + len));
    p += len;
    if (bfinal) break;
  }
  const size_t stride = static_cast<size_t>(out.width) * 3 + 1;
  if (raw.size() < stride * out.height) return out;
  out.rgb.resize(static_cast<size_t>(out.width) * out.height * 3);
  for (uint32_t y = 0; y < out.height; ++y) {
    if (raw[static_cast<size_t>(y) * stride] != 0) return out;
    std::memcpy(out.rgb.data() + static_cast<size_t>(y) * out.width * 3,
                raw.data() + static_cast<size_t>(y) * stride + 1,
                static_cast<size_t>(out.width) * 3);
  }
  out.ok = true;
  return out;
}

}  // namespace

// ------------------------------------------------------------------ image_io

NQ_TEST(png_roundtrip_matches_pixels) {
  Surface s(7, 5);
  s.fill(premul(0, 0, 0));
  dl::Builder b;
  b.fill_rect(RectF{0, 0, 3, 5}, premul(255, 0, 0));
  b.fill_rect(RectF{3, 0, 4, 2}, premul(0, 255, 0, 128));
  const dl::DisplayList list = b.finish();
  raster::replay(list, s);

  const std::string path = tmp("rt.png");
  NQ_EXPECT(write_png(s, path.c_str()));
  const std::vector<uint8_t> bytes = read_all(path);
  NQ_EXPECT(bytes.size() > 64);
  const DecodedPng png = decode_our_png(bytes);
  NQ_EXPECT(png.ok);
  NQ_EXPECT_EQ(png.width, 7u);
  NQ_EXPECT_EQ(png.height, 5u);
  // 红色区域与白色背景合成后仍是红
  NQ_EXPECT_EQ(static_cast<int>(png.rgb[0]), 255);
  NQ_EXPECT_EQ(static_cast<int>(png.rgb[1]), 0);
  // 半透明绿色（预乘）叠在不透明黑上：颜色 128，结果不透明 → PNG 直通值 (0,128,0)
  const size_t idx = (0 * 7 + 4) * 3;
  NQ_EXPECT_EQ(static_cast<int>(png.rgb[idx]), 0);
  NQ_EXPECT_NEAR(static_cast<int>(png.rgb[idx + 1]), 128, 2);
}

NQ_TEST(png_multi_block_for_large_images) {
  Surface s(200, 200);  // raw = 200 * (600+1) > 65535 → 触发多 stored 块
  s.fill(premul(30, 40, 50));
  const std::string path = tmp("large.png");
  NQ_EXPECT(write_png(s, path.c_str()));
  const DecodedPng png = decode_our_png(read_all(path));
  NQ_EXPECT(png.ok);
  NQ_EXPECT_EQ(png.width, 200u);
  NQ_EXPECT_EQ(static_cast<int>(png.rgb[0]), 30);
  NQ_EXPECT_EQ(static_cast<int>(png.rgb[png.rgb.size() - 1]), 50);
}

NQ_TEST(png_and_ppm_reject_invalid_input) {
  Surface empty;
  NQ_EXPECT(!write_png(empty, tmp("no.png").c_str()));
  NQ_EXPECT(!write_ppm(empty, tmp("no.ppm").c_str()));
  Surface s(4, 4);
  NQ_EXPECT(!write_png(s, "/nonexistent-dir/nq/x.png"));
  NQ_EXPECT(!write_ppm(s, "/nonexistent-dir/nq/x.ppm"));
}

NQ_TEST(ppm_header_is_valid) {
  Surface s(3, 2);
  s.fill(premul(255, 255, 255, 128));
  const std::string path = tmp("hdr.ppm");
  NQ_EXPECT(write_ppm(s, path.c_str()));
  const std::vector<uint8_t> bytes = read_all(path);
  NQ_EXPECT(bytes.size() == 3 * 2 * 3 + 11);
  NQ_EXPECT(std::memcmp(bytes.data(), "P6\n3 2\n255\n", 11) == 0);
}

// ------------------------------------------------------------------ surface

NQ_TEST(surface_bounds_and_diff) {
  Surface a(4, 4);
  a.fill(premul(10, 20, 30));
  Surface b = a;
  NQ_EXPECT_EQ(mean_abs_diff(a, b), 0.0);
  NQ_EXPECT_EQ(a.at(-1, 0).a, 0);
  NQ_EXPECT_EQ(a.at(0, -1).a, 0);
  NQ_EXPECT_EQ(a.at(4, 4).a, 0);
  NQ_EXPECT_EQ(a.at(3, 3).g, 20);
  NQ_EXPECT_EQ(a.row(0)[0], 10);

  Surface c(8, 8);
  NQ_EXPECT_EQ(mean_abs_diff(a, c), 255.0);

  Surface d = a;
  d.fill(premul(10, 20, 40));
  NQ_EXPECT(mean_abs_diff(a, d) > 0.0);

  Surface empty1;
  Surface empty2;
  NQ_EXPECT_EQ(mean_abs_diff(empty1, empty2), 0.0);
  NQ_EXPECT(!empty1.valid());
  NQ_EXPECT(empty1.bounds().is_empty());
}

// ------------------------------------------------------------------ arena / diag

NQ_TEST(arena_rejects_zero_and_absurd_sizes) {
  Arena arena(4096);
  NQ_EXPECT(arena.alloc(0, 16) == nullptr);
  NQ_EXPECT(arena.alloc(~static_cast<size_t>(0) / 2, 16) == nullptr);  // 首个块就分配失败
  NQ_ASSERT(arena.alloc(16, 16) != nullptr);
  // 已有块时再申请超大块 → 走"新增块失败"分支
  NQ_EXPECT(arena.alloc(~static_cast<size_t>(0) / 2, 16) == nullptr);
  // 多块后 reset：保留一个块并释放其余
  NQ_ASSERT(arena.alloc(256 * 1024, 64) != nullptr);
  NQ_ASSERT(arena.block_count() >= 2u);
  arena.reset();
  NQ_EXPECT_EQ(arena.bytes_used(), 0u);
  NQ_EXPECT_EQ(arena.block_count(), 1u);
  arena.release();
  NQ_EXPECT_EQ(arena.bytes_committed(), 0u);
  NQ_EXPECT_EQ(arena.block_count(), 0u);
  NQ_EXPECT(arena.alloc(64, 32) != nullptr);
}

NQ_TEST(diag_registry_behaviour) {
  auto& c1 = diag::counter("test.shared", "ms");
  auto& c2 = diag::counter("test.shared", "ms");
  NQ_EXPECT(&c1 == &c2);
  c1.add(5);
  const std::string json = diag::dump_json();
  NQ_EXPECT(json.find("test.shared") != std::string::npos);
  NQ_EXPECT(json.find("\"ms\"") != std::string::npos);
  diag::reset_all();
  NQ_EXPECT_EQ(c1.value(), 0u);
  const std::string json_after = diag::dump_json();
  NQ_EXPECT(json_after.find("test.shared") != std::string::npos);
}

#ifdef NQ_HAS_FORK
NQ_TEST(assert_fail_aborts_process) {
  const pid_t pid = fork();
  if (pid == 0) {
    // 子进程：触发断言失败，期望 abort
    NQ_FAIL("intentional failure to exercise report_failure/assert path");
    nq::assert_fail("intentional", __FILE__, __LINE__);
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  NQ_EXPECT(WIFSIGNALED(status));
}
#endif

// ------------------------------------------------------------------ display list

NQ_TEST(path_builder_edge_cases) {
  dl::PathBuilder p;
  NQ_EXPECT(p.points().empty());
  p.clear();
  p.line_to({1, 1});  // 未 move_to：隐式 move
  NQ_EXPECT_EQ(p.points().size(), 1u);
  p.quad_to({2, 2}, {3, 3});
  NQ_EXPECT(p.points().size() >= 2);
  p.close();
  p.close();  // 重复 close 不产生额外轮廓
  NQ_EXPECT_EQ(p.contour_ends().size(), 1u);

  dl::PathBuilder r;
  r.add_rrect(RectF{0, 0, 10, 10}, 0.0f);  // 半径 0 → 直角
  NQ_EXPECT_EQ(r.points().size(), 5u);
  NQ_EXPECT_EQ(r.points().back().x, r.points().front().x);

  dl::PathBuilder big;
  big.add_rrect(RectF{0, 0, 10, 10}, 99.0f);  // 半径超过一半 → 夹紧
  NQ_EXPECT(big.points().size() > 5u);

  dl::PathBuilder zero;
  zero.add_rrect(RectF{0, 0, 0, 0}, 4.0f);  // 空矩形
  NQ_EXPECT(zero.points().empty());
  zero.add_circle({0, 0}, 0.0f);
  NQ_EXPECT(zero.points().empty());
}

NQ_TEST(builder_gradient_and_reset) {
  dl::Builder b;
  b.fill_rect_gradient(RectF{0, 0, 10, 4}, premul(255, 0, 0), premul(0, 0, 255), false);
  b.fill_rect_gradient(RectF{0, 0, 0, 0}, premul(255, 0, 0), premul(0, 0, 255), true);
  const dl::DisplayList list = b.finish();
  NQ_EXPECT_EQ(list.op_count, 1u);  // 空矩形被跳过
  NQ_EXPECT_EQ(b.byte_size(), 0u);  // finish 后重置
  NQ_EXPECT_EQ(b.clip_depth(), 0);
  NQ_EXPECT(b.bounds().is_empty());
}

NQ_TEST(builder_skips_degenerate_primitives) {
  dl::Builder b;
  b.fill_rect(RectF{0, 0, 0, 10}, premul(255, 255, 255));
  b.fill_rrect(RectF{0, 0, 10, 0}, 4.0f, premul(255, 255, 255));
  b.fill_circle({5, 5}, -1.0f, premul(255, 255, 255));
  dl::PathBuilder empty_path;
  b.fill_path(empty_path, premul(255, 255, 255));
  const dl::DisplayList list = b.finish();
  NQ_EXPECT_EQ(list.op_count, 0u);
  NQ_EXPECT(list.empty());
}

NQ_TEST(path_op_records_points) {
  dl::PathBuilder p;
  p.move_to({1, 1});
  p.line_to({5, 1});
  p.line_to({5, 5});
  p.close();
  dl::Builder b;
  b.fill_path(p, premul(0, 0, 255));
  const dl::DisplayList list = b.finish();
  NQ_EXPECT_EQ(list.op_count, 1u);
  NQ_EXPECT_EQ(list.points.size(), p.points().size());
}

// ------------------------------------------------------------------ raster

NQ_TEST(gradient_interpolates_and_clamps) {
  Surface s(8, 4);
  s.fill(premul(0, 0, 0));
  raster::fill_rect_gradient(s, RectF{0, 0, 8, 4}, premul(0, 0, 0), premul(255, 255, 255),
                             true, s.bounds());
  NQ_EXPECT_EQ(s.at(0, 0).r, 16);
  NQ_EXPECT_NEAR(s.at(3, 0).r, 112.0, 8.0);
  NQ_EXPECT(s.at(7, 0).r >= 230);  // 最后一个像素中心位于 7.5/8 处

  Surface v(4, 8);
  v.fill(premul(0, 0, 0));
  raster::fill_rect_gradient(v, RectF{0, 0, 4, 8}, premul(0, 0, 0), premul(0, 0, 255), false,
                             v.bounds());
  NQ_EXPECT(v.at(0, 7).b >= 230);
  NQ_EXPECT(v.at(0, 0).b <= 24);

  // 裁剪：区域外不画
  Surface c(8, 4);
  c.fill(premul(0, 0, 0));
  raster::fill_rect_gradient(c, RectF{0, 0, 8, 4}, premul(255, 0, 0), premul(255, 0, 0), true,
                             RectF{0, 0, 2, 4});
  NQ_EXPECT_EQ(c.at(1, 1).r, 255);
  NQ_EXPECT_EQ(c.at(4, 1).r, 0);
}

NQ_TEST(shadow_falls_off_and_respects_bounds) {
  Surface s(64, 64);
  s.fill(premul(255, 255, 255));
  raster::fill_shadow(s, RectF{16, 16, 32, 32}, 8.0f, 8.0f, premul(0, 0, 0, 160), s.bounds());
  const int near_alpha = s.at(16, 14).r;   // 靠近边缘：应被压暗
  const int far_alpha = s.at(16, 2).r;     // 远离：接近纯白
  NQ_EXPECT(near_alpha < 255);
  NQ_EXPECT(near_alpha < far_alpha);
  // 形状内部：覆盖率为 1，颜色 alpha=160 → 白底被压暗到 255-160
  NQ_EXPECT_NEAR(s.at(32, 32).r, 95.0, 2.0);
  NQ_EXPECT_EQ(s.at(0, 0).r, 255);

  // 参数退化：直接返回，不崩溃
  raster::fill_shadow(s, RectF{}, 8.0f, 8.0f, premul(0, 0, 0), s.bounds());
  raster::fill_shadow(s, RectF{10, 10, 10, 10}, 4.0f, 0.0f, premul(0, 0, 0), s.bounds());
  raster::fill_shadow(s, RectF{10, 10, 10, 10}, 4.0f, 4.0f, premul(0, 0, 0), RectF{});
}

NQ_TEST(coverage_mask_nz_winding_with_hole) {
  // 外圈正方形 + 反向内圈正方形 → 中间为空（nonzero 规则）
  const std::vector<Vec2> pts = {{0, 0}, {4, 0}, {4, 4}, {0, 4}, {0, 0},
                                 {1, 1}, {1, 2}, {2, 2}, {2, 1}, {1, 1}};
  const std::vector<uint32_t> ends{5, 10};
  std::vector<uint8_t> mask(16, 0);
  raster::compute_coverage(pts, ends, 0, 0, 4, 4, mask.data());
  NQ_EXPECT_EQ(mask[0], 255);              // 边框内
  NQ_EXPECT_EQ(mask[1 * 4 + 1], 0);        // 洞里
  NQ_EXPECT_EQ(mask[3 * 4 + 3], 255);
}

NQ_TEST(replay_handles_malformed_streams) {
  // 未知 opcode
  {
    std::vector<std::byte> ops(sizeof(dl::Header));
    dl::Header h{sizeof(dl::Header), 999, 0};
    std::memcpy(ops.data(), &h, sizeof(h));
    dl::DisplayList list;
    list.ops = ops;
    Surface s(8, 8);
    s.fill(premul(0, 0, 0));
    raster::replay(list, s);  // 不应崩溃
    NQ_EXPECT_EQ(s.at(4, 4).a, 255);
  }
  // 头部 size 非法
  {
    std::vector<std::byte> ops(sizeof(dl::Header));
    dl::Header h{1, static_cast<uint16_t>(dl::Op::kFillRect), 0};
    std::memcpy(ops.data(), &h, sizeof(h));
    dl::DisplayList list;
    list.ops = ops;
    Surface s(8, 8);
    raster::replay(list, s);  // 直接 break
    NQ_EXPECT(s.valid());
  }
  // 路径点越界
  {
    dl::Builder b;
    b.fill_rect(RectF{0, 0, 8, 8}, premul(255, 255, 255));
    dl::DisplayList list = b.finish();
    dl::FillPathOp bad{0, 9999, premul(255, 0, 0)};
    const size_t offset = list.ops.size();
    list.ops.resize(offset + sizeof(dl::Header) + sizeof(bad));
    dl::Header h{static_cast<uint32_t>(sizeof(dl::Header) + sizeof(bad)),
                 static_cast<uint16_t>(dl::Op::kFillPath), 0};
    std::memcpy(list.ops.data() + offset, &h, sizeof(h));
    std::memcpy(list.ops.data() + offset + sizeof(h), &bad, sizeof(bad));
    Surface s(8, 8);
    raster::replay(list, s);  // 越界 op 被跳过，前面的矩形仍然绘制
    NQ_EXPECT_EQ(s.at(4, 4).a, 255);
  }
  // 空列表 / 非法 surface
  {
    dl::DisplayList empty;
    Surface s(4, 4);
    raster::replay(empty, s);
    Surface invalid;
    dl::Builder b;
    b.fill_rect(RectF{0, 0, 4, 4}, premul(255, 255, 255));
    raster::replay(b.finish(), invalid);
    NQ_EXPECT(!invalid.valid());
  }
}

NQ_TEST(polylines_degenerate_inputs) {
  Surface s(16, 16);
  s.fill(premul(0, 0, 0));
  raster::fill_polylines(s, {}, {}, premul(255, 255, 255), s.bounds());
  const std::vector<Vec2> one{{1, 1}};
  raster::fill_polylines(s, one, {1}, premul(255, 255, 255), s.bounds());
  NQ_EXPECT_EQ(s.at(4, 4).a, 255);
  // 完全在裁剪区外
  const std::vector<Vec2> outside{{100, 100}, {104, 100}, {104, 104}};
  raster::fill_polylines(s, outside, {3}, premul(255, 255, 255), s.bounds());
}

NQ_TEST(raster_rejects_zero_alpha_and_clipped_shapes) {
  Surface s(8, 8);
  s.fill(premul(0, 0, 0));
  raster::fill_rect_analytic(s, RectF{0, 0, 8, 8}, premul(255, 255, 255, 0), s.bounds());
  NQ_EXPECT_EQ(s.at(4, 4).a, 255);
  raster::fill_rect_analytic(s, RectF{100, 100, 4, 4}, premul(255, 255, 255), s.bounds());
  dl::PathBuilder p;
  p.add_circle({200, 200}, 10.0f);
  raster::fill_polylines(s, p.points(), p.contour_ends(), premul(255, 255, 255), s.bounds());
  NQ_EXPECT_EQ(s.at(4, 4).a, 255);
}

NQ_TEST(replay_gradient_and_path_ops) {
  Surface s(16, 16);
  s.fill(premul(0, 0, 0, 0));
  dl::Builder b;
  b.fill_rect_gradient(RectF{0, 0, 16, 8}, premul(0, 0, 0), premul(0, 0, 255), false);
  dl::PathBuilder p;
  p.move_to({2, 10});
  p.line_to({13, 10});
  p.line_to({13, 15});
  p.close();
  b.fill_path(p, premul(255, 0, 0));
  const dl::DisplayList list = b.finish();
  raster::replay(list, s);
  NQ_EXPECT(s.at(8, 6).b > 100);     // 渐变下半段变蓝
  NQ_EXPECT_EQ(s.at(11, 12).r, 255);  // 路径填充（避开斜边抗锯齿）
  NQ_EXPECT_EQ(s.at(8, 9).a, 0);     // 路径外
}

NQ_TEST(gradient_rejects_fully_clipped_rect) {
  Surface s(8, 8);
  s.fill(premul(0, 0, 0));
  raster::fill_rect_gradient(s, RectF{0, 0, 8, 8}, premul(255, 0, 0), premul(255, 0, 0), true,
                             RectF{});
  NQ_EXPECT_EQ(s.at(4, 4).r, 0);
}

NQ_TEST(coverage_tolerates_invalid_contour_ends) {
  const std::vector<Vec2> pts = {{0, 0}, {4, 0}, {4, 4}, {0, 4}, {0, 0}};
  const std::vector<uint32_t> bad_ends{5, 999};  // 合法轮廓 + 越界轮廓
  std::vector<uint8_t> mask(25, 0);
  raster::compute_coverage(pts, bad_ends, 0, 0, 5, 5, mask.data());
  NQ_EXPECT_EQ(mask[2 * 5 + 2], 255);  // 合法轮廓仍然被填充
  std::vector<uint8_t> tiny(1, 0);
  raster::compute_coverage({}, {}, 0, 0, 1, 1, tiny.data());  // 空输入安全
  raster::compute_coverage(pts, bad_ends, 0, 0, 0, 0, tiny.data());  // 零尺寸安全
}

// ------------------------------------------------------------------ text

NQ_TEST(bitmap_font_metrics_and_fallback) {
  NQ_EXPECT_EQ(text::measure_unscaled(""), 0);
  NQ_EXPECT_EQ(text::measure_unscaled("A"), text::kGlyphW);
  NQ_EXPECT_EQ(text::measure_unscaled("AB"), text::kGlyphW * 2 + text::kTracking);
  NQ_EXPECT_EQ(text::measure("AB", 2), (text::kGlyphW * 2 + text::kTracking) * 2);
  NQ_EXPECT_EQ(text::measure("A", 0), text::kGlyphW);  // scale 夹紧到 1

  Surface s(32, 16);
  s.fill(premul(0, 0, 0));
  // 小写映射到大写；未知字符只前进不绘制
  const int end = text::draw_text(s, 0, 0, 1, premul(255, 255, 255), "a@b");
  NQ_EXPECT_EQ(end, (text::kGlyphW + text::kTracking) * 3);
  bool has_ink = false;
  for (int y = 0; y < 16 && !has_ink; ++y) {
    for (int x = 0; x < 32; ++x) {
      if (s.at(x, y).r > 0) {
        has_ink = true;
        break;
      }
    }
  }
  NQ_EXPECT(has_ink);
}

NQ_TEST(layout_unit_arithmetic_coverage) {
  const LayoutUnit one = LayoutUnit::from_int(1);
  NQ_EXPECT_EQ((one * 3).raw(), 3 * LayoutUnit::kOne);
  NQ_EXPECT_EQ((one - one).raw(), 0);
  NQ_EXPECT_EQ((-one).raw(), -LayoutUnit::kOne);
  NQ_EXPECT_EQ((LayoutUnit::from_int(10) / 0).raw(), LayoutUnit::from_int(10).raw());
  NQ_EXPECT_EQ((LayoutUnit::from_int(-10) / 4).round_to_int(), -3);  // 远离零
  NQ_EXPECT(one > LayoutUnit::from_int(0));
  NQ_EXPECT(one >= one);
  NQ_EXPECT(one != LayoutUnit::from_int(2));
  LayoutUnit acc = LayoutUnit::from_int(1);
  acc += one;
  acc -= one;
  NQ_EXPECT_EQ(acc.to_float(), 1.0f);
  NQ_EXPECT_EQ(LayoutUnit::from_float(1e12f).raw(), LayoutUnit::kMax);
  NQ_EXPECT_EQ(LayoutUnit::from_float(-1e12f).raw(), LayoutUnit::kMin);
}

NQ_TEST(geom_helpers) {
  const RectF a{0, 0, 10, 10};
  const RectF b{5, 5, 10, 10};
  NQ_EXPECT(intersect(a, b).contains(5.5f, 5.5f));
  NQ_EXPECT(intersect(a, RectF{20, 20, 5, 5}).is_empty());
  NQ_EXPECT_EQ(unite(RectF{}, a).w, 10.0f);
  NQ_EXPECT_EQ(unite(a, RectF{}).h, 10.0f);
  NQ_EXPECT_EQ(unite(a, b).right(), 15.0f);
  NQ_EXPECT(!a.contains(10.0f, 5.0f));  // 右边界开区间
  NQ_EXPECT_EQ(clampi(9, 0, 5), 5);
  NQ_EXPECT_EQ(clampi(-9, 0, 5), 0);
  NQ_EXPECT_NEAR(clampf(9.0f, 0.0f, 5.0f), 5.0f, 1e-6);
  NQ_EXPECT(mix64(1) != mix64(2));
  NQ_EXPECT_EQ(hash_bytes("abc", 3), hash_bytes("abc", 3));
}

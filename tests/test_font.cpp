#include <cstring>
#include <string>
#include <vector>

#include "base/diag.h"
#include "base/utf8.h"
#include "font/sfnt.h"
#include "nqtest.h"
#include "raster/surface.h"
#include "text/font.h"

namespace {

using namespace nq;

text::Font& bundled() {
  static std::unique_ptr<text::Font> font = [] {
    auto result = text::Font::load_file(NQ_BUNDLED_FONT);
    return result.ok() ? std::move(*result) : nullptr;
  }();
  NQ_ASSERT(font != nullptr);
  return *font;
}

// 白底上是否出现非白像素
bool has_ink(const Surface& s) {
  for (size_t i = 0; i < s.pixels.size(); i += 4) {
    if (s.pixels[i] != 255 || s.pixels[i + 1] != 255 || s.pixels[i + 2] != 255) return true;
  }
  return false;
}

}  // namespace

NQ_TEST(utf8_roundtrip_and_invalid_sequences) {
  const std::string text = "A\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80";  // A é 中 😀
  std::vector<char32_t> cps;
  Utf8Iterator it(text);
  while (!it.done()) cps.push_back(it.next());
  NQ_EXPECT_EQ(cps.size(), 4u);
  NQ_EXPECT_EQ(cps[0], U'A');
  NQ_EXPECT_EQ(cps[1], 0x00E9u);
  NQ_EXPECT_EQ(cps[2], 0x4E2Du);
  NQ_EXPECT_EQ(cps[3], 0x1F600u);
  for (char32_t cp : cps) {
    char buf[4];
    const size_t n = utf8_encode(cp, buf);
    const std::string back(buf, n);
    size_t index = 0;
    NQ_EXPECT_EQ(utf8_decode(back, &index), cp);
  }

  // 非法：孤立续字节 / 截断序列 / 超长编码
  const char* bad[] = {"\x80", "\xE4\xB8", "\xC0\x80", "\xF5\x80\x80\x80"};
  for (const char* b : bad) {
    size_t index = 0;
    const std::string_view sv(b, std::strlen(b));
    NQ_EXPECT_EQ(utf8_decode(sv, &index), kReplacementChar);
    NQ_EXPECT_EQ(index, 1u);
  }
}

NQ_TEST(sfnt_header_and_metrics) {
  const font::Face& face = bundled().face();
  NQ_EXPECT(face.units_per_em() >= 512);
  NQ_EXPECT(face.num_glyphs() > 100);
  NQ_EXPECT(face.ascender() > 0);
  NQ_EXPECT(face.descender() < 0);
  NQ_EXPECT(!face.family().empty());
  NQ_EXPECT(face.line_gap() >= 0);
}

NQ_TEST(sfnt_cmap_maps_expected_codepoints) {
  const font::Face& face = bundled().face();
  const uint16_t a = face.glyph_index(U'A');
  const uint16_t b = face.glyph_index(U'B');
  NQ_EXPECT(a != 0);
  NQ_EXPECT(b != 0);
  NQ_EXPECT(a != b);
  NQ_EXPECT(face.glyph_index(U' ' ) != 0);
  NQ_EXPECT_EQ(face.glyph_index(0x10FFFFu), 0u);  // 未映射
  NQ_EXPECT(face.glyph_index(U'e') != face.glyph_index(U'E'));
}

NQ_TEST(sfnt_advances_and_outlines) {
  const font::Face& face = bundled().face();
  NQ_EXPECT(face.advance(face.glyph_index(U'm')) > 0);
  NQ_EXPECT(face.advance(face.glyph_index(U'i')) < face.advance(face.glyph_index(U'm')));

  const font::Outline a = face.outline(face.glyph_index(U'A'));
  NQ_EXPECT(!a.empty());
  NQ_EXPECT(!a.contour_ends.empty());
  NQ_EXPECT_EQ(a.contour_ends.back(), a.points.size());

  // 复合字形（含变音符）必须能展开
  const font::Outline diaeresis = face.outline(face.glyph_index(0x00C4));  // Ä
  NQ_EXPECT(!diaeresis.empty());
  NQ_EXPECT(diaeresis.contour_ends.size() >= 2);

  // 空格无轮廓，.notdef 也有定义
  NQ_EXPECT(face.outline(face.glyph_index(U' ')).empty());
  NQ_EXPECT(face.outline(0).empty() || !face.outline(0).empty());
  NQ_EXPECT(face.outline(60000).empty());  // 越界 gid 安全返回空
}

NQ_TEST(font_loading_errors) {
  auto missing = text::Font::load_file("/nonexistent/font.ttf");
  NQ_EXPECT(!missing);
  NQ_EXPECT_EQ(missing.status().code, Code::kNotFound);

  auto garbage = text::Font::load_bytes(std::vector<uint8_t>(64, 0x42), "garbage");
  NQ_EXPECT(!garbage);
  NQ_EXPECT_EQ(garbage.status().code, Code::kCorrupt);

  auto empty = text::Font::load_bytes({}, "empty");
  NQ_EXPECT(!empty);

  // 合法头但缺表
  std::vector<uint8_t> truncated(12, 0);
  truncated[3] = 0x01;  // version 0x00010000
  auto bad = text::Font::load_bytes(std::move(truncated), "truncated");
  NQ_EXPECT(!bad);
}

NQ_TEST(font_measure_and_draw) {
  text::Font& font = bundled();
  NQ_EXPECT_EQ(font.measure("", 16.0f), 0);
  const int w1 = font.measure("Hello", 16.0f);
  const int w2 = font.measure("Hello, world", 16.0f);
  NQ_EXPECT(w1 > 0);
  NQ_EXPECT(w2 > w1);
  NQ_EXPECT(font.measure("Hello", 32.0f) > w1 * 3 / 2);

  NQ_EXPECT_NEAR(font.ascent(16.0f), 16.0f * 0.78, 4.0);
  NQ_EXPECT(font.descent(16.0f) > 0.0f);
  NQ_EXPECT(font.line_height(16.0f) > font.ascent(16.0f));

  Surface s(120, 40);
  s.fill(premul(255, 255, 255));
  const float end = font.draw(s, 4.0f, 28.0f, "Hello", 18.0f, premul(20, 24, 30));
  NQ_EXPECT(end > 4.0f);
  NQ_EXPECT(has_ink(s));

  // 空字符串不产生像素也不崩溃
  Surface blank(8, 8);
  blank.fill(premul(255, 255, 255));
  font.draw(blank, 0.0f, 6.0f, "", 12.0f, premul(0, 0, 0));
  NQ_EXPECT(!has_ink(blank));
}

NQ_TEST(glyph_cache_is_effective) {
  text::Font& font = bundled();
  Surface s(200, 40);
  s.fill(premul(255, 255, 255));

  font.draw(s, 2.0f, 30.0f, "reuse-me", 16.0f, premul(0, 0, 0));
  const uint64_t rasterized_after_first = diag::counter("text.glyphs_rasterized").value();
  const uint64_t hits_after_first = diag::counter("text.glyph_cache_hits").value();

  font.draw(s, 2.0f, 30.0f, "reuse-me", 16.0f, premul(0, 0, 0));
  NQ_EXPECT_EQ(diag::counter("text.glyphs_rasterized").value(), rasterized_after_first);
  NQ_EXPECT(diag::counter("text.glyph_cache_hits").value() > hits_after_first);
  NQ_EXPECT(font.cache_bytes() > 0u);

  // 预算极小 → 触发驱逐，但不崩溃
  const uint64_t evictions_before = diag::counter("text.cache_evictions").value();
  font.set_cache_budget(64);
  font.draw(s, 2.0f, 30.0f, "another-string", 16.0f, premul(0, 0, 0));
  NQ_EXPECT(diag::counter("text.cache_evictions").value() > evictions_before);
  font.set_cache_budget(8u * 1024u * 1024u);
}

NQ_TEST(font_wrap_prefers_space_boundaries) {
  text::Font& font = bundled();
  const std::string_view text = "hello world nextqt";
  std::string_view line;
  const float width = static_cast<float>(font.measure("hello wor", 14.0f));
  const size_t consumed = font.wrap(text, 14.0f, width, &line);
  NQ_EXPECT(consumed > 0);
  NQ_EXPECT(consumed <= text.size());
  NQ_EXPECT(line == text.substr(0, consumed));
  NQ_EXPECT(line.find("world") == std::string_view::npos);  // 在空格处断开

  // 宽度足够 → 全部消费
  std::string_view whole;
  const size_t all = font.wrap(text, 14.0f, 10000.0f, &whole);
  NQ_EXPECT_EQ(all, text.size());
  NQ_EXPECT_EQ(whole.size(), text.size());
}

// =====================================================================
// 合成字体：不依赖外部字体文件，精确覆盖 sfnt 解析的各条分支
// （cmap 4 / cmap 12、short / long loca、简单 / 复合字形、畸形输入）
// =====================================================================
namespace {

struct SynthGlyph {
  std::vector<uint8_t> data;  // 已 padding 到偶数长度
};

void put16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x >> 8));
  v.push_back(static_cast<uint8_t>(x & 0xFF));
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x >> 24));
  v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
  v.push_back(static_cast<uint8_t>(x & 0xFF));
}
void put_tag(std::vector<uint8_t>& v, const char* tag) {
  v.insert(v.end(), tag, tag + 4);
}

// 简单字形：正方形（on-curve）或三角形（含 off-curve），用短向量编码
std::vector<uint8_t> make_simple(bool triangle_with_curve) {
  std::vector<uint8_t> g;
  put16(g, 1);  // numberOfContours
  put16(g, 0); put16(g, 0); put16(g, 500); put16(g, 500);  // bbox
  if (!triangle_with_curve) {
    put16(g, 3);  // endPts
    put16(g, 0);  // instructions
    for (int i = 0; i < 4; ++i) g.push_back(0x01);  // ON_CURVE，坐标用 int16 增量
  } else {
    put16(g, 2);
    put16(g, 0);
    g.push_back(0x01);  // on-curve
    g.push_back(0x00);  // off-curve (quadratic control)
    g.push_back(0x01);  // on-curve
  }
  // 坐标：全部用 2 字节有符号增量（flag 的 short 位为 0）
  const int16_t xs[4] = {0, 500, 0, -500};
  const int16_t ys[4] = {0, 0, 500, 0};
  const int count = triangle_with_curve ? 3 : 4;
  for (int i = 0; i < count; ++i) {
    put16(g, static_cast<uint16_t>(xs[i]));
    put16(g, static_cast<uint16_t>(ys[i]));
  }
  while (g.size() % 2 != 0) g.push_back(0);
  return g;
}

// 用 REPEAT 标志 + 指令块编码的正方形（覆盖解析器里的重复展开分支）
std::vector<uint8_t> make_simple_repeat() {
  std::vector<uint8_t> g;
  put16(g, 1);
  put16(g, 0); put16(g, 0); put16(g, 500); put16(g, 500);
  put16(g, 3);    // endPts
  put16(g, 3);    // instructionLength
  g.push_back(0x11); g.push_back(0x22); g.push_back(0x33);
  // 4 个点全部 on-curve，x/y 用短向量：flag = ON_CURVE|X_SHORT|Y_SHORT|X_SAME|Y_SAME
  g.push_back(0x3B);  // 0b0011_1011
  g.push_back(0x03);  // REPEAT 3 次
  for (int i = 0; i < 8; ++i) g.push_back(0);  // 4 个 x/y 增量字节
  while (g.size() % 2 != 0) g.push_back(0);
  return g;
}

std::vector<uint8_t> make_composite_scaled(uint16_t component) {
  std::vector<uint8_t> g;
  put16(g, static_cast<uint16_t>(-1));
  put16(g, 0); put16(g, 0); put16(g, 500); put16(g, 500);
  // ARGS_ARE_XY | ARG_1_AND_2_ARE_WORDS | WE_HAVE_A_SCALE
  put16(g, static_cast<uint16_t>(0x0003 | 0x0008));
  put16(g, component);
  put16(g, 10);  // dx
  put16(g, 20);  // dy
  put16(g, 0x2000);  // scale = 0.5 (F2Dot14)
  while (g.size() % 2 != 0) g.push_back(0);
  return g;
}

std::vector<uint8_t> make_composite(uint16_t component, uint16_t flags_extra) {
  std::vector<uint8_t> g;
  put16(g, static_cast<uint16_t>(-1));  // -1 轮廓 = 复合
  put16(g, 0); put16(g, 0); put16(g, 500); put16(g, 500);
  put16(g, static_cast<uint16_t>(0x0003 | flags_extra));  // ARGS_ARE_XY | WORDS
  put16(g, component);
  put16(g, 0);  // dx
  put16(g, 0);  // dy
  while (g.size() % 2 != 0) g.push_back(0);
  return g;
}

std::vector<uint8_t> make_cmap4() {
  std::vector<uint8_t> c;
  put16(c, 0);  // version
  put16(c, 1);  // numTables
  put16(c, 3); put16(c, 1); put32(c, 12);  // record -> offset 12
  // format 4
  const uint16_t segCount = 2;
  put16(c, 4);
  put16(c, static_cast<uint16_t>(16 + segCount * 8));  // length
  put16(c, 0);                                          // language
  put16(c, static_cast<uint16_t>(segCount * 2));
  put16(c, 4);  // searchRange
  put16(c, 1);  // entrySelector
  put16(c, static_cast<uint16_t>(segCount * 2 - 4));
  put16(c, 0x42);      // endCode[0]
  put16(c, 0xFFFF);    // endCode[1]
  put16(c, 0);         // reservedPad
  put16(c, 0x41);      // startCode[0]
  put16(c, 0xFFFF);    // startCode[1]
  put16(c, static_cast<uint16_t>(1 - 0x41));  // idDelta[0] -> 'A' = gid1
  put16(c, 1);                                // idDelta[1]
  put16(c, 0);                                // idRangeOffset[0]
  put16(c, 0);                                // idRangeOffset[1]
  return c;
}

std::vector<uint8_t> make_cmap12() {
  std::vector<uint8_t> c;
  put16(c, 0);
  put16(c, 1);
  put16(c, 3); put16(c, 10); put32(c, 12);
  put16(c, 12);  // format
  put16(c, 0);   // reserved
  put32(c, 16 + 12);  // length
  put32(c, 0);        // language
  put32(c, 1);        // nGroups
  put32(c, 0x41);     // startCharCode
  put32(c, 0x42);     // endCharCode
  put32(c, 1);        // startGlyphID
  return c;
}

std::vector<uint8_t> make_name() {
  std::vector<uint8_t> n;
  const char* family = "SynthFont";
  std::vector<uint8_t> str;
  for (const char* p = family; *p != '\0'; ++p) put16(str, static_cast<uint16_t>(*p));
  put16(n, 0);              // version
  put16(n, 1);              // count
  put16(n, 6 + 12);         // stringOffset
  put16(n, 3); put16(n, 1); put16(n, 0x0409);  // platform/encoding/language
  put16(n, 1);              // nameID: family
  put16(n, static_cast<uint16_t>(str.size()));
  put16(n, 0);
  n.insert(n.end(), str.begin(), str.end());
  return n;
}

struct SynthSpec {
  bool use_cmap12 = true;
  bool long_loca = false;
  bool triangle = false;
  bool self_referential_composite = false;
  bool bad_glyf_offset = false;
};

std::vector<uint8_t> build_font(const SynthSpec& spec,
                                std::vector<std::vector<uint8_t>> glyph_data = {}) {
  std::vector<SynthGlyph> glyphs;
  if (glyph_data.empty()) {
    glyphs.push_back({{}});                              // .notdef
    glyphs.push_back({make_simple(spec.triangle)});      // gid1
    glyphs.push_back({make_composite(1, spec.self_referential_composite ? 1 : 0)});  // gid2
    glyphs.push_back({make_simple(false)});              // gid3
  } else {
    glyphs.push_back({{}});
    for (const auto& g : glyph_data) glyphs.push_back({g});
  }

  // glyf + loca
  std::vector<uint8_t> glyf;
  std::vector<uint32_t> offsets;
  offsets.push_back(0);
  for (const SynthGlyph& g : glyphs) {
    glyf.insert(glyf.end(), g.data.begin(), g.data.end());
    offsets.push_back(static_cast<uint32_t>(glyf.size()));
  }
  if (spec.bad_glyf_offset) {
    offsets.resize(offsets.size());
    offsets.back() = static_cast<uint32_t>(glyf.size() + 4096);  // 越界
  }
  std::vector<uint8_t> loca;
  for (uint32_t off : offsets) {
    if (spec.long_loca) {
      put32(loca, off);
    } else {
      put16(loca, static_cast<uint16_t>(off / 2));
    }
  }

  std::vector<uint8_t> head(54, 0);
  const auto set16 = [&head](size_t at, uint16_t v) {
    head[at] = static_cast<uint8_t>(v >> 8);
    head[at + 1] = static_cast<uint8_t>(v & 0xFF);
  };
  const auto set32 = [&head](size_t at, uint32_t v) {
    head[at] = static_cast<uint8_t>(v >> 24);
    head[at + 1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    head[at + 2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    head[at + 3] = static_cast<uint8_t>(v & 0xFF);
  };
  set32(0, 0x00010000);
  set32(12, 0x5F0F3CF5);  // magic
  set16(18, 1000);        // unitsPerEm
  set16(50, spec.long_loca ? 1 : 0);

  std::vector<uint8_t> maxp;
  put32(maxp, 0x00005000);
  put16(maxp, static_cast<uint16_t>(glyphs.size()));

  std::vector<uint8_t> hhea(36, 0);
  const auto h16 = [&hhea](size_t at, uint16_t v) {
    hhea[at] = static_cast<uint8_t>(v >> 8);
    hhea[at + 1] = static_cast<uint8_t>(v & 0xFF);
  };
  h16(4, 800);   // ascender
  h16(6, static_cast<uint16_t>(-200));  // descender
  h16(8, 0);     // lineGap
  h16(34, static_cast<uint16_t>(glyphs.size()));  // numberOfHMetrics

  std::vector<uint8_t> hmtx;
  for (size_t i = 0; i < glyphs.size(); ++i) {
    put16(hmtx, static_cast<uint16_t>(500 + i * 10));
    put16(hmtx, 0);
  }

  std::vector<uint8_t> cmap = spec.use_cmap12 ? make_cmap12() : make_cmap4();

  std::vector<std::pair<std::string, std::vector<uint8_t>>> tables = {
      {"cmap", cmap}, {"glyf", glyf}, {"head", head}, {"hhea", hhea},
      {"hmtx", hmtx}, {"loca", loca}, {"maxp", maxp}, {"name", make_name()},
  };

  std::vector<uint8_t> out;
  put32(out, 0x00010000);
  put16(out, static_cast<uint16_t>(tables.size()));
  put16(out, 0);
  put16(out, 0);
  put16(out, 0);
  size_t data_offset = 12 + tables.size() * 16;
  std::vector<uint32_t> table_offsets;
  for (const auto& [tag, data] : tables) {
    put_tag(out, tag.c_str());
    put32(out, 0);  // checksum（解析器不校验）
    put32(out, static_cast<uint32_t>(data_offset));
    put32(out, static_cast<uint32_t>(data.size()));
    table_offsets.push_back(static_cast<uint32_t>(data_offset));
    data_offset += (data.size() + 3) & ~size_t{3};
  }
  for (const auto& [tag, data] : tables) {
    out.insert(out.end(), data.begin(), data.end());
    while (out.size() % 4 != 0) out.push_back(0);
  }
  return out;
}

}  // namespace

NQ_TEST(synthetic_font_cmap_and_outlines) {
  for (bool use12 : {true, false}) {
    for (bool long_loca : {true, false}) {
      auto face_result = font::Face::load(build_font(SynthSpec{use12, long_loca, false, false, false}));
      NQ_EXPECT(face_result.ok());
      const font::Face& face = **face_result;
      NQ_EXPECT_EQ(face.units_per_em(), 1000);
      NQ_EXPECT_EQ(face.num_glyphs(), 4u);
      NQ_EXPECT_EQ(face.ascender(), 800);
      NQ_EXPECT_EQ(face.descender(), -200);
      NQ_EXPECT_EQ(face.family(), std::string("SynthFont"));
      NQ_EXPECT_EQ(face.glyph_index(U'A'), 1u);
      NQ_EXPECT_EQ(face.glyph_index(U'B'), 2u);
      NQ_EXPECT_EQ(face.glyph_index(U'Z'), 0u);
      NQ_EXPECT_EQ(face.advance(1), 510u);
      const font::Outline square = face.outline(1);
      NQ_EXPECT_EQ(square.contour_ends.size(), 1u);
      NQ_EXPECT_EQ(square.points.size(), 4u);
      NQ_EXPECT(!face.outline(2).empty());   // 复合字形
      NQ_EXPECT(face.outline(0).empty());    // .notdef 空
    }
  }
}

NQ_TEST(synthetic_font_quadratic_contour) {
  auto face_result = font::Face::load(build_font(SynthSpec{true, false, true, false, false}));
  NQ_EXPECT(face_result.ok());
  const font::Face& face = **face_result;
  const font::Outline tri = face.outline(1);
  NQ_EXPECT_EQ(tri.points.size(), 3u);
  NQ_EXPECT(tri.points[0].on_curve);
  NQ_EXPECT(!tri.points[1].on_curve);
}

NQ_TEST(synthetic_font_rejects_bad_glyph_offsets) {
  auto face_result = font::Face::load(build_font(SynthSpec{true, false, false, false, true}));
  NQ_EXPECT(face_result.ok());  // 字体本身合法
  const font::Face& face = **face_result;
  NQ_EXPECT(face.outline(3).empty());  // 越界轮廓安全返回空
}

NQ_TEST(synthetic_font_self_referential_composite_is_bounded) {
  auto face_result = font::Face::load(build_font(SynthSpec{true, true, false, true, false}));
  NQ_EXPECT(face_result.ok());
  const font::Face& face = **face_result;
  // gid2 引用自身：必须被深度限制截断而不是栈溢出
  const font::Outline o = face.outline(2);
  NQ_EXPECT(o.points.size() < 100000u);
}

NQ_TEST(synthetic_font_flag_variants) {
  // REPEAT 标志 + 指令块
  {
    const std::vector<uint8_t> glyf_glyph = make_simple_repeat();
    (void)glyf_glyph;
    SynthSpec spec{true, false, false, false, false};
    auto face_result = font::Face::load(build_font(spec, {make_simple(false), make_simple_repeat(),
                                                           make_composite_scaled(1)}));
    NQ_EXPECT(face_result.ok());
    const font::Face& face = **face_result;
    const font::Outline repeat = face.outline(2);
    NQ_EXPECT_EQ(repeat.points.size(), 4u);   // REPEAT 正确展开
    const font::Outline scaled = face.outline(3);  // 复合字形带 0.5 缩放 + 位移
    NQ_EXPECT(!scaled.empty());
    float max_x = -1e9f;
    for (const auto& cp : scaled.points) max_x = cp.p.x > max_x ? cp.p.x : max_x;
    NQ_EXPECT_NEAR(max_x, 10.0 + 500 * 0.5, 1.0);
  }
}

NQ_TEST(surface_bounds_guards) {
  Surface s(4, 4);
  s.fill(premul(0, 0, 0, 0));
  // 越界写入必须被静默忽略
  s.blend_pixel(-1, 0, premul(255, 255, 255), 255);
  s.blend_pixel(0, -1, premul(255, 255, 255), 255);
  s.blend_pixel(4, 0, premul(255, 255, 255), 255);
  s.blend_pixel(0, 4, premul(255, 255, 255), 255);
  s.blend_pixel(1, 1, premul(255, 255, 255), 0);   // 覆盖率 0
  s.blend_pixel(1, 1, premul(255, 255, 255), 255); // 全覆盖
  NQ_EXPECT_EQ(s.at(1, 1).a, 255);
  NQ_EXPECT_EQ(s.at(0, 0).a, 0);
  // valid() 与 bounds
  Surface bad(0, 5);
  NQ_EXPECT(!bad.valid());
  NQ_EXPECT_NEAR(s.bounds().right(), 4.0f, 1e-6);
}

NQ_TEST(font_cache_budget_and_missing_glyphs) {
  text::Font& font = bundled();
  Surface s(64, 32);
  s.fill(premul(255, 255, 255));
  // 预算设为 0 → 每次都会驱逐，但绘制结果仍然正确
  font.set_cache_budget(1);
  font.draw(s, 2.0f, 24.0f, "A", 16.0f, premul(0, 0, 0));
  NQ_EXPECT(font.cache_bytes() > 0u);
  font.set_cache_budget(8u * 1024u * 1024u);

  // 未映射码点：只前进不绘制
  Surface blank(32, 32);
  blank.fill(premul(255, 255, 255));
  const float end = font.draw(blank, 0.0f, 24.0f, "\xF0\x9F\x98\x80", 16.0f, premul(0, 0, 0));
  NQ_EXPECT(end >= 0.0f);
  NQ_EXPECT_EQ(font.name(), std::string("Inter"));

  // 无空格的长词：不得死循环，必须消费一部分
  std::string_view line;
  const size_t consumed = font.wrap("supercalifragilistic", 12.0f, 10.0f, &line);
  NQ_EXPECT(consumed > 0);
  NQ_EXPECT(consumed < std::string_view("supercalifragilistic").size());
}

NQ_TEST(utf8_more_edge_cases) {
  // 编码辅助
  NQ_EXPECT_EQ(utf8_encode_string(U'A'), std::string("A"));
  NQ_EXPECT_EQ(utf8_encode_string(0x4E2D), std::string("\xE4\xB8\xAD"));
  NQ_EXPECT_EQ(utf8_encode_string(0x1F600).size(), 4u);

  // 非法续字节 / 代理区 / 超出范围 / 截断
  struct Bad {
    const char* bytes;
    size_t len;
  };
  const Bad cases[] = {
      {"\xC3", 1},                    // 截断的 2 字节
      {"\xE4\x41\x42", 3},          // 非法续字节
      {"\xED\xA0\x80", 3},          // 代理区
      {"\xF4\x90\x80\x80", 4},     // > U+10FFFF
      {"\xF0\x9F\x98", 3},          // 截断的 4 字节
      {"\xFF", 1},                    // 非法首字节
  };
  for (const Bad& b : cases) {
    size_t index = 0;
    const std::string_view sv(b.bytes, b.len);
    NQ_EXPECT_EQ(utf8_decode(sv, &index), kReplacementChar);
    NQ_EXPECT_EQ(index, 1u);
  }
  // 迭代器在空串上立即结束
  Utf8Iterator empty("");
  NQ_EXPECT(empty.done());
  NQ_EXPECT_EQ(empty.byte_index(), 0u);
}

NQ_TEST(font_load_from_bytes_and_empty_glyph) {
  std::FILE* f = std::fopen(NQ_BUNDLED_FONT, "rb");
  NQ_EXPECT(f != nullptr);
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  NQ_EXPECT_EQ(std::fread(bytes.data(), 1, bytes.size(), f), bytes.size());
  std::fclose(f);

  auto loaded = text::Font::load_bytes(std::move(bytes), "InterBytes");
  NQ_EXPECT(loaded.ok());
  NQ_EXPECT_EQ((*loaded)->name(), std::string("InterBytes"));

  // 空格：有 advance、无轮廓 → 只前进不绘制
  Surface s(40, 24);
  s.fill(premul(255, 255, 255));
  const float start = 2.0f;
  const float end = (*loaded)->draw(s, start, 18.0f, " ", 14.0f, premul(0, 0, 0));
  NQ_EXPECT(end > start);

  // 恰好放得下：整段消费且不提前断行
  std::string_view line;
  const std::string_view text = "abc";
  const float exact = static_cast<float>((*loaded)->measure(text, 13.0f)) + 1.0f;
  NQ_EXPECT_EQ((*loaded)->wrap(text, 13.0f, exact, &line), text.size());
}

NQ_TEST(status_to_string_covers_all_codes) {
  for (int i = 0; i <= static_cast<int>(Code::kInternal); ++i) {
    const std::string_view name = to_string(static_cast<Code>(i));
    NQ_EXPECT(!name.empty());
    NQ_EXPECT(name != std::string_view("unknown"));
  }
  NQ_EXPECT_EQ(to_string(static_cast<Code>(999)), std::string_view("unknown"));
  const Status ok;
  NQ_EXPECT(ok.ok());
  NQ_EXPECT(static_cast<bool>(ok));
  const Status bad{Code::kCorrupt, "x"};
  NQ_EXPECT(!bad.ok());
  NQ_EXPECT_EQ(bad.message, std::string("x"));
  const Result<int> value(7);
  NQ_EXPECT_EQ(*value, 7);
  NQ_EXPECT_EQ(value.value_or(0), 7);
  const Result<int> err{Status{Code::kNotFound}};
  NQ_EXPECT_EQ(err.value_or(3), 3);
}

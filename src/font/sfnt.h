// NextQt · TrueType/OpenType(sfnt) 解析：cmap / hmtx / glyf / head / hhea / maxp
//
// 只依赖 base。不依赖 FreeType/HarfBuzz。当前支持：
//   - sfnt 0x00010000 与 'true'（glyf 轮廓）
//   - cmap 格式 12（UCS-4）优先，回退格式 4（BMP）
//   - glyf 简单字形 + 复合字形（含二维变换、递归深度限制）
//   - hmtx 前进宽度、hhea 垂直度量
// 后续：CFF/CFF2、变体（gvar/HVAR）、COLR、GSUB/GPOS（见 docs/04 §7–8）
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base/geom.h"
#include "base/status.h"

namespace nq::font {

// 字形轮廓：按轮廓分组的二次贝塞尔段（字体单位，y 轴向上）
struct ContourPoint {
  Vec2 p;
  bool on_curve = true;
};

struct Outline {
  std::vector<ContourPoint> points;
  std::vector<uint32_t> contour_ends;  // 每个轮廓的结束索引（开区间）
  [[nodiscard]] bool empty() const noexcept { return points.empty(); }
};

class Face {
 public:
  // 失败时返回 kCorrupt / kUnsupported / kLimitExceeded
  static Result<std::unique_ptr<Face>> load(std::vector<uint8_t> bytes);
  static Result<std::unique_ptr<Face>> load_file(const char* path);

  [[nodiscard]] uint16_t units_per_em() const noexcept { return units_per_em_; }
  [[nodiscard]] uint16_t num_glyphs() const noexcept { return num_glyphs_; }
  [[nodiscard]] int16_t ascender() const noexcept { return ascender_; }
  [[nodiscard]] int16_t descender() const noexcept { return descender_; }
  [[nodiscard]] int16_t line_gap() const noexcept { return line_gap_; }
  [[nodiscard]] const std::string& family() const noexcept { return family_; }

  // cmap 查询；未映射返回 0（.notdef）
  [[nodiscard]] uint16_t glyph_index(char32_t cp) const noexcept;
  // 前进宽度（字体单位）；越界返回 0
  [[nodiscard]] uint16_t advance(uint16_t gid) const noexcept;
  // 轮廓（字体单位）；gid 非法或轮廓为空时返回空 Outline
  [[nodiscard]] Outline outline(uint16_t gid) const;

 private:
  Face() = default;
  [[nodiscard]] const uint8_t* table(std::string_view tag) const noexcept;
  [[nodiscard]] uint32_t table_len(std::string_view tag) const noexcept;
  bool parse();
  bool parse_cmap();
  bool parse_name();
  void load_simple_glyph(const uint8_t* p, uint32_t len, Outline* out) const;
  void load_composite_glyph(const uint8_t* p, uint32_t len, Outline* out, int depth) const;
  [[nodiscard]] bool glyph_range(uint16_t gid, const uint8_t** data, uint32_t* len) const;

  std::vector<uint8_t> bytes_;
  std::string family_;
  uint16_t units_per_em_ = 1000;
  uint16_t num_glyphs_ = 0;
  int16_t ascender_ = 0;
  int16_t descender_ = 0;
  int16_t line_gap_ = 0;
  uint16_t num_hmetrics_ = 0;
  bool long_loca_ = false;
  const uint8_t* hmtx_ = nullptr;
  const uint8_t* hhea_ = nullptr;
  const uint8_t* loca_ = nullptr;
  const uint8_t* glyf_ = nullptr;
  uint32_t glyf_len_ = 0;
  const uint8_t* cmap_sub_ = nullptr;  // 选中的 cmap 子表
  uint16_t cmap_format_ = 0;
};

// 有界读取工具（专门给字体解析用，避免 strict-aliasing / 越界）
inline uint16_t read_u16(const uint8_t* p) noexcept {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline int16_t read_i16(const uint8_t* p) noexcept { return static_cast<int16_t>(read_u16(p)); }
inline uint32_t read_u32(const uint8_t* p) noexcept {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

}  // namespace nq::font

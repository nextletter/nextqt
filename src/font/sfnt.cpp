#include "font/sfnt.h"

#include <cstdio>
#include <cstring>

#include "base/diag.h"

namespace nq::font {
namespace {

constexpr int kMaxCompositeDepth = 8;
constexpr uint16_t kMaxGlyphPoints = 10000;
constexpr uint16_t kMaxContours = 512;

struct Reader {
  const uint8_t* p;
  const uint8_t* end;

  [[nodiscard]] bool ok(size_t n) const noexcept {
    return p != nullptr && end != nullptr && static_cast<size_t>(end - p) >= n;
  }
  uint8_t u8() {
    if (!ok(1)) return 0;
    return *p++;
  }
  uint16_t u16() {
    if (!ok(2)) return 0;
    const uint16_t v = read_u16(p);
    p += 2;
    return v;
  }
  int16_t i16() { return static_cast<int16_t>(u16()); }
  uint32_t u32() {
    if (!ok(4)) return 0;
    const uint32_t v = read_u32(p);
    p += 4;
    return v;
  }
  void skip(size_t n) {
    if (!ok(n)) {
      p = end;
      return;
    }
    p += n;
  }
};

// 复合字形的二维变换
struct Transform {
  float xx = 1, xy = 0, yx = 0, yy = 1, dx = 0, dy = 0;

  Vec2 apply(Vec2 v) const noexcept {
    return {xx * v.x + xy * v.y + dx, yx * v.x + yy * v.y + dy};
  }
};

// F2Dot14
float f2dot14(int16_t v) noexcept { return static_cast<float>(v) / 16384.0f; }

}  // namespace

Result<std::unique_ptr<Face>> Face::load_file(const char* path) {
  std::FILE* f = std::fopen(path, "rb");
  if (f == nullptr) return Status{Code::kNotFound, "font file not found"};
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(f);
    return Status{Code::kCorrupt, "empty font file"};
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  const size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  if (got != bytes.size()) return Status{Code::kCorrupt, "short read"};
  return load(std::move(bytes));
}

Result<std::unique_ptr<Face>> Face::load(std::vector<uint8_t> bytes) {
  if (bytes.size() < 12) return Status{Code::kCorrupt, "sfnt header too small"};
  std::unique_ptr<Face> face(new Face());
  face->bytes_ = std::move(bytes);
  if (!face->parse()) return Status{Code::kCorrupt, "invalid sfnt tables"};
  return face;
}

const uint8_t* Face::table(std::string_view tag) const noexcept {
  const uint32_t* dummy = nullptr;
  (void)dummy;
  if (bytes_.size() < 12) return nullptr;
  const uint16_t count = read_u16(bytes_.data() + 4);
  for (uint16_t i = 0; i < count; ++i) {
    const size_t rec = 12 + static_cast<size_t>(i) * 16;
    if (rec + 16 > bytes_.size()) return nullptr;
    const uint8_t* p = bytes_.data() + rec;
    if (tag.size() != 4 || std::memcmp(p, tag.data(), 4) != 0) continue;
    const uint32_t offset = read_u32(p + 8);
    const uint32_t length = read_u32(p + 12);
    if (offset > bytes_.size() || length > bytes_.size() - offset) return nullptr;
    return bytes_.data() + offset;
  }
  return nullptr;
}

uint32_t Face::table_len(std::string_view tag) const noexcept {
  if (bytes_.size() < 12) return 0;
  const uint16_t count = read_u16(bytes_.data() + 4);
  for (uint16_t i = 0; i < count; ++i) {
    const size_t rec = 12 + static_cast<size_t>(i) * 16;
    if (rec + 16 > bytes_.size()) return 0;
    const uint8_t* p = bytes_.data() + rec;
    if (tag.size() != 4 || std::memcmp(p, tag.data(), 4) != 0) continue;
    return read_u32(p + 12);
  }
  return 0;
}

bool Face::parse() {
  const uint32_t version = read_u32(bytes_.data());
  if (version != 0x00010000u && version != 0x74727565u /* 'true' */) {
    // 'OTTO' 是 CFF 轮廓，暂不支持
    diag::counter("font.rejected_cff", "font").add();
    return false;
  }
  const uint8_t* head = table("head");
  const uint8_t* maxp = table("maxp");
  const uint8_t* hhea = table("hhea");
  if (head == nullptr || maxp == nullptr || hhea == nullptr) return false;

  units_per_em_ = read_u16(head + 18);
  long_loca_ = read_i16(head + 50) == 1;
  if (units_per_em_ == 0) units_per_em_ = 1000;
  num_glyphs_ = read_u16(maxp + 4);
  ascender_ = read_i16(hhea + 4);
  descender_ = read_i16(hhea + 6);
  line_gap_ = read_i16(hhea + 8);
  num_hmetrics_ = read_u16(hhea + 34);

  loca_ = table("loca");
  glyf_ = table("glyf");
  glyf_len_ = table_len("glyf");
  hmtx_ = table("hmtx");
  hhea_ = hhea;
  if (loca_ == nullptr || glyf_ == nullptr || hmtx_ == nullptr) return false;
  if (!parse_cmap()) return false;
  parse_name();
  return true;
}

bool Face::parse_cmap() {
  const uint8_t* cmap = table("cmap");
  if (cmap == nullptr) return false;
  const uint16_t count = read_u16(cmap + 2);
  const uint8_t* best4 = nullptr;
  const uint8_t* best12 = nullptr;
  for (uint16_t i = 0; i < count; ++i) {
    const size_t rec = 4 + static_cast<size_t>(i) * 8;
    const uint8_t* p = cmap + rec;
    const uint16_t platform = read_u16(p);
    const uint16_t encoding = read_u16(p + 2);
    const uint32_t offset = read_u32(p + 4);
    const uint8_t* sub = cmap + offset;
    if (sub < cmap) return false;
    const uint16_t format = read_u16(sub);
    const bool unicode = platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10));
    if (!unicode) continue;
    if (format == 12) best12 = sub;
    if (format == 4) best4 = sub;
  }
  if (best12 != nullptr) {
    cmap_sub_ = best12;
    cmap_format_ = 12;
    return true;
  }
  if (best4 != nullptr) {
    cmap_sub_ = best4;
    cmap_format_ = 4;
    return true;
  }
  return false;
}

bool Face::parse_name() {
  const uint8_t* name = table("name");
  if (name == nullptr) return false;
  const uint16_t count = read_u16(name + 2);
  const uint16_t string_offset = read_u16(name + 4);
  for (uint16_t i = 0; i < count; ++i) {
    const uint8_t* rec = name + 6 + static_cast<size_t>(i) * 12;
    const uint16_t platform = read_u16(rec);
    const uint16_t name_id = read_u16(rec + 6);
    if (name_id != 1) continue;  // 家族名
    if (platform != 3 && platform != 0) continue;
    const uint16_t length = read_u16(rec + 8);
    const uint16_t offset = read_u16(rec + 10);
    const uint8_t* str = name + string_offset + offset;
    std::string out;
    for (uint16_t k = 0; k + 1 < length; k += 2) {
      const uint16_t ch = read_u16(str + k);
      if (ch < 0x80) {
        out.push_back(static_cast<char>(ch));
      } else {
        out.push_back('?');  // 非 ASCII 家族名暂不转换
      }
    }
    if (!out.empty()) {
      family_ = std::move(out);
      return true;
    }
  }
  return true;
}

uint16_t Face::glyph_index(char32_t cp) const noexcept {
  if (cmap_sub_ == nullptr) return 0;
  if (cmap_format_ == 12) {
    const uint32_t groups = read_u32(cmap_sub_ + 12);
    if (groups > 100000) return 0;
    uint32_t lo = 0;
    uint32_t hi = groups;
    while (lo < hi) {
      const uint32_t mid = (lo + hi) / 2;
      const uint8_t* g = cmap_sub_ + 16 + static_cast<size_t>(mid) * 12;
      const uint32_t start = read_u32(g);
      const uint32_t end = read_u32(g + 4);
      if (static_cast<uint32_t>(cp) < start) {
        hi = mid;
      } else if (static_cast<uint32_t>(cp) > end) {
        lo = mid + 1;
      } else {
        const uint32_t start_gid = read_u32(g + 8);
        const uint32_t gid = start_gid + (static_cast<uint32_t>(cp) - start);
        return gid < num_glyphs_ ? static_cast<uint16_t>(gid) : 0;
      }
    }
    return 0;
  }
  // format 4
  const uint16_t seg_x2 = read_u16(cmap_sub_ + 6);
  const uint8_t* end_codes = cmap_sub_ + 14;
  const uint8_t* start_codes = end_codes + seg_x2 + 2;
  const uint8_t* id_deltas = start_codes + seg_x2;
  const uint8_t* id_range_offsets = id_deltas + seg_x2;
  const uint16_t c = static_cast<uint16_t>(cp > 0xFFFF ? 0xFFFF : cp);
  for (uint16_t i = 0; i < seg_x2 / 2; ++i) {
    const uint16_t end = read_u16(end_codes + static_cast<size_t>(i) * 2);
    if (c > end) continue;
    const uint16_t start = read_u16(start_codes + static_cast<size_t>(i) * 2);
    if (c < start) return 0;
    const uint16_t delta = read_u16(id_deltas + static_cast<size_t>(i) * 2);
    const uint16_t range_offset = read_u16(id_range_offsets + static_cast<size_t>(i) * 2);
    if (range_offset == 0) return static_cast<uint16_t>(c + delta) % num_glyphs_;
    const uint8_t* addr = id_range_offsets + static_cast<size_t>(i) * 2 + range_offset +
                          static_cast<size_t>(c - start) * 2;
    uint16_t gid = read_u16(addr);
    if (gid != 0) gid = static_cast<uint16_t>(gid + delta);
    return gid < num_glyphs_ ? gid : 0;
  }
  return 0;
}

uint16_t Face::advance(uint16_t gid) const noexcept {
  if (hmtx_ == nullptr || num_hmetrics_ == 0) return 0;
  const uint16_t index = gid < num_hmetrics_ ? gid : static_cast<uint16_t>(num_hmetrics_ - 1);
  return read_u16(hmtx_ + static_cast<size_t>(index) * 4);
}

bool Face::glyph_range(uint16_t gid, const uint8_t** data, uint32_t* len) const {
  if (gid >= num_glyphs_) return false;
  uint32_t start = 0;
  uint32_t end = 0;
  if (long_loca_) {
    start = read_u32(loca_ + static_cast<size_t>(gid) * 4);
    end = read_u32(loca_ + static_cast<size_t>(gid) * 4 + 4);
  } else {
    start = static_cast<uint32_t>(read_u16(loca_ + static_cast<size_t>(gid) * 2)) * 2;
    end = static_cast<uint32_t>(read_u16(loca_ + static_cast<size_t>(gid) * 2 + 2)) * 2;
  }
  if (end <= start || end > glyf_len_) return false;
  *data = glyf_ + start;
  *len = end - start;
  return true;
}

Outline Face::outline(uint16_t gid) const {
  Outline out;
  const uint8_t* data = nullptr;
  uint32_t len = 0;
  if (!glyph_range(gid, &data, &len) || len < 10) return out;
  const int16_t contours = read_i16(data);
  if (contours > 0) {
    load_simple_glyph(data, len, &out);
  } else if (contours < 0) {
    load_composite_glyph(data, len, &out, 0);
  }
  return out;
}

void Face::load_simple_glyph(const uint8_t* p, uint32_t len, Outline* out) const {
  Reader r{p, p + len};
  r.skip(10);  // header
  const int16_t contours = read_i16(p);
  if (contours <= 0 || contours > static_cast<int16_t>(kMaxContours)) return;
  const uint16_t n = static_cast<uint16_t>(contours);
  std::vector<uint16_t> ends(static_cast<size_t>(n));
  for (uint16_t i = 0; i < n; ++i) ends[i] = r.u16();
  const uint16_t points = static_cast<uint16_t>(ends.empty() ? 0 : ends.back() + 1);
  if (points == 0 || points > kMaxGlyphPoints) return;
  const uint16_t instructions = r.u16();
  r.skip(instructions);

  std::vector<uint8_t> flags;
  flags.reserve(points);
  while (flags.size() < points) {
    const uint8_t flag = r.u8();
    flags.push_back(flag);
    if ((flag & 0x08) != 0) {  // REPEAT
      const uint8_t repeat = r.u8();
      for (uint8_t k = 0; k < repeat && flags.size() < points; ++k) flags.push_back(flag);
    }
  }
  std::vector<int32_t> xs(static_cast<size_t>(points), 0);
  std::vector<int32_t> ys(static_cast<size_t>(points), 0);
  int32_t value = 0;
  for (uint16_t i = 0; i < points; ++i) {
    const uint8_t flag = flags[i];
    if ((flag & 0x02) != 0) {
      const uint8_t delta = r.u8();
      value += (flag & 0x10) != 0 ? static_cast<int32_t>(delta) : -static_cast<int32_t>(delta);
    } else if ((flag & 0x10) == 0) {
      value += r.i16();
    }
    xs[i] = value;
  }
  value = 0;
  for (uint16_t i = 0; i < points; ++i) {
    const uint8_t flag = flags[i];
    if ((flag & 0x04) != 0) {
      const uint8_t delta = r.u8();
      value += (flag & 0x20) != 0 ? static_cast<int32_t>(delta) : -static_cast<int32_t>(delta);
    } else if ((flag & 0x20) == 0) {
      value += r.i16();
    }
    ys[i] = value;
  }

  uint16_t begin = 0;
  for (uint16_t i = 0; i < n; ++i) {
    const uint16_t end = ends[i];
    if (end < begin || end >= points) {
      begin = static_cast<uint16_t>(end + 1);
      continue;
    }
    for (uint16_t k = begin; k <= end; ++k) {
      out->points.push_back(
          ContourPoint{Vec2{static_cast<float>(xs[k]), static_cast<float>(ys[k])},
                       (flags[k] & 0x01) != 0});
    }
    out->contour_ends.push_back(static_cast<uint32_t>(out->points.size()));
    begin = static_cast<uint16_t>(end + 1);
  }
}

void Face::load_composite_glyph(const uint8_t* p, uint32_t len, Outline* out,
                                int depth) const {
  if (depth > kMaxCompositeDepth) return;
  Reader r{p, p + len};
  r.skip(10);
  for (;;) {
    const uint16_t flags = r.u16();
    const uint16_t gid = r.u16();
    int16_t arg1 = 0;
    int16_t arg2 = 0;
    if ((flags & 0x0001) != 0) {
      arg1 = r.i16();
      arg2 = r.i16();
    } else {
      arg1 = static_cast<int8_t>(r.u8());
      arg2 = static_cast<int8_t>(r.u8());
    }
    Transform t;
    if ((flags & 0x0008) != 0) {  // WE_HAVE_A_SCALE
      const float s = f2dot14(r.i16());
      t.xx = s;
      t.yy = s;
    } else if ((flags & 0x0040) != 0) {  // X_AND_Y_SCALE
      t.xx = f2dot14(r.i16());
      t.yy = f2dot14(r.i16());
    } else if ((flags & 0x0080) != 0) {  // TWO_BY_TWO
      t.xx = f2dot14(r.i16());
      t.xy = f2dot14(r.i16());
      t.yx = f2dot14(r.i16());
      t.yy = f2dot14(r.i16());
    }
    if ((flags & 0x0002) != 0) {  // ARGS_ARE_XY_VALUES
      t.dx = static_cast<float>(arg1);
      t.dy = static_cast<float>(arg2);
    }
    const uint8_t* sub_data = nullptr;
    uint32_t sub_len = 0;
    if (glyph_range(gid, &sub_data, &sub_len)) {
      const int16_t sub_contours = read_i16(sub_data);
      if (sub_contours > 0) {
        Outline sub;
        load_simple_glyph(sub_data, sub_len, &sub);
        const uint32_t base = static_cast<uint32_t>(out->points.size());
        for (const ContourPoint& cp : sub.points) {
          out->points.push_back(ContourPoint{t.apply(cp.p), cp.on_curve});
        }
        for (uint32_t end : sub.contour_ends) out->contour_ends.push_back(base + end);
      } else if (sub_contours < 0) {
        Outline sub;
        load_composite_glyph(sub_data, sub_len, &sub, depth + 1);
        const uint32_t base = static_cast<uint32_t>(out->points.size());
        for (const ContourPoint& cp : sub.points) {
          out->points.push_back(ContourPoint{t.apply(cp.p), cp.on_curve});
        }
        for (uint32_t end : sub.contour_ends) out->contour_ends.push_back(base + end);
      }
    }
    if ((flags & 0x0020) == 0) break;  // MORE_COMPONENTS
  }
}

}  // namespace nq::font

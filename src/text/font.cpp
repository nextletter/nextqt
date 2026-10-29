#include "text/font.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include "base/diag.h"
#include "base/utf8.h"
#include "raster/raster.h"

namespace nq::text {
namespace {

constexpr float kFlattenTolerance = 0.2f;  // 设备像素
constexpr size_t kDefaultCacheBudget = 8u * 1024u * 1024u;

struct GlyphMask {
  int width = 0;
  int height = 0;
  int left = 0;  // 相对 floor(pen_x)
  int top = 0;   // 相对 baseline
  std::vector<uint8_t> bits;
};

struct GlyphKey {
  uint16_t gid = 0;
  uint16_t size_q = 0;   // px * 4
  uint8_t subpixel = 0;  // 0..3
  bool operator<(const GlyphKey& o) const noexcept {
    if (gid != o.gid) return gid < o.gid;
    if (size_q != o.size_q) return size_q < o.size_q;
    return subpixel < o.subpixel;
  }
};

// 简易折线构建器（二次贝塞尔自适应细分）
struct PolyBuilder {
  std::vector<Vec2> points;
  std::vector<uint32_t> ends;
  bool open = false;

  void move_to(Vec2 p) {
    if (open) close();
    points.push_back(p);
    open = true;
  }
  void line_to(Vec2 p) {
    if (!open) {
      move_to(p);
      return;
    }
    points.push_back(p);
  }
  void quad_to(Vec2 c, Vec2 p, int depth = 0) {
    // 控制点到弦的距离足够小即停止细分
    const float dx = p.x - (points.empty() ? c.x : points.back().x);
    const float dy = p.y - (points.empty() ? c.y : points.back().y);
    const float cx = points.empty() ? c.x : points.back().x;
    const float cy = points.empty() ? c.y : points.back().y;
    const float num = std::fabs((c.x - cx) * dy - (c.y - cy) * dx);
    const float den = std::sqrt(dx * dx + dy * dy);
    const float dist = den > 0.0f ? num / den : 0.0f;
    if (depth >= 8 || dist <= kFlattenTolerance) {
      line_to(p);
      return;
    }
    const Vec2 a{(cx + c.x) * 0.5f, (cy + c.y) * 0.5f};
    const Vec2 b{(c.x + p.x) * 0.5f, (c.y + p.y) * 0.5f};
    const Vec2 m{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    quad_to(a, m, depth + 1);
    quad_to(b, p, depth + 1);
  }
  void close() {
    if (!open) return;
    if (points.size() >= 2) {
      const Vec2 first = points[ends.empty() ? 0 : ends.back()];
      if (points.back().x != first.x || points.back().y != first.y) points.push_back(first);
    }
    ends.push_back(static_cast<uint32_t>(points.size()));
    open = false;
  }
};

Vec2 midpoint(Vec2 a, Vec2 b) noexcept { return {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}; }

// 把一个 TrueType 轮廓（可能以 off-curve 开头）转成折线
void flatten_contour(const font::Outline& outline, size_t begin, size_t end, float scale,
                     float ox, float oy, PolyBuilder* out) {
  const size_t n = end - begin;
  if (n == 0) return;
  auto at = [&](size_t i) {
    const font::ContourPoint& cp = outline.points[begin + (i % n)];
    return font::ContourPoint{Vec2{cp.p.x * scale + ox, oy - cp.p.y * scale}, cp.on_curve};
  };

  // 找到起点（优先 on-curve；全是 off-curve 时用前两点的中点）
  size_t start = 0;
  bool have_on_curve = false;
  for (size_t i = 0; i < n; ++i) {
    if (at(i).on_curve) {
      start = i;
      have_on_curve = true;
      break;
    }
  }
  Vec2 first_point{};
  size_t index = 0;
  if (have_on_curve) {
    first_point = at(start).p;
    index = 1;
  } else {
    first_point = midpoint(at(0).p, at(1).p);
    index = 1;  // at(0) 作为控制点
  }
  out->move_to(first_point);
  Vec2 prev = first_point;
  Vec2 control{};
  bool have_control = false;

  for (size_t k = 0; k < n; ++k) {
    const font::ContourPoint cp = at(start + index + k);
    if (cp.on_curve) {
      if (have_control) {
        out->quad_to(control, cp.p);
        have_control = false;
      } else {
        out->line_to(cp.p);
      }
      prev = cp.p;
    } else {
      if (have_control) {
        const Vec2 mid = midpoint(control, cp.p);
        out->quad_to(control, mid);
        prev = mid;
      }
      control = cp.p;
      have_control = true;
    }
  }
  if (have_control) {
    out->quad_to(control, first_point);
  } else if (prev.x != first_point.x || prev.y != first_point.y) {
    out->line_to(first_point);
  }
  out->close();
}

}  // namespace

struct Font::Impl {
  std::unique_ptr<font::Face> face;
  std::string name;
  std::map<GlyphKey, GlyphMask> cache;
  size_t cache_bytes = 0;
  size_t budget = kDefaultCacheBudget;
  float last_scale = 1.0f;

  [[nodiscard]] float scale_for(float px) const noexcept {
    const float upem = static_cast<float>(face->units_per_em());
    return upem > 0.0f ? px / upem : px / 1000.0f;
  }

  const GlyphMask* glyph(uint16_t gid, float px, uint8_t subpixel) {
    const uint16_t size_q = static_cast<uint16_t>(std::lround(px * 4.0f));
    const GlyphKey key{gid, size_q, subpixel};
    auto it = cache.find(key);
    if (it != cache.end()) {
      diag::counter("text.glyph_cache_hits", "glyph").add();
      return &it->second;
    }
    diag::counter("text.glyph_cache_misses", "glyph").add();

    const font::Outline outline = face->outline(gid);
    if (outline.empty()) return nullptr;

    const float scale = scale_for(px);
    const float ox = static_cast<float>(subpixel) * 0.25f;
    PolyBuilder poly;
    size_t begin = 0;
    for (uint32_t end : outline.contour_ends) {
      flatten_contour(outline, begin, end, scale, ox, 0.0f, &poly);
      begin = end;
    }
    if (poly.points.empty()) return nullptr;

    float min_x = poly.points[0].x;
    float max_x = min_x;
    float min_y = poly.points[0].y;
    float max_y = min_y;
    for (const Vec2& p : poly.points) {
      min_x = std::min(min_x, p.x);
      max_x = std::max(max_x, p.x);
      min_y = std::min(min_y, p.y);
      max_y = std::max(max_y, p.y);
    }
    const int left = static_cast<int>(std::floor(min_x));
    const int top = static_cast<int>(std::floor(min_y));
    const int width = static_cast<int>(std::ceil(max_x)) - left;
    const int height = static_cast<int>(std::ceil(max_y)) - top;
    if (width <= 0 || height <= 0) return nullptr;

    GlyphMask mask;
    mask.width = width;
    mask.height = height;
    mask.left = left;
    mask.top = top;
    mask.bits.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    raster::compute_coverage(poly.points, poly.ends, left, top, width, height, mask.bits.data());
    diag::counter("text.glyphs_rasterized", "glyph").add();

    const size_t bytes = mask.bits.size();
    if (cache_bytes + bytes > budget) {
      diag::counter("text.cache_evictions", "glyph").add(cache.size());
      cache.clear();
      cache_bytes = 0;
    }
    auto [slot, inserted] = cache.emplace(key, std::move(mask));
    (void)inserted;
    cache_bytes += bytes;
    return &slot->second;
  }

  float advance_px(uint16_t gid, float px) const noexcept {
    return static_cast<float>(face->advance(gid)) * scale_for(px);
  }
};

Font::Font() : impl_(new Impl()) {}
Font::~Font() = default;
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

Result<std::unique_ptr<Font>> Font::load_file(const char* path) {
  NQ_TRY(face_result, font::Face::load_file(path));
  std::unique_ptr<Font> font(new Font());
  font->impl_->face = std::move(face_result);
  font->impl_->name = font->impl_->face->family();
  if (font->impl_->name.empty()) font->impl_->name = "unknown";
  return font;
}

Result<std::unique_ptr<Font>> Font::load_bytes(std::vector<uint8_t> bytes, std::string name) {
  NQ_TRY(face_result, font::Face::load(std::move(bytes)));
  std::unique_ptr<Font> font(new Font());
  font->impl_->face = std::move(face_result);
  font->impl_->name = std::move(name);
  return font;
}

const font::Face& Font::face() const noexcept { return *impl_->face; }
const std::string& Font::name() const noexcept { return impl_->name; }

float Font::ascent(float px) const noexcept {
  return static_cast<float>(impl_->face->ascender()) * impl_->scale_for(px);
}
float Font::descent(float px) const noexcept {
  return -static_cast<float>(impl_->face->descender()) * impl_->scale_for(px);
}
float Font::line_height(float px) const noexcept {
  const float upem_scale = impl_->scale_for(px);
  return static_cast<float>(impl_->face->ascender() - impl_->face->descender() +
                            impl_->face->line_gap()) *
         upem_scale;
}

int Font::measure(std::string_view utf8, float px) const noexcept {
  float width = 0.0f;
  Utf8Iterator it(utf8);
  while (!it.done()) {
    const char32_t cp = it.next();
    width += impl_->advance_px(impl_->face->glyph_index(cp), px);
  }
  return static_cast<int>(std::lround(width));
}

float Font::draw(Surface& target, float x, float baseline, std::string_view utf8, float px,
                 Rgba8 color) {
  float pen = x;
  Utf8Iterator it(utf8);
  while (!it.done()) {
    const char32_t cp = it.next();
    const uint16_t gid = impl_->face->glyph_index(cp);
    const float advance = impl_->advance_px(gid, px);
    if (gid != 0) {
      const float frac = pen - std::floor(pen);
      const uint8_t subpixel = static_cast<uint8_t>(clampi(
          static_cast<int>(std::lround(frac * 4.0f)), 0, 3));
      const GlyphMask* mask = impl_->glyph(gid, px, subpixel);
      if (mask != nullptr) {
        const int dst_x = static_cast<int>(std::floor(pen)) + mask->left;
        const int dst_y = static_cast<int>(std::lround(baseline)) + mask->top;
        for (int row = 0; row < mask->height; ++row) {
          const uint8_t* line = mask->bits.data() + static_cast<size_t>(row) *
                                                        static_cast<size_t>(mask->width);
          for (int col = 0; col < mask->width; ++col) {
            const uint32_t cov = line[col];
            if (cov == 0) continue;
            target.blend_pixel(dst_x + col, dst_y + row, color, cov);
          }
        }
        diag::counter("text.pixels_shaded", "px").add(static_cast<uint64_t>(mask->width) *
                                                      static_cast<uint64_t>(mask->height));
      }
    }
    pen += advance;
  }
  return pen;
}

size_t Font::wrap(std::string_view utf8, float px, float max_width, std::string_view* out_line) {
  float width = 0.0f;
  size_t last_space = std::string_view::npos;
  Utf8Iterator it(utf8);
  size_t consumed = 0;
  while (!it.done()) {
    const size_t before = it.byte_index();
    const char32_t cp = it.next();
    const uint16_t gid = impl_->face->glyph_index(cp);
    const float advance = impl_->advance_px(gid, px);
    if (cp == U' ' || cp == U'\t') last_space = before;
    if (width + advance > max_width && consumed > 0) {
      const size_t cut = (last_space != std::string_view::npos && last_space > 0) ? last_space
                                                                                 : consumed;
      if (out_line != nullptr) *out_line = utf8.substr(0, cut);
      return cut;
    }
    width += advance;
    consumed = it.byte_index();
  }
  if (out_line != nullptr) *out_line = utf8.substr(0, consumed);
  return consumed;
}

void Font::set_cache_budget(size_t bytes) noexcept {
  impl_->budget = bytes == 0 ? 1u : bytes;
}
size_t Font::cache_bytes() const noexcept { return impl_->cache_bytes; }

}  // namespace nq::text

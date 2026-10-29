// NextQt · 字体门面 + 字形掩码缓存（真字体，抗锯齿）
//
// 依赖关系：text → font（轮廓）→ raster（覆盖率）。不依赖 dl，保持分层单向。
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "base/geom.h"
#include "base/status.h"
#include "font/sfnt.h"
#include "raster/surface.h"

namespace nq::text {

class Font {
 public:
  static Result<std::unique_ptr<Font>> load_file(const char* path);
  static Result<std::unique_ptr<Font>> load_bytes(std::vector<uint8_t> bytes, std::string name);
  ~Font();
  Font(Font&&) noexcept;
  Font& operator=(Font&&) noexcept;

  [[nodiscard]] const font::Face& face() const noexcept;
  [[nodiscard]] const std::string& name() const noexcept;

  [[nodiscard]] float ascent(float px) const noexcept;
  [[nodiscard]] float descent(float px) const noexcept;  // 正数
  [[nodiscard]] float line_height(float px) const noexcept;
  [[nodiscard]] int measure(std::string_view utf8, float px) const noexcept;

  // 在基线 (x, baseline) 处绘制；返回结束 x 坐标
  float draw(Surface& target, float x, float baseline, std::string_view utf8, float px,
             Rgba8 color);

  // 期望宽度（px）与容差内换行：返回消费的字节数
  size_t wrap(std::string_view utf8, float px, float max_width, std::string_view* out_line);

  void set_cache_budget(size_t bytes) noexcept;
  [[nodiscard]] size_t cache_bytes() const noexcept;

 private:
  Font();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace nq::text

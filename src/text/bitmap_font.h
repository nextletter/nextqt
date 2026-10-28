// NextQt · 位图字体（M0 占位；M1 起由自研 Unicode/整形/字体栈替换）
//
// 5x7 点阵，支持 ASCII 大写、数字与常用标点；小写自动映射到大写。
// 存在的意义：让示例与 showcase 有真实文本层次，同时把"文本 → 字形 → 覆盖掩码"
// 的调用面先固化下来，后续替换实现不影响上层。
#pragma once

#include <cstdint>
#include <string_view>

#include "base/geom.h"
#include "raster/surface.h"

namespace nq::text {

inline constexpr int kGlyphW = 5;
inline constexpr int kGlyphH = 7;
inline constexpr int kTracking = 1;  // 字间空格（以 glyph 像素为单位）

// 返回文本像素宽度（未缩放）
[[nodiscard]] int measure_unscaled(std::string_view text) noexcept;
[[nodiscard]] int measure(std::string_view text, int scale) noexcept;

// 在 (x, y) 处绘制文本；y 为字形顶部。返回结束 x 坐标。
int draw_text(Surface& target, int x, int y, int scale, Rgba8 color,
              std::string_view text) noexcept;

}  // namespace nq::text

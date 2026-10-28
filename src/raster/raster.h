// NextQt · CPU 参考光栅器（M0：矩形解析式 + 路径 16x16 超采样）
//
// M0 目标：正确的、确定性的像素真值，用于 golden 测试与示例。
// M1 起替换为分层光栅（Tier 0 解析式 / Tier 1 掩码缓存 / Tier 2 compute / Tier 3 兜底），
// 见 docs/02-render.md §2。接口保持稳定，替换不影响上层。
#pragma once

#include <vector>

#include "base/geom.h"
#include "dl/display_list.h"
#include "raster/surface.h"

namespace nq::raster {

inline constexpr int kSupersample = 16;  // 每轴子采样数（M0 参考实现）

// 执行显示列表（M0 只支持矩形裁剪）
void replay(const dl::DisplayList& list, Surface& target);

// 轴对齐矩形：精确解析覆盖率（Tier 0 原型）
void fill_rect_analytic(Surface& target, RectF rect, Rgba8 color, RectF clip);

// 折线轮廓（nonzero 环绕规则）→ 16x16 超采样覆盖率
void fill_polylines(Surface& target, const std::vector<Vec2>& points,
                    const std::vector<uint32_t>& contour_ends, Rgba8 color, RectF clip);

}  // namespace nq::raster

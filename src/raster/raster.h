// NextQt · CPU 参考光栅器
//
// M0 实现：
//   Tier 0 原型  轴对齐矩形：精确解析覆盖率（1 采样，无 conflation）
//   参考实现     任意折线轮廓：16x16 超采样 + nonzero 环绕规则（慢但可验证）
//   阴影        覆盖掩码 + 三次盒式模糊（近似高斯），比多层 saveLayer 便宜
// M1 起替换为分层光栅（掩码 atlas / compute 覆盖 / MSAA 兜底），接口保持不变。
#pragma once

#include <cstdint>
#include <vector>

#include "base/geom.h"
#include "dl/display_list.h"
#include "raster/surface.h"

namespace nq::raster {

inline constexpr int kSupersample = 16;  // 每轴子采样数（M0 参考实现）

// 执行显示列表（M0 支持矩形裁剪栈、矩形/圆角/圆/路径/渐变/阴影）
void replay(const dl::DisplayList& list, Surface& target);

// 轴对齐矩形：精确解析覆盖率（Tier 0 原型）
void fill_rect_analytic(Surface& target, RectF rect, Rgba8 color, RectF clip);

// 线性渐变矩形（预乘插值）
void fill_rect_gradient(Surface& target, RectF rect, Rgba8 from, Rgba8 to, bool horizontal,
                        RectF clip);

// 折线轮廓（nonzero 环绕规则）→ 覆盖率
void fill_polylines(Surface& target, const std::vector<Vec2>& points,
                    const std::vector<uint32_t>& contour_ends, Rgba8 color, RectF clip);

// 圆角矩形投影：掩码 + 盒式模糊（3 次）近似高斯
void fill_shadow(Surface& target, RectF rect, float radius, float blur, Rgba8 color,
                 RectF clip);

// 轮廓覆盖率（供测试与阴影复用）：origin 为像素原点，mask 尺寸 width*height
void compute_coverage(const std::vector<Vec2>& points,
                      const std::vector<uint32_t>& contour_ends, int origin_x, int origin_y,
                      int width, int height, uint8_t* mask);

}  // namespace nq::raster

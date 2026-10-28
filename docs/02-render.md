# 02 · 渲染

## 1. 显示列表 IR（`src/dl`）

设计目标：**不可变 / 可缓存 / 可序列化 / 可差分 / 可跨线程共享 / 可回放**。它是引擎内部唯一"渲染真相"，CPU 光栅、GPU 光栅、录制回放、远程渲染、黄金测试都消费同一种数据结构。

```cpp
namespace nq::dl {

enum class Op : uint8_t {                 // 16 字节对齐的 POD，payload 在尾部 arena
  kPushClipRect, kPushClipRRect, kPushClipPath, kPushClipMask, kPopClip,
  kPushLayer, kPopLayer,                     // opacity / blend / filter / saturate
  kDrawRect, kDrawRRect, kDrawCircle, kDrawPath, kDrawLine,
  kDrawShadow, kDrawImage, kDrawAtlas, kDrawVertices, kDrawTextBlob,
  kSave, kRestore, kBackdropFilter, kCustom,
};

struct DrawList final {                    // 引用计数不可变块
  Span<const std::byte>  bytes;            // ops 序列
  Span<const ResourceId> resources;        // 图像/字体/渐变/自定义着色器
  Rect                   bounds;           // 设备无关逻辑像素
  uint64_t               hash;             // 内容哈希（瓦片复用的键）
};

} // namespace nq::dl
```

规则：
- **不含裸指针**，资源一律 index 引用，`Scene` 持有资源表 → 序列化即 memcpy + 资源表。
- 绘制位置用 `LayoutUnit`（定点）而非 f32，保证跨平台与跨帧一致。
- 文本只存 `TextBlobId`（已整形、已断行），记录阶段零整形开销。
- `hash` 是增量计算的（记录时滚动 xxh3 类自研哈希），用于瓦片复用与"本帧无变化"快速路径。

## 2. 光栅分层（核心创新）

单一光栅算法必然在某个图元类上吃亏。我们按**图元类别 + 设备像素尺寸 + 后端能力**在光栅期动态选路，成本模型可测、可用 `NQ_RASTER_FORCE=tier1` 覆写。

| Tier | 适用 | 覆盖率来源 | 每像素成本 | 质量 |
|------|------|-----------|-----------|------|
| **0 解析式** | rect / rrect / circle / ellipse / 轴对齐线 / rrect 描边 | 闭式几何覆盖率，1 采样 | ~0.3 ns | 精确，**无 conflation** |
| **1 CPU 掩码缓存** | 小路径（bbox ≤ 192² 设备像素、段数 ≤ 64）：图标、圆角指示、小图形 | 扫描线解析覆盖 → A8 掩码，进 atlas | 首次 ~2 µs，命中 ~0 | 与 Tier 0 同级 |
| **2 compute 覆盖** | 大路径 / SVG 插画 / 图表 / 字形外框放大 | 线段 → 16×16 tile 分箱 → 有符号面积累加（u32 通道） | ~1.2 ns/px | 精确，无 MSAA 显存 |
| **3 MSAA 兜底** | 无 compute 的后端（GLES3/WebGL1/老 iOS）、剪辑路径等病态输入 | 4× MSAA + stencil-then-cover + resolve | ~2.5 ns/px | 近似，边缘 ≤4 级灰 |

选择逻辑（`raster::choose_tier`）：

```
if (prim.is_analytic_representable)                tier 0
else if (device_bbox <= 192² && segs <= 64 && cache_budget_ok)  tier 1
else if (backend.has_compute() && device_px >= 4096)            tier 2
else                                                            tier 3
```

Tier 1 的价值在于**把 UI 里 95% 的路径图元（图标、logo、圆角、波形）变成一次实例化贴图**；Tier 2 的价值在于**大幅缩放/高 DPI 时不做 MSAA 也不糊**；Tier 0 的价值在于**圆角卡片和圆点不产生任何边缘暗线**（Qt 的 raster 与 Flutter 的 MSAA 都在这里翻车）。

混合与 γ：全部 premultiplied alpha；覆盖率作为 alpha 参与混合；滤镜/渐变/模糊在**线性空间**计算，最终在目标空间合成。当高对比度文本/细线在深色背景出现"发灰"时，启用 `coverage` 的感知修正（自研 LUT，默认在 ppem < 32 且对比度 > 4:1 时生效），这是 Qt/Flutter 长期存在的细节缺陷。

## 3. 几何预处理（`src/raster/geom`）

- 曲线自适应展平：容差 0.25 设备像素，二次/三次 Bézier 用细分深度解析式（`n = ceil(sqrt(k·ε⁻¹))`），结果按缩放比缓存（同一路径在相同 scale 桶下复用展平结果）。
- 变换分类：`identity / translate / axis_scale / similarity / affine / projective`。相似变换下 T0/T1 可直接走（不重算掩码，只重定位）；仿射走重算；投影走逐像素路径（少见）。
- 批处理：按 `(pipeline, texture, blend, clip_state)` 排序合并 drawcall，Tier 0 图元合并为 instanced quad batch（矩形批次可达 数千/次）。

## 4. 剪裁

- 矩形剪裁 → scissor（GPU）/ 扫描线边界（CPU），零成本。
- rrect / 任意路径剪裁 → **烘焙覆盖率掩码**（A8，按需分辨率），比 stencil 更省状态切换，且嵌套剪裁可"相乘合成"成一个掩码。
- clip 与图层：`PushLayer` 才需要离屏；普通 clip 不离屏（这是相对 Flutter `ClipRRect` 强制 saveLayer 的明显优化）。

## 5. 字形渲染

| 场景 | 方案 |
|------|------|
| 正文（ppem 8–96） | Tier 1 路径光栅 → A8 掩码 atlas；`(font_id, ppem_bucket, subpixel_x∈[0,3], glyph_id)` 为键 |
| 动画缩放 / 大标题（ppem > 96 或缩放中） | Tier 2 直接光栅外框（清晰、无位图糊） |
| 缩放到极端的图表标签 | MSDF（离线/异步生成，带 2–3 级 fallback） |
| 彩色字体 | COLRv0/v1 绘制图 → 递归光栅并缓存；sbix/CBDT 位图直贴；SVG-in-OT 第三阶段 |
| 可变字体 | 按当前轴值实例化轮廓（缓存每轴值组合） |

细节：
- **不做传统 hinting**（不 snap 到整像素），只做"ppem ≤ 20 时的垂直度量取整"，与 Flutter/Qt 的实际观感对齐，避免字形畸变。
- 子像素定位 1/4 px，纵向整像素对齐基线 → 垂直方向永不糊。
- atlas 用紧凑 2D 装箱 + LRU 驱逐；单帧新字形 > 阈值时批量上传（一次 `vkCmdCopyBufferToImage`）。
- 亚像素 LCD AA **默认关闭**（跨平台不一致、且需要不透明背景分析），桌面档提供 `NQ_LCD_TEXT=1` 开关，实现为 3 通道 atlas + 水平 5-tap 重建。

## 6. 阴影

- **圆角矩形阴影解析求解**：对 `(σ, corner_radius, blur_kind)` 建二维误差函数 LUT，直接算出每像素覆盖率。精度 8-bit 输出 < 1/255，速度约为模糊图层方案的 6–10 倍，且**没有离屏纹理**。
- 任意路径阴影、内阴影、扩散（spread）走 mip 链模糊管线。
- 阴影图元参与瓦片缓存（阴影随内容变化，缓存键含 σ 与 r 的量化值）。

## 7. 模糊与滤镜

- 高斯用 **mip 链**：逐级 5-tap 下采样到 σ 对应的层级，再 9-tap 上采样回目标级。σ ≤ 100 时单 pass 完成，避免了 Flutter 多次 saveLayer 的开销。
- 玻璃拟态（backdrop filter）：读取目标纹理的 halo 区域（3σ 外扩）→ 模糊 → 色彩矩阵/饱和度 → 合成。halo 必然跨 tile，因此 backdrop 区域强制"damage 扩张 + 相邻 tile 一同重光栅"，并在合成阶段限制重算范围。
- 滤镜链在显示列表里是紧凑的 `FilterOp` 序列，可被成本模型估算（`cost = area × chain_len × σ`），超预算时降级（如把双层模糊合并为单层）并在 devtools 标注。

## 8. 颜色管理

- 主 surface：`B8G8R8A8_SRGB` / `R8G8B8A8_SRGB`；HDR/广色域：`A2B10G10R10`（HDR 用 FP16 中间缓冲）。
- 采样/渐变/模糊/合成中间结果在**线性 + premultiplied** 空间；最终写入目标空间，带**蓝噪声抖动**（自研 void-and-cluster 8×8 纹理）防止 8-bit banding。
- ICC：自研解析（matrix-shaper 与 mAB/mBA LUT 类，覆盖 99% 显示器 profile），转 3D LUT（33³，三线性插值）。
- 广色域：Display P3 / Rec.2020 输出，sRGB 内容做色域映射（感知/相对色度可选）；HDR 支持 PQ 与 HLG，SDR 内容按平台 tone mapping 曲线映射。
- 分数缩放（1.25 / 1.5）：布局在 DIP，光栅在物理像素；1px 发丝线与文本基线**吸附物理像素网格**，其余保持亚像素——这是 Qt（模糊）与 Flutter（发丝线粗细跳变）共同的痛点。

## 9. 瓦片、damage 与帧节奏（`src/compositor`）

```
层树 ──▶ 自动分层（变换/透明度/滤镜/滚动/稳定图像）
     ──▶ 512×512 瓦片 + 每瓦片子显示列表哈希
     ──▶ 键比对：hash 未变且资源版本未变 → 复用上一帧瓦片（跨帧 GPU 驻留）
     ──▶ damage = 脏矩形 ∪ 动画矩形 ∪ 滤镜扩张(3σ) → 与瓦片求交 → 只重光栅这些瓦片
     ──▶ 遮挡剔除：不透明瓦片按 z 前置，被完全遮挡的瓦片不进命令流
     ──▶ 提交（timeline semaphore）→ present
```

**picture cache 默认开启且自动**：Flutter 需要手写 `RepaintBoundary` 才能拿到类似效果，Qt Quick 需要手写 `layer.enabled`；我们把它做成编译期/运行期都无感的默认行为，这是"静态 UI 零成本"的关键。

- 滚动预取：`prefetch = f(velocity)`，向滚动方向多光栅 1–2 屏，避免 checkerboard。
- 动画下沉：`transform / opacity / clip / filter 参数` 的动画在合成线程重采样，UI 线程卡 50 ms 也不掉帧（Flutter 做不到）。
- present 定时：优先 `VK_KHR_present_wait` / `DXGI` waitable swapchain / `CAMetalDisplayLink` 拿到真实 vsync；拿不到时用历史帧间隔预测。VRR 下按内容自适应。
- 输入延迟：合成线程收到输入事件即刻处置可本地响应的部分（滚动、按压高亮），不等下一帧 UI。

## 10. GPU HAL（`src/gpu`）

```cpp
class Device;   class Queue;   class CommandBuffer;  class RenderPass;
class Pipeline; class Buffer;  class Texture;        class Sampler;
class TimelineSemaphore; class QueryPool;  class PipelineCache;  // 落盘
```

| 后端 | 优先级 | 备注 |
|------|--------|------|
| Vulkan 1.1+ | 主（Linux/Windows/Android） | 直接写，不经中间层；用 timeline semaphore、dynamic rendering / render pass 双路径 |
| Metal | 主（macOS/iOS） | 与 Vulkan 后端共享同一份资源模型 |
| D3D12 | 主（Windows 备选/首选之一） | PSO library + async compile |
| GLES 3.0 | 兼容（老设备、嵌入式） | Tier 3 光栅为主 |
| WebGPU | WASM | 浏览器/沙箱 |

工程要点：
- **PSO 预热**：首帧以"录制模式"收集所需 pipeline 集，异步编译（`VK_EXT_graphics_pipeline_library` / D3D12 async），并落盘缓存（`VkPipelineCache` / `MTLBinaryArchive` / PSO library），键含驱动版本 → 二次启动无卡顿。
- **显存预算**：所有 GPU 资源走统一的 `GpuPool`（slab + LRU + 预算），暴露占用给 devtools；`trim` 在后台与失焦时触发。
- **设备丢失**（TDR / 驱动崩溃）：重建 HAL 对象，从 CPU 侧 `Scene` 重放；不重启进程。
- 多窗口共享 device、字形 atlas、图像池；每窗口独立 swapchain。
- 着色器：源用 GLSL（Vulkan/GLES），Metal/D3D12 手写等价版本，由 `shadergen` 生成变体并做 SPIR-V 校验 + 反射；shader 总数控制在 ~20 个（不建通用着色器编译器）。

## 11. CPU 参考光栅器（`src/raster`）

它不只是兜底，而是**产品的质量基准**：

| 用途 | 说明 |
|------|------|
| 像素真值 | CI golden 测试在 CPU 后端跑，GPU 后端只需"结构性一致" |
| 无 GPU 环境 | 服务器渲染、CI、容器、无 GPU 的嵌入式 |
| 打印 / PDF / SVG 导出 | 复用同一显示列表（对齐 Qt 的打印能力，不落后） |
| 远程渲染 | 编码瓦片流（H.264/自研无损）到瘦客户端 |
| 性能下限 | 单核填充 > 250 Mpix/s（AVX2），多核线性扩展 |

实现：活动边表扫描线 + 8-bit 覆盖率，16 px SIMD 填充，瓦片并行、结果与线程数无关（确定性）。

## 12. 帧预算（4K@60，16.6 ms）

| 阶段 | 预算 | 说明 |
|------|------|------|
| 输入 → 信号传播 | 0.3 ms | |
| build + style + layout | 1.7 ms | 5k 节点、1% 脏区 |
| record（显示列表） | 0.6 ms | |
| 光栅（增量瓦片） | 6.0 ms | 全屏重绘时占满；稳态通常 < 1 ms |
| 合成 + present | 1.2 ms | |
| 余量 | 6.8 ms | 给 GC-less 但仍需分配/字体加载的抖动 |

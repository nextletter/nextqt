# 08 · 可测试性与性能可测量

> 两个硬性架构要求：**任何 feature 在合并前必须可测**；**任何性能相关改动必须可测量、可回归**。
> 这不是流程文档——它要求引擎本身提供测试与度量所需的基础设施（注入点、计数器、确定性模式、trace）。

## 1. 架构级可测试性（先有地基，再谈测试）

核心引擎必须能在**没有窗口、没有 GPU、没有文件系统、没有真实时间**的环境里完整运行：

```cpp
namespace nq {
struct Host {                            // 所有非确定性来源的唯一入口
  Clock&      clock;                     // 单调/系统时间；DeterministicClock 固定步进
  Rng&        rng;                       // 一切随机（抖动/采样偏移/布局打散）必须从这里取
  FileSystem& fs;                        // 字体/图像/配置；MemoryFs 供单测
  TaskScheduler& tasks;                  // 线程池；InlineScheduler 单线程同步执行
  Alloc&      alloc;                     // 记账分配器；FailureAlloc 可注入分配失败
  Diag&       diag;                      // 计数器 / trace / 断言策略
};
}
```

| 注入点 | 默认实现 | 测试实现 | 解决什么问题 |
|--------|----------|----------|--------------|
| `Clock` | 系统时钟 | `DeterministicClock`（手动推进） | 动画、超时、帧节奏测试无 `sleep` |
| `Rng` | 随机设备种子 | 固定种子 / 记录回放 | 抖动、采样、布局稳定可复现 |
| `FileSystem` | OS | `MemoryFs` | 字体/图像测试不依赖外部文件 |
| `TaskScheduler` | 线程池 | `InlineScheduler`（同步） | 消除并发不确定性，单步调试 |
| `Alloc` | malloc + arena | 记账 / 注入失败 / OOM 模拟 | 内存断言、OOM 路径覆盖 |
| `platform::Window` | 真窗口 | `HeadlessWindow` | 端到端场景测试无需 GUI |
| `gpu::Device` | 真 GPU | `NullDevice`（记录命令流）+ CPU 光栅 | GPU 行为可在 CI 断言 |

CI 里这些是有测试强制覆盖的：**`src/base` 的注入点若被绕过（直接调 `::clock_gettime`/`rand`/`std::filesystem`），clang-tidy 自定义检查直接报错。**

## 2. 测试金字塔（每一层都有对应的自研基础设施）

| 层 | 内容 | 载体 | CI 频率 |
|----|------|------|---------|
| L0 单元 | 纯函数：几何、LayoutUnit 运算、哈希、容器、分段 | `nqtest` 断言 + 属性测试 | 每次 push |
| L1 一致性 | Unicode/OT/字体/BiDi/换行 conformance + oracle 对比 | `conformance` 套件 | 每次 push（快子集）+ 夜间（全量） |
| L2 行为快照 | 场景构建 → **显示列表哈希** + 布局 fragment 快照 + **计数器断言** | `nqtest snapshot` | 每次 push |
| L3 图像黄金 | CPU 光栅渲染 → PNG 对比（容差模式） | `nqtest golden` | 每次 push（分片） |
| L4 交互回放 | 脚本化输入 → 逐帧 `Scene` 哈希 + 最终图像 | `nq_replay` | 每次 push |
| L5 端到端 | 真实窗口/真实 GPU/真实 IME/屏幕阅读器 | 自托管 runner + 设备农场 | 夜间 / 发版前 |
| L6 压力与模糊 | fuzz（字体/图像/dump 反序列化）、长跑内存、OOM 注入 | libFuzzer + `nqtest stress` | 夜间 / 每周长跑 |

**L2 是最重要的一层**：比像素稳定、比单元测试完整。场景的显示列表哈希变化 = 渲染行为变化，一个 32 字节的哈希就能在 1 ms 内抓住"圆角位置偏了 0.5px"这种问题，而不用比图像。

## 3. 让"新 feature 默认可测"的机制

任何 feature 的 Definition of Done（PR 模板强制勾选，CI 校验文件存在）：

1. **≥3 个 golden 用例**：正常 / 极值（0、负数、超大、NaN）/ 交互态（hover/disabled/RTL），放在 `tests/features/<feature>/`。
2. **≥1 个计数器断言**：例如"滚动 100px 后 `tiles_rastered == 2` 且 `text_shapes == 0`"。它比时间断言稳定，能抓住"悄悄退化成全量重绘"。
3. **≥1 个错误路径测试**：注入分配失败 / 字体损坏 / 后端降级，断言引擎不崩且给出正确降级。
4. **1 个基准或"为何不需要基准"的说明**：走热路径的 feature 必须在 `bench/` 里有对应 case。
5. **devtools 可见**：新增的可调参数/缓存/状态必须有计数器或面板，否则无法排查线上问题。

配套工具：
- `nqtest --create-feature <name>`：一键生成上述骨架（含 golden 占位与基准桩）。
- `--update-golden` 走"审核目录"流程：生成 `*.actual.png` + diff，人工确认后合入，防止"改坏了就更新黄金图"。
- 黄金图统一由 **CPU 参考光栅器**生成，GPU 结果比对用容差 + 结构掩码。

## 4. 回放（record / replay）

```bash
nq_rec --app examples/mail --out trace.nqr      # 录制：输入事件 + 时钟 + 配置 + 字体版本
nq_replay trace.nqr --check-frames              # 回放：逐帧 Scene 哈希 + 计数器 + 末帧图像
```

- 回放是**确定性**的：`DeterministicClock` + 固定 `Rng` + 固定字体/引擎版本哈希；任何一项不匹配直接报错而不是"悄悄给个近似结果"。
- 用途：bug 复现（用户发一个 200 KB 的 `.nqr` 比录屏有用）、性能回归定位（帧 X 的布局耗时）、跨平台差异定位（同一 trace 在 Win/mac/Linux 的哈希差异点）。
- 回放不依赖真实 GPU 也能跑（`NullDevice` + CPU 光栅 + 命令流哈希），所以能在任意 CI 机器上稳定跑。

## 5. 性能度量：三层指标

| 层 | 指标 | 工具 | 阈值方式 |
|----|------|------|----------|
| **微** | ns/op、cycles/op、Mpix/s、glyphs/s | `bench/micro/*`，`nq_bench --micro` | 相对基线比值 + 噪声地板 |
| **宏** | B1–B6 场景的帧时间 P50/P99、掉帧数、瓦片命中率、内存 | `bench/scenarios/*` | 预算文件 + 回归门 |
| **产品** | 冷启动、输入延迟、jank 率、RSS、GPU 显存、二进制体积、能耗 | `nq_bench --product` + CI 产物分析 | 预算文件 + 发布门禁 |

**行为计数器（与时间无关的可断言量）**——每个子系统必须暴露：

| 计数器 | 单位 | 用途 |
|--------|------|------|
| `layout.nodes_visited` / `layout.cache_hits` | 个 | 增量布局是否正确工作 |
| `style.recomputes` | 个 | 样式失效范围 |
| `text.shapes` / `text.shape_cache_hits` / `text.glyphs_rasterized` | 个 | 整形/字形缓存 |
| `raster.tiles_rastered` / `raster.drawcalls` / `raster.pixels_shaded` | 个 / 像素 | 瓦片复用与批处理是否退化 |
| `gpu.uploads_bytes` / `gpu.pso_compiles` | 字节 / 个 | 上传与管线编译抖动 |
| `alloc.bytes_peak` / `alloc.count` | 字节 / 个 | 内存回归 |

统一注册到 `nq::diag::Counter`（名字、单位、类型），自动导出：devtools 面板、`--stats-json`、trace 事件（可对齐火焰图）。> 规则：**没有计数器的子系统不允许合并性能相关改动。**

## 6. 基准方法论（避免自欺）

1. **固定工作负载**：场景数据（文本、图像、图元数量）进仓库，不依赖系统字体/网络。
2. **预热 + 多次 + 分布**：报告 P50/P90/P99/max，不报告单次最好值；报告样本数与方差。
3. **反优化消除**：微基准用 `DoNotOptimize` + 校验输出；宏基准用真实帧循环。
4. **频率漂移校准**：同一进程内跑一个固定的"校准算子"，用它的耗时归一化，消除 Turbo/热降频导致的 ±15% 噪声。
5. **机器固定**：性能门禁只在自托管 bare-metal runner 上跑（记录 CPU 型号、内存、GPU 驱动、内核版本），云端共享 runner 只做"是否有 2× 级退化"的粗筛。
6. **温度/电源**：夜间跑，报告里附带频率与温度；连续 3 次取中位数。
7. **不跨机器比较绝对数字**：只比较同一 runner、同一 commit 基线。

## 7. 预算即代码（budget as code）

```json
// bench/budgets/desktop.json
{ "frame_ms":      { "B1_p99": 8.3,  "B3_p99": 2.0, "B5_p50": 16.6 },
  "startup_ms":    100,
  "binary_bytes":  8388608,
  "rss_mb":        120,
  "jank_per_1000_frames": 0,
  "counters": { "B1": { "raster.tiles_rastered": 60, "text.shapes": 0 } } }
```

- `nq_bench --check-budget bench/budgets/desktop.json` 在 CI 失败即阻断合并。
- 计数器上限是"行为预算"：它抓住的是"算法退化成 O(n²)/全量重绘"，这类问题在快机器上时间看不出来，但计数器立刻爆。
- 预算随平台分档：`desktop / laptop / embedded / mobile / wasm`。

## 8. 回归防线

| 机制 | 说明 |
|------|------|
| PR 门禁 | L0–L4 快子集 + B1/B3 冒烟基准（相对基线 >3% 失败） |
| 夜间全量 | 全部 conformance + L5 + B1–B6 + 内存长跑 + fuzz 30 min + 三平台矩阵 |
| 性能历史 | `bench/history/*.json` 入库，自动生成趋势图（自研 HTML 报告，无外部依赖） |
| 自动二分 | 夜间发现退化 → 在 PR 合并区间内自动 bisect，输出可疑 commit |
| 抖动治理 | 任何 `sleep`、真实网络、真实时间、线程调度依赖在 CI 中静态检测并拒绝；flaky 测试进隔离区并计数 |
| 覆盖率 | 核心模块分支覆盖 ≥ 85%（llvm-cov，工具链自带），平台层 ≥ 50%，新增代码不得降低整体 |
| 内存 | 每次 CI 跑 `alloc.bytes_peak` 与泄漏检测；夜间跑 24h 长跑看 RSS 曲线斜率 |

## 9. 平台相关的可测性

| 项 | 策略 |
|----|------|
| GPU 差异 | 命令流哈希（`NullDevice`，跨后端可比的抽象命令）+ 图像容差比对；驱动黑名单用例显式标注 |
| IME | 可脚本化的 IME 注入接口（`platform::Ime::inject_preedit/commit`）+ 各平台真实输入法手工清单 |
| 无障碍 | 语义树快照（JSON）+ 每平台屏幕阅读器实测清单（NVDA/VoiceOver/Orca/TalkBack） |
| 输入延迟 | 软件估算（事件时间戳 → `present_wait` 时间戳，取分布）+ 可选硬件光敏探针（实验室） |
| 能耗 | macOS `powermetrics`、Windows `EnergyEstimation`、Linux RAPL；仅夜间 |

## 10. 与路线图的挂钩

- M0 必须交付：`nqtest`、golden runner、`DeterministicClock/MemoryFs/InlineScheduler`、计数器注册表、`bench/` 骨架。**没有这些不开工写 feature。**
- 每个阶段的 gate（见 [06-roadmap.md](06-roadmap.md) §2）都以本文件的指标表达，不使用"感觉变快了"。
- 1.0 的对外承诺附一份可复现的基准报告：场景、数据、机器、命令、原始 JSON。

# NextQt

> 下一代原生 UI 渲染引擎。目标：在 **帧时间 / 文本质量 / 原生融合 / 跨平台一致性** 四个维度同时超过 Qt 与 Flutter。

## 三条硬约束

| 约束 | 决定 |
|------|------|
| **语言** | C++ 单一实现。基线 **C++20**，可用 `NQ_HAS_CXX23` 渐进启用 C++23（`std::expected` / `std::print` / `flat_map`）。不叫 "C++21"——标准名是 C++20 / C++23，本机 g++11.4 与 clang14 在 `-std=c++2b` 下均**缺少** `<expected>`/`<format>`，所以自研 `nq::Result`/`nq::fmt`，升级到 gcc13+/clang16+/MSVC19.35+ 后一个头文件切换 |
| **零第三方运行时依赖** | 链接期只允许 C++ 标准库 + 操作系统/GPU 系统 ABI。文本、Unicode、字体解析、图像解码、GPU 抽象、容器、测试框架全部自研。第三方**只允许**出现在构建期工具与 **CI 对照 oracle** 中（不进入产物） |
| **跨平台** | Windows 10+ / macOS 11+ / Linux(X11+Wayland) 为一等公民；Android / iOS / WASM 同架构预留，第二阶段落地。核心层（`unicode/text/layout/dl/raster`）必须能在 **无平台、无 GPU** 的 headless 模式下编译运行 |

## 一句话定位

自带 GPU 光栅器、增量布局、**确定性像素**的声明式 + 响应式原生 UI 引擎：
像 Flutter 一样像素可预测，像 Qt 一样"长在系统里"，比两者都快，而且**可以静态链接进任何 C++ 程序**。

## 六个核心赌注

| # | 赌注 | 击败谁 | 机制 |
|---|------|--------|------|
| 1 | **分层增量** | Flutter（每帧 rebuild+layout+paint 串行） | 信号级细粒度更新 + 布局 fragment 缓存 + 显示列表自动差分与瓦片复用。**默认开启**，用户不用手加 `RepaintBoundary` |
| 2 | **光栅质量与速度同时要** | Qt Raster（CPU 慢）/ Flutter（MSAA 显存 + conflation artifact） | 四层光栅：解析式覆盖 → 小路径 CPU 覆盖率缓存 → compute 覆盖 → MSAA 兜底，按成本模型自动选 |
| 3 | **文本一等公民** | 两家被骂最狠处 | 自研 Unicode/整形/字体栈 + 自带文本编辑内核（grapheme、BiDi caret、IME 组合、COLRv1） |
| 4 | **原生融合，不是一块画布** | Flutter | 原生菜单/右键菜单/对话框、完整 IME 协议、无障碍树增量同步、双向平台视图嵌入、真多窗口 |
| 5 | **确定性跨平台像素** | 两家都做不到 | 定点布局（`LayoutUnit` i32/64）+ 自研整形 + **随包字体**；系统字体差异不再改变布局。golden 测试跨 OS 逐像素可比 |
| 6 | **零依赖可审计 / 可静态链接** | Qt 的 LGPL 陷阱、Flutter 的 Dart VM | 单许可证（Apache-2.0 OR MIT）、无 LGPL、可裁到 8MB；**UI 执行层是端口**：默认直接 C++（零边界开销），需要多语言/插件时切换为 WASM 适配器 |

## 明确不做

- **不做完整 Web 布局**：只做 `flex / grid / stack / scroll / inline-text` 子集；CSS 语法仅作可选糖，编译到同一套 style 结构体。不做 float、inline-block、table 布局。
- **前两年不做 3D 场景图**：显示列表预留 3D 通道（图表 `DrawVertices`），3D 由上层通过纹理/共享 GPU 纹理接入。
- **第一天不自研跨后端着色器编译器**：第一年 Vulkan+GLSL；Metal/D3D12 用手写等价 shader（shader 总数 ~20，可控）；第三年再统一前端。
- **不承诺 GPU 后端逐位一致**：以 CPU 参考光栅器为像素真值，GPU 后端允许 ≤1 LSB / 结构性差异（覆盖率差 ≤ 2%）。

## 依赖政策

| 类别 | 允许 | 例子 |
|------|------|------|
| 运行时链接 | C++ 标准库、OS ABI、GPU 驱动 ABI | libc++, Vulkan/Metal/D3D12、X11、Wayland、Win32、Cocoa、Android NDK |
| 平台系统库（仅 `src/platform/*` 内） | 发行版必备系统库，必须被 `nq_platform_*` 封装，核心层不可见 | fontconfig（仅枚举字体路径）、dbus（仅托盘/menu）、CoreText（仅 iOS 取系统字体数据） |
| 构建期工具 | 编译器、DXC、CMake、脚本 | clang/gcc/msvc, dxc, cmake, 自研 ucdgen |
| 仅测试/CI | 作为**对照 oracle**，绝不进产物 | HarfBuzz（整形对照）、FreeType（字形位图对照）、ICU（BiDi/换行对照）、resvg（SVG 黄金图） |
| 禁止 | 任何**未经端口封装**的第三方库进入产物 | Skia、HarfBuzz、ICU、FreeType、SDL、GLFW、wgpu、fmt、Catch2 直接 `#include` |
| **例外通道** | 必须依赖时，**只能**做成可替换适配器（C ABI、能力探测、可降级、过 conformance） | 见 [docs/07-adapters.md](docs/07-adapters.md) |

> 这条政策的商业价值：无 LGPL/LGPL-linking 义务、无 FTL 署名要求、可闭源静态链接、可审计（供应链攻击面 ≈ 系统 API）、WASM 体积可控。
> 代价：整形一致性与字体解析是最大的工期风险，见 [docs/04-text-unicode.md](docs/04-text-unicode.md) 的分级策略。

## 北极星指标

| 指标 | 目标 | 对标 |
|------|------|------|
| 冷启动 → 首帧（中端笔记本 4K） | < 100 ms | Qt6 150–300 ms；Flutter desktop 200–400 ms |
| 5k 节点、1% 脏区/帧 UI 线程 | < 2 ms P99 | Flutter 4–8 ms |
| 120 Hz 滚动 20 万字形掉帧 | 0（144 Hz 亦 0） | 两家 1–10% |
| 点击 → 像素改变 | < 16 ms（目标 8 ms） | 两家 1–2 帧 |
| 引擎 + 20 控件静态链接 | < 8 MB（release, LTO） | Flutter 20–40 MB；Qt6 ≥15 MB |
| 跨 OS 渲染同一 app | golden 逐像素一致（CPU 真值） | 两家均因系统字体而漂移 |
| feature 可测性 | 每 feature ≥3 golden + 计数器断言 + 回放 | 无对应要求 |
| 原生菜单/IME/无障碍 | ≥ Qt | Flutter 明显落后 |

## 文档索引

| 文档 | 内容 |
|------|------|
| [docs/01-architecture.md](docs/01-architecture.md) | 分层、模块划分、线程模型、帧数据流、C++ 工程约定、内存/确定性、构建与热重载 |
| [docs/02-render.md](docs/02-render.md) | 显示列表 IR、四层光栅、字形渲染、模糊/阴影、颜色管理/HDR、瓦片与 damage、自研 GPU HAL |
| [docs/03-ui.md](docs/03-ui.md) | 信号系统、Element/协调器、布局引擎、样式与主题、动画、滚动、输入、无障碍 |
| [docs/04-text-unicode.md](docs/04-text-unicode.md) | UCD 表生成、规范化、分段、BiDi、换行、OpenType 布局、字体格式、编辑内核、oracle 策略 |
| [docs/05-platform.md](docs/05-platform.md) | 平台矩阵、窗口/合成/输入/IME/无障碍/DnD/菜单/多窗口、移动端与 WASM、系统字体与图像解码 |
| [docs/06-roadmap.md](docs/06-roadmap.md) | 分阶段计划（三轨并行）、基准套件、ADR、风险与 kill criteria、待拍板问题 |
| [docs/07-adapters.md](docs/07-adapters.md) | 可插拔后端：端口/适配器边界、C ABI 契约、粒度红线、选择与降级、许可证隔离、端口清单 |
| [docs/08-testing-perf.md](docs/08-testing-perf.md) | 可测试性注入点、测试金字塔、feature 可测的 DoD、录制回放、三层性能指标、计数器、预算即代码、回归防线 |
| [docs/09-ui-runtime.md](docs/09-ui-runtime.md) | UI 执行层端口：native C++ 与 WASM 双适配器、统一 C ABI 契约、规范 IR 与等价性测试、沙箱配额、热重载、阶段与风险 |

## 诚实的预期

Qt 有 30 年控件与平台积累，Flutter 有 Google 的跨端网络效应。"超过"必须**按战场定义**：

1. **桌面优先、要原生质感的现代应用**——Flutter 的像素一致性在桌面反成缺点（字体/菜单/IME/无障碍），Qt 的 DX 停在 2010 年。
2. **高刷新率专业工具**（DAW / 行情 / CAD / 设计）——两家在 120 Hz 上都不稳。
3. **嵌入式 HMI / 受限内存**——无 GC、无 VM、确定性内存（Qt 强但老，Flutter 需要 VM 且内存不可控）。

第一阶段只打第 1 个战场。3–4 名资深工程师，18–24 个月到可用的 1.0。见 [docs/06-roadmap.md](docs/06-roadmap.md)。

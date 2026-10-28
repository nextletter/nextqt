# 01 · 架构

## 1. 技术栈声明

| 项 | 决定 | 理由 |
|----|------|------|
| 语言 | C++，**基线 C++20**，`NQ_HAS_CXX23` 渐进启用 C++23 | 本机 g++11.4 / clang14 在 `-std=c++2b` 下无 `<expected>`/`<format>`；C++20 已提供 concepts / spans / ranges / constexpr / por / 协程 / latch |
| 标准库 | `std` 允许；不用 `std::iostream`、`std::regex`、`std::locale`、`std::function`(热路径) | 体积、局部性、可预测性 |
| 异常 / RTTI | **全引擎 `-fno-exceptions -fno-rtti`** | 体积、无隐藏分配、跨 ABI 稳定；错误走 `nq::Result<T>` |
| 编译产物 | 3 个静态库 + 1 个 C ABI 动态库 + 1 个测试二进制 | 静态链接可裁到 8MB；C ABI 供其他语言 |
| 目标平台 | Win10+/macOS11+/Linux(X11, Wayland) 一等；Android/iOS/WASM 二阶段 | 见 05-platform |
| 编译器矩阵 | MSVC 19.3x、clang 15+、gcc 12+（CI 三套） | 早期用 g++11/clang14 也能编，但 CI 用新版 |

## 2. 分层

```
┌──────────────────────────────────────────────────────────────────────┐
│ L5 集成层    src/abi (C ABI)   src/app (C++ 门面)   src/devtools      │
├──────────────────────────────────────────────────────────────────────┤
│ L4 渲染层    src/dl  →  src/raster(CPU)  │  src/gpu(HAL+vk/mtl/d3d12) │
│              src/compositor (层树/瓦片/damage/present)                │
├──────────────────────────────────────────────────────────────────────┤
│ L3 UI 核心   src/style → src/layout → src/scene(信号/Element)         │
├──────────────────────────────────────────────────────────────────────┤
│ L2 文本栈    src/unicode → src/font → src/text(整形) → src/edit       │
├──────────────────────────────────────────────────────────────────────┤
│ L1 平台抽象  src/platform/*  (window/surface/input/ime/a11y/menu/...) │
├──────────────────────────────────────────────────────────────────────┤
│ L0 基础      src/base (Result/容器/字符串/arena/数学/哈希/日志/时钟) │
└──────────────────────────────────────────────────────────────────────┘
```

**依赖方向单向向下，L0–L4 不得 include `src/platform/*`。**
核心层必须能这样构建并通过全部 golden 测试：

```bash
cmake --preset headless -DNQ_PLATFORM=none -DNQ_GPU=none   # 纯 CPU，无 OS 头文件
ctest --preset headless                                     # 逐像素黄金图
```

这条约束是"零依赖 + 可 WASM + 可服务端渲染"的技术前提，不是洁癖。

## 3. 模块与目录

```
nextqt/
├── CMakeLists.txt  CMakePresets.json  cmake/{flags,sanitizers,codegen}.cmake
├── tools/
│   ├── ucdgen/        # 构建期：UCD/UAX 数据 → 压缩表 (.inc)
│   ├── shadergen/     # 构建期：着色器变体生成 / SPIR-V 校验
│   ├── fontdump/      # 调试：sfnt 表结构、GSUB/GPOS 反汇编
│   ├── dlreplay/      # 调试：显示列表录制/回放 → CPU 光栅
│   └── nqtest/        # 自研测试框架 + 基准 + golden runner（零依赖）
├── src/
│   ├── base/  unicode/  font/  text/  edit/
│   ├── style/ layout/  scene/  dl/
│   ├── raster/ gpu/    compositor/
│   ├── a11y/  platform/  abi/  app/  devtools/
├── data/        # 随包字体(SIL OFL)、UCD 源数据、着色器源
├── tests/ bench/ examples/
```

CMake target 命名：`nq::base nq::unicode nq::font nq::text nq::edit nq::style nq::layout nq::scene nq::dl nq::raster nq::gpu nq::compositor nq::a11y nq::platform_win32 … nq::capi`。

## 4. 线程模型

| 线程 | 职责 | 不可做的事 |
|------|------|-----------|
| **平台线程** | OS 事件循环、窗口消息、IME 事件、菜单/DnD 原生调用 | 不做布局、不碰 GPU |
| **UI 线程** | 输入分发 → 信号传播 → build → style → layout → record → 发布 `Scene` | 不做光栅、不 present、不阻塞 IO |
| **合成线程** | 合成动画重采样、滚动、层树、瓦片决策、damage、提交、present 定时 | 不触发 UI rebuild |
| **光栅池** (0..N-1) | 瓦片光栅（GPU 次级命令缓冲 / CPU 扫描线） | 不写 UI 状态 |
| **IO 池** | 字体 mmap、图片解码、协程任务 | 不持锁调用 UI |

线程优先级：合成线程 > UI 线程 > 光栅池 > IO 池。**热路径零互斥锁**：线程间只用 SPSC 无锁队列 + `std::atomic`（release/acquire），跨线程释放对象走"延迟回收队列"（在下一帧的已知安全点批量销毁）。

GPU 同步用 timeline semaphore（Vulkan）/ `MTLSharedEvent`（Metal）/ fence+`SetEventOnCompletion`（D3D12），不用 `glFinish`，不用隐式全设备等待。

## 5. 帧数据流与时间线

```
        ┌──────────── UI 线程 ────────────┐   ┌──── 合成线程 ────┐   ┌─ GPU ─┐
frame N │ input → signals → build →       │   │ 动画重采样        │   │       │
        │          layout → record        │──▶│ 层树/damage       │──▶│ 光栅  │
        │  ↓                              │   │ 瓦片键比对        │   │ 合成  │
        │ Scene(N)  (immutable, arena)    │   │ present 定时      │   │present│
        └─────────────────────────────────┘   └───────────────────┘   └───────┘
                    ▲                               │
                    └──── Scene 三缓冲，UI 不被 GPU 阻塞 ─────┘
```

- `Scene` 是**不可变快照**（显示列表 + 层树 + 资源表 + 文本 blob 表），producer/consumer 通过索引交换，永不原地修改。
- **合成线程可以在 UI 线程卡顿时继续出帧**：transform / opacity / clip / 滚动 / 滤镜参数动画默认下沉到合成线程（"compositor-driven animation"）。这是相对 Flutter 的关键差异——Flutter 的动画 tick 在 UI isolate，UI 一卡动画就抖。
- 帧预算记账（`NQ_FRAME_STATS`）：build/style/layout/record/raster/composite/present 各段耗时，P50/P99 环形缓冲，devtools 读。

## 6. C++ 工程约定

```cpp
namespace nq {                       // 子模块 nq::text, nq::layout …
using namespace std::literals;

// 类型 CamelCase，函数/变量 snake_case，成员尾下划线，常量 kCamel
struct Element;  void record(const Scene& s);  int ref_count_;

// 错误：无异常
nq::Result<GlyphId> lookup(Cp cp);
NQ_TRY(auto g, lookup(cp));          // 宏展开为 early-return，零开销

// 所有权：明确三类
std::unique_ptr<FontFace>          // 唯一所有权
nq::Ref<Image>                     // 侵入式引用计数（非原子，单线程域）
nq::Arc<DisplayList>               // 侵入式原子引用计数（跨线程共享不可变数据）
Raw*                               // 仅非拥有，参数用 T& / nq::Span<T>

// 内存：per-frame / per-document arena
auto& a = frame_arena();           // 每帧 reset，O(1)
nq::Vec<Element, FrameAlloc> v(a); // 自定义分配器容器

// 回调：热路径不用 std::function
void on_click(nq::FunctionRef<void()>);   // 非拥有
nq::MoveOnlyFunction<void()>  cb;         // 小缓冲，>32B 才堆分配

// 热路径类型
nq::Span<const PaintOp>;  std::string_view;  std::u8string_view(文本用 UTF-8 存)
```

硬性规则（CI 用 clang-tidy 强制）：
- 头文件 `#pragma once`；禁止 `using namespace` 出现在头文件；禁止头文件里定义可变全局。
- 禁止裸 `new/delete`（走 `nq::make_ref` / arena）；禁止 `shared_ptr`（语义不清、原子开销）。
- 所有可能失败的操作返回 `Result`，`NQ_ASSERT` 只用于不变量（release 编译为空，checked 构建中断言并 dump）。
- 禁止 `reinterpret_cast` 与 strict-aliasing 违规（字体/网络字节序解析一律走 `nq::read_be16` 等显式函数）。
- 数值：布局用 `LayoutUnit`（定点），光栅用 `f32`，禁止 `-ffast-math`；golden 构建加 `-ffp-contract=off`。

## 7. 内存策略

| 区域 | 生命周期 | 预算（默认） |
|------|----------|--------------|
| Frame arena | 单帧 | 8–32 MB |
| Document arena | 文档/窗口存活期 | 按内容 |
| Glyph mask atlas | LRU，可驱逐 | 8 MB (A8) / 16 MB (彩色) |
| 瓦片缓存 | LRU 按屏幕面积倍数 | 1.5× 屏幕 |
| 图像解码池 | LRU，按字节 | 64 MB |
| 整形结果缓存 (TextBlob) | LRU | 4 MB |

- 所有缓存实现统一 `Cache<Key, Value, Budget>`：命中率 / 驱逐次数 / 峰值暴露给 devtools。
- 平台内存压力回调（Windows `CreateMemoryResourceNotification`、macOS `DISPATCH_SOURCE_TYPE_MEMORYPRESSURE`、Linux cgroup/PSI）→ `trim_to(level)`。
- 目标：中等复杂度应用 idle RSS < 60 MB，GPU 显存 < 120 MB；嵌入式档提供 `NQ_MINIMAL`（无图像解码、无 HDR、无模糊）。

## 8. 确定性（跨平台像素一致的根基）

1. **布局定点化**：`LayoutUnit = int32_t`，1 单位 = 1/64 px，加法饱和，乘除显式舍入。字体度量从 f32 转 LU 只在入口处舍入一次。
2. **禁止把指针/哈希表迭代序写进输出**：所有影响绘制顺序的集合用有序容器或显式 id 排序；id 单调递增而非地址派生。
3. **文本确定性**：自研整形 + 随包字体（`data/fonts/`，SIL OFL）→ 同一版本引擎在不同 OS 上产出相同 run/advance/glyph。
4. **光栅确定性**：CPU 光栅器的浮点路径禁用 FMA/收缩（`-ffp-contract=off`），并行按 tile 划分（结果与线程数无关）。
5. **时间**：一切时钟走 `nq::Clock`，存在 `deterministic` 模式（固定步进），用于录制回放与黄金测试。

## 9. 构建

```bash
cmake --preset dev          # Debug + ASan/UBSan，Ninja 若可用
cmake --preset rel          # -O2 + LTO + -fno-exceptions -fno-rtti
cmake --preset golden       # 确定性光栅（-ffp-contract=off）
cmake --preset ci-werror    # -Wall -Wextra -Wconversion -Werror + clang-tidy
```

- 构建期代码生成：`ucdgen`（Unicode 表）、`shadergen`（shader 变体）、`fontdump`（仅调试）。生成物进 `build/gen/`，不提交。
- 编译时间预算：8 核 clean build < 3 min；核心模块支持 `NQ_UNITY_BUILD` 快速迭代。
- 测试：自研 `tools/nqtest`（注册式、无异常、支持 fixture + golden + 基准），不引入 Catch2/gtest。
- CI 必须跑：ASan/UBSan/TSan、libFuzzer（字体解析、PNG 解码、显示列表反序列化）、golden、benchmark 回归门（>5% 退化即失败）。

## 10. 热重载

UI 代码编译成 `libapp_ui.so` / `app_ui.dll` / `app_ui.dylib`，导出单一入口：

```cpp
extern "C" NQ_EXPORT uint64_t nq_app_abi_hash();               // 与引擎头文件哈希比对
extern "C" NQ_EXPORT nq::Element nq_app_build(nq::Ui& ui, void* state);
extern "C" NQ_EXPORT bool     nq_app_migrate(void* old_state, void* new_state);
```

- 引擎持 `dlopen`/`LoadLibrary` 句柄；文件 mtime 变化 → 编译 → 加载 → 用信号的稳定 path-id 迁移状态 → 下一帧重建。
- **仅状态层热重载**：不重载引擎自身（避免 GPU 资源/线程状态损坏）。改了引擎代码就重启，`<200ms` 的冷启动让这件事可以接受。
- Linux 需 `-fvisibility=hidden` + 版本脚本避免符号劫持；Windows 必须真正 `FreeLibrary`（要求 UI 库无静态含状态的单例，用 `NQ_NO_STATIC_STATE` 编译选项强制）。

## 11. 与其他语言 / 宿主程序

- 产物 `libnextqt.so` 只暴露 C ABI（`src/abi`）：版本化结构体（首字段 `uint32_t size`）、句柄、错误码。**STL 类型禁止跨界**。
- C++ 用户拿 `nq/nq.hpp`（RAII 包装，header-only）+ 静态库。
- 其他语言（Swift/Kotlin/TS/Python/C#）由社区或我们后续做薄绑定，语义以 **UI IR**（可序列化的 element 描述）为契约——这是语言中立性的真正落点。
- 嵌入式：`nq_attach_to_native_view(void* hwnd_or_nsview_or_xid)`，把引擎当作宿主程序里的一个子视图；反向 `nq_platform_view(handle)` 把宿主原生控件合成进来（纹理或层直挂）。

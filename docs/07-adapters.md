# 07 · 可插拔后端（适配器模式）

> 原则：**能用自研就用自研；不得不依赖第三方时，第三方只能存在于适配器里，并且必须可替换、可卸载、可对照、可降级。**
> 这条规则把"零依赖"从口号变成可执行的架构约束。

## 1. 边界：什么可以插，什么绝对不能插

| 分类 | 内容 | 可否替换 |
|------|------|----------|
| **内核不变量** | 显示列表 IR (`dl`)、`LayoutUnit` 与布局引擎、信号/Element 树、瓦片与 damage 调度、`Scene` 快照 | **绝不**。这些决定了"跨平台像素一致"与"增量正确性"，一旦可替换，承诺即失效 |
| **可替换端口** | 整形、字体解析、Unicode 分段/BiDi/换行、图像解码、GPU HAL、窗口/输入、无障碍桥、剪贴板/托盘 | 允许，见 §3 的粒度约束 |
| **适配器** | 内置自研实现 与 第三方实现，共用同一端口 | 见 §2 |

## 2. 两种适配器形态

### 2.1 内置适配器（编译期选择，零开销）

自研实现与"必须编译期确定"的后端（GPU HAL、窗口系统）走这条：

```cpp
// nq/port/gpu.h
namespace nq::port {
struct GpuBackend {                     // 端口：只有粗粒度方法，无 per-draw 虚调用
  static constexpr std::string_view name;
  static Result<std::unique_ptr<Device>> create(const DeviceDesc&);
};
using ActiveGpu = NQ_GPU_BACKEND;        // 编译期选择：vk | mtl | d3d12 | gles | webgpu
}
```

- 通过 CMake 选项 `-DNQ_GPU_BACKEND=vk` 选定，**链接期直接绑定**，`device->draw()` 内联展开，无间接跳转。
- 同一份代码在 CI 中按后端矩阵各编译一次（`vk`/`mtl`/`d3d12`/`gles`），保证行为一致。

### 2.2 外挂适配器（运行时加载，C ABI）

整形、字体解析、图像解码、BiDi/分段这类"每 run / 每图元 / 每图像"粒度、且第三方确实可能更强的部分，走动态适配器：

```c
/* nq_port.h —— 纯 C，无 C++ 类型跨越 ABI（MSVC/GCC/Clang 可互操作） */
#define NQ_PORT_ABI_VERSION 3

typedef struct nq_host_api {           /* 引擎提供的基础设施，避免适配器自带一套 */
  uint32_t struct_size;
  void*  (*alloc)(void* ud, size_t size, size_t align);
  void   (*free)(void* ud, void* ptr);
  void  *(*persist_open)(void* ud, const char* key, size_t key_len); /* 磁盘缓存 */
  void   (*log)(int level, const char* msg, size_t len);
  void   (*task_post)(void* ud, void (*fn)(void*), void* arg);        /* 线程池 */
  int    (*thread_count)(void* ud);
} nq_host_api;

typedef struct nq_shape_backend {
  uint32_t       struct_size;
  uint32_t       abi_version;
  const char*    name;                 /* "own" | "harfbuzz" | ... */
  uint64_t       capabilities;         /* 位掩码：变体字体/COLRv1/竖排/… */
  int            deterministic;        /* 是否可承诺逐像素一致（关掉 golden 时要声明） */
  int            thread_safe;          /* 0 = 引擎串行化调用 */

  int  (*init)(const nq_host_api* host, const void* cfg, void** out_self);
  void (*shutdown)(void* self);                       /* 必须释放全部资源 */
  int  (*shape)(void* self, const nq_shape_input*, nq_shape_output*);
  int  (*glyph_metrics)(void* self, nq_glyph_id, nq_glyph_metrics_out*);
  int  (*query_capabilities)(void* self, const nq_font_blob*, uint64_t* out_caps);
} nq_shape_backend;

NQ_EXPORT const nq_shape_backend* nq_shape_backend_entry(uint32_t abi_version);
```

硬性规则：
- **端口结构体首字段 `struct_size`**，引擎按自己的版本读取，新增字段只追加 → 新旧适配器可共存。
- **不传 C++ 类型**（`std::string_view`、异常、`std::function` 一律禁止跨界），失败用返回码 + `nq_last_error()`。
- 适配器**必须使用 `nq_host_api` 的分配器**，否则内存记账、arena、上限控制全部失效。
- 适配器声明的 `thread_safe=0` 时，引擎在端口层加一次串行化（不污染调用方）。
- `deterministic=0` 时，引擎在启动日志与 release 元数据里标注"跨平台像素一致承诺降级"，绝不静默。

## 3. 粒度规则（性能红线）

> **可替换的粒度必须 ≥ 1 µs。** 每像素、每 drawcall、每个字形提前退出的路径一律编译期绑定。

| 端口 | 调用粒度 | 允许运行时替换 | 原因 |
|------|----------|----------------|------|
| 文本整形 | 每 run（µs 级） | ✅ | 虚调用占比 < 0.01% |
| 字体解析 | 每字形（有缓存） | ✅ | 缓存命中后不进适配器 |
| Unicode 分段/换行 | 每段落 | ✅ | |
| BiDi | 每段落 | ✅ | |
| 图像解码 | 每图像（ms 级） | ✅ | |
| 无障碍桥 | 每语义更新（ms 级） | ✅ | |
| 剪贴板/托盘/对话框 | 每用户操作 | ✅ | |
| **GPU HAL** | **每 drawcall（ns 级）** | ❌ 编译期选择 | 间接跳转 + 无法内联的状态设置会直接吃掉 10–20% |
| **CPU 光栅器** | **每像素** | ❌ 内核不变量的底座 | 它是像素真值 |
| 窗口/输入 | 每事件 | ⚠️ 仅移植辅助（SDL/GLFW），产品构建强制原生 | 事件语义丢失（IME/DnD/DPI） |

## 4. 选择与降级

```cpp
// 启动时一次性决定，运行中不允许切换（切换必须重建全部文本/字形缓存）
Engine::Config cfg;
cfg.text.shape_backend  = "harfbuzz,own";   // 有序链：第一个通过能力探测的胜出
cfg.text.decode_backend = "own";            // 强制自研
cfg.gpu.backend         = "vk";             // 编译期已定，这里只做一致性校验
```

- 环境变量/配置文件/代码三处入口，优先级：代码 > 配置 > 环境变量。
- **能力探测失败即降级**（版本不匹配、缺字体表、初始化返回错误），降级必须打日志并在 devtools 显示"当前后端 + 降级原因"。
- **一个子系统在一个进程内只能有一个活跃后端**：混用会导致度量/断行不一致、布局漂移（这是最隐蔽的 bug 来源）。
- 切换后端 = `Engine::shutdown()` + 重建；不做运行时热切换。

## 5. 配套测试（适配器准入门槛）

每个适配器必须通过同一套 conformance 测试，否则不允许出现在 release 构建里：

| 端口 | 准入门槛 |
|------|----------|
| 整形 | Unicode 官方 conformance 100%；对 `own` 后端的字形序列一致率：Tier A ≥99.9%，Tier B ≥98%（第三方适配器对 `own` 反向比对，任意一方为基准都要过阈值） |
| 字体解析 | 轮廓/度量与 `own` 逐字节一致（允许 hint 差异） |
| Unicode | 官方 `BidiTest`/`LineBreakTest`/`GraphemeBreakTest` 100% |
| 图像解码 | 与 `own` 解码结果 PSNR ≥ 55 dB（无损格式必须逐像素相等） |
| 无障碍 | 每平台屏幕阅读器实测清单 |
| 所有端口 | 线程安全声明与实际行为一致（TSan 跑）；fuzz 10 分钟无崩溃；内存全部经 host allocator（泄漏检测） |

CI 矩阵：`adapter ∈ {own, harfbuzz, icu, fribidi, libjpeg-turbo, ...} × sanitizer`，夜间跑全量。

## 6. 许可证隔离（这条是"可替换"的真正动机）

| 情况 | 要求 |
|------|------|
| 宽松许可（MIT/Apache/BSD/zlib/OFL） | 可静态链接，但**仍必须通过端口**，保证未来可换 |
| 弱 copyleft（LGPL，如 FriBidi） | **只能动态链接为独立 `.so`**，提供替换能力与源码，且不得与静态库合并；适配器本身用 Apache-2.0 以避免"衍生作品"争议 |
| 强 copyleft（GPL/AGPL） | **禁止**出现在任何构建配置里（CI 白名单校验依赖清单） |
| 平台系统库（WIC/ImageIO/CoreText/fontconfig） | 允许，但只在平台层/适配器内，且必须有自研替代路径 |

配套工具：`tools/deps-audit`（自研）在 CI 里扫描 `ldd`/导入符号/许可证清单，任何一个"未登记为适配器的第三方符号"直接失败。

## 7. 当前端口清单

| 端口 | 内置默认 | 可选第三方适配器 | 状态 |
|------|----------|------------------|------|
| `shape` | own（OT layout） | HarfBuzz | M1 起有 own，M5 加 HB 适配器（主要用于 CI oracle） |
| `font` | own（sfnt/CFF/变体） | FreeType | M1 own，M6 加 FT 适配器 |
| `unicode` | own（UCD 生成表） | ICU4C | M1 own，M7 加 ICU 适配器（含数据版本差异声明） |
| `bidi` | own（UAX#9） | SheenBidi / FriBidi(LGPL→动态) | M1 own |
| `image` | own PNG/JPEG/QOI | libjpeg-turbo / libwebp / 系统 WIC·ImageIO | M4 own，M9 加可选 |
| `gpu` | own vk/mtl/d3d12/gles | Dawn / ANGLE（编译期） | 编译期端口从 M3 就位 |
| `window` | own per-OS | SDL2 / GLFW（仅移植辅助） | M3 |
| `a11y` | own per-OS | AT-SPI2 / ATK | M7 |
| `clipboard` `menu` `dialog` | own per-OS | 平台库 | M3 起 |
| `decode_sandbox` | 无 | 子进程解码服务（隔离高风险解码） | M12 可选 |

## 8. 反模式（明确禁止）

- ❌ **在端口后面偷偷假设某个实现**：例如在 `layout` 里 `#include <hb.h>` 用字形 advance 做行内对齐。所有字体相关数值必须来自 `port::font`。
- ❌ **端口方法返回内部指针/引用**：必须写入调用方提供的输出结构。
- ❌ **适配器持有引擎对象**（`Element*`、`Scene*`）：适配器只能看到自己的 `self` 与 `nq_host_api`。
- ❌ **"临时"直接用某个库**：没有端口封装的一行 `#include`，在评审阶段就应该被拒。
- ❌ **按后端分支改变 UI 行为**：后端差异只允许出现在"质量/性能档位"，不允许出现在布局结果。

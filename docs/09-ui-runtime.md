# 09 · UI 执行层端口（native C++ / WASM 双适配器）

> 结论先行：**UI 执行层是一个端口，不是一种技术。** "直接 C++"和"WebAssembly"是同一端口的两种适配器，
> 契约相同、IR 相同、测试相同、可互换——符合 [07-adapters.md](07-adapters.md) 的适配器原则。

## 1. 位置

```
┌─ 应用 / UI 逻辑 / 插件 ──────────────────────────────────────────┐
│  port::ui_runtime 的两个适配器家族：                             │
│    A. native   : 直接 C++（编译期链接 / 动态库热重载）           │
│    B. wasm     : WebAssembly（AOT / 解释器 / 浏览器原生）        │
└───────────────────┬─────────────────────────────────────────────┘
                    │  统一契约 nq_ui_module + 规范 IR（sha 相同）
┌───────────────────▼─────────────────────────────────────────────┐
│  宿主（全 native）：style → layout → paint → 瓦片光栅 → 合成     │  ← 内核不变量，不可替换
└───────────────────┬─────────────────────────────────────────────┘
                    │  port::gpu / port::platform（编译期端口）
┌───────────────────▼─────────────────────────────────────────────┐
│  Vulkan / Metal / D3D12 / GLES / WebGPU + X11 / Wayland / Win32…  │
└─────────────────────────────────────────────────────────────────┘
```

**布局、文本、光栅、合成永远在宿主 native 侧。** 适配器只跑 UI 逻辑（状态 + 事件 + IR 生成），实测预算 < 1 ms/帧；因此即使 wasm 只有原生 65% 的性能，绝对损失仍 < 0.35 ms。这是"双适配器"能成立的前提，也是红线。

## 2. 端口契约（两种适配器都实现它）

```c
/* nq_ui_module.h —— 纯 C ABI，版本化，两端可跨编译器互操作 */
typedef struct nq_ui_module {
  uint32_t    struct_size;
  uint32_t    abi_version;
  const char* name;              /* "native_static"|"native_dylib"|"wasm_aot"|"wasm_interp"|"wasm_browser" */
  uint64_t    caps;              /* kSandboxed | kHotReload | kDeterministicFp | kStateSnapshot */

  int  (*attach)(const nq_host* host, const void* cfg, void** self);
  void (*detach)(void* self);
  void (*pump_events)(void* self, const nq_event_batch*);   /* 每帧一次，批量 */
  int  (*commit_ir)(void* self, nq_ir_out*);                /* 每帧一次，规范 IR */
  void (*tick)(void* self, uint64_t now_micros);            /* 动画/定时，确定性时钟 */
  int  (*snapshot)(void* self, nq_blob_out*);               /* 热重载状态导出 */
  int  (*restore)(void* self, const nq_blob*);
} nq_ui_module;

NQ_EXPORT const nq_ui_module* nq_ui_enter(uint32_t abi_version);
```

契约要点：
- **每帧跨边界调用 ≤ 50 次**（`pump_events` 1 次 + `commit_ir` 1 次 + `tick` 1 次），由计数器 `ui.boundary_calls` 门禁。逐属性下发的 API 一律禁止。
- `attach` 时拿到 `nq_host`（分配器/日志/时钟/唤醒/任务），**适配器不得直接调 OS**。
- 状态导出/恢复是可选能力（`caps` 声明），但热重载需要它。

## 3. 适配器矩阵

| 适配器 | 边界 | 调用开销 | 热重载 | 沙箱 | 用途 |
|--------|------|----------|--------|------|------|
| `native_static` | **无**（编译期链接、可内联、IR 直接写入宿主 arena） | **0 ns** | ✘（重启 < 100 ms） | ✘ | **1.0 默认**、性能与可调试性基线 |
| `native_dylib` | C ABI + `dlopen` | 3–8 ns | ✔（换库 + restore） | ✘（同进程） | 一方插件、内部热重载 |
| `wasm_aot` | C ABI + 影子栈 | 20–50 ns | ✔（新实例 + restore） | ✔ | 第三方插件、多语言 |
| `wasm_interp` | 同上 | 0.5–2 µs | ✔（最快） | ✔ | 开发期、极小设备 |
| `wasm_browser` | 同线性内存，零拷贝 | 20–50 ns | ✔ | ✔ | Web 部署 |

Unity（同一份 C++ 源码两种构建）：
```bash
-DNQ_UI_RUNTIME=native_static   # 直接链接，经 LTO 后边界消失
-DNQ_UI_RUNTIME=wasm_aot        # clang --target=wasm32 + 自研 AOT
```

## 4. 规范 IR：适配器可互换的唯一依据

- **唯一权威格式是二进制 IR**（可序列化、可哈希、可回放、可远程传输）：
  `[版本][arena 偏移][node(kind,id,style_ref,props...)]*[utf8 池][patch ops]*`，节点 id 由适配器单调分配并复用。
- `native_static` 允许 **fastpath**：直接把 IR 写进宿主侧的 arena/结构体（零序列化），前提是 `caps` 声明 `kFastIR` 且关闭"录制/回放/远程"。
- **IR 等价性测试（准入门槛）**：同一示例 app 在 5 个适配器下必须产出**相同 IR 哈希**（fastpath 模式例外，但需与规范 IR 逐字段等价，测试中一并断言）。做不到就说明适配器不可互换，不允许发布。

## 5. 选择与安全策略

```cpp
cfg.ui.runtime = "auto";   // auto = native_static(产品) / wasm_*(插件)
```

| 场景 | 允许的适配器 | 理由 |
|------|--------------|------|
| 应用主 UI（自家代码） | `native_static` / `native_dylib` / `wasm_aot` | 信任，追求性能 |
| 第三方控件、主题、脚本、市场插件 | **只允许 `wasm_*`** | 必须有沙箱与配额 |
| 浏览器/远程/服务端 | `wasm_browser` / `wasm_aot` | 同一份产物 |
| 调试与 bug 复现 | `wasm_interp` | 编译最快、行为可单步 |

配额（仅 wasm 适配器）：`fuel_per_frame` 默认 2M 指令、`memory_max` 64 MB、`max_boundary_calls` 200、`recursion_limit` 256；超限 = 冻结该插件一帧 + 降级占位 + 上报诊断，不影响其他 UI。

## 6. WASM 适配器实现要点（自研，零第三方运行时）

| 组件 | 说明 | 工程量 |
|------|------|--------|
| 验证器 | MVP + `bulk-memory` + `sign-ext` + `multivalue`；SIMD128 可选；不支持 GC/EH | 1.0 人月 |
| 解释器 | 单趟基线（冷启动、调试、极小设备） | 0.5 人月 |
| AOT 转译（`wasm2c` 思路） | 生成 C11 → 系统 clang/gcc/msvc 优化；影子栈、已验证模块的边界检查消除、fuel 注入 | 2.0 人月 |
| 最小 libc | malloc（走宿主 arena）、memcpy/memset、`printf`→`log`、`setjmp/longjmp`；无文件/进程/线程 | 0.5 人月 |
| 调试 | 解析 `.debug_*`（DWARF 行号/名字）→ 栈回溯到源码行 | 0.5 人月 |
| 一致性 | 官方 WebAssembly spec testsuite（数千 `.wast`） | 0.5 人月 |

性能：AOT 后为原生 65–85%；模块实例化 < 3 ms（对比 Dart VM 初始化 20–60 ms）；fuel 开销 1–3%。
浮点严格 IEEE-754（wasm 禁止多余精度与 FMA 收缩）→ `kDeterministicFp`。

## 7. native 适配器实现要点

- `native_static`：普通静态链接，宿主与 UI 同编译器、同 flags；LTO 后 `commit_ir` 可内联，边界彻底消失。调试器单步即源码级——**这是它必须保留为一等公民的原因**。
- `native_dylib`：`-fvisibility=hidden` + 版本脚本导出单一 `nq_ui_enter`；UI 库内禁止静态含状态的单例（`NQ_NO_STATIC_STATE` 编译选项强制）；Windows 需真正 `FreeLibrary` 才算卸载成功。
- 确定性差异：native 允许 FMA 收缩 → `caps` 不带 `kDeterministicFp`；golden 测试对 native 用容差比对，对 wasm 用精确比对。引擎侧对布局已有定点兜底，因此这一差异只影响光栅级像素，不影响 layout。

## 8. 热重载（两族都支持，机制不同）

| 适配器 | 机制 | 观感 |
|--------|------|------|
| `native_dylib` | 重编译 → `dlopen` 新库 → `restore(snapshot)` → 双模块并存 2 帧 | < 1 s |
| `wasm_*` | 重编译 → 验证 → 新实例化 → `restore(snapshot)` | < 200 ms（AOT）/ < 10 ms（interp） |
| `native_static` | 重启进程 | 冷启动 < 100 ms，可接受 |

状态迁移三档：① 适配器导出 `snapshot/restore`（推荐）；② 同版本线性/线性内存快照；③ 重建并保留节点 id 映射（动画尽量续接）。因 IR 节点 id 稳定，**布局缓存与动画在重载后可直接命中**。

## 9. 测试（与 08 挂钩）

| 测试 | 覆盖 |
|------|------|
| 契约一致性 | 5 个适配器 × 同一 app → IR 哈希相同 + 逐帧 `Scene` 哈希相同 |
| 沙箱 | wasm 适配器：死循环/越界/超大内存/无限递归 → 被冻结、主进程不受影响 |
| 热重载 | dylib 与 wasm 各一套：重载后状态、焦点、动画、滚动位置、选区全部保留 |
| 性能 | `ui.logic_ms` < 1 ms（P99）、`ui.boundary_calls` < 50/帧，进入预算文件 |
| 确定性 | 同一 trace 在 wasm 下逐像素一致；在 native 下 ≤1 LSB |

## 10. 阶段

| 阶段 | 交付 | 出口 |
|------|------|------|
| **M0–M8** | 只做 `native_static`（编译期端口先就位，接口按 C ABI 形态设计） | 1.0 桌面不依赖 wasm |
| **M9** | `native_dylib`（热重载）+ 端口契约冻结 | 热重载可用；IR 等价性测试通过 2 个适配器 |
| **M10** | `wasm_interp` + 最小 libc + spec testsuite | testsuite ≥ 99%，示例 app 可跑 |
| **M11** | `wasm_aot` + DWARF + fuel + 插件沙箱与配额 | AOT 后 UI 逻辑 < 1 ms/帧；恶意插件不崩溃 |
| **M12** | `wasm_browser`（引擎自身也编译到 wasm）+ 服务端渲染 | 三大浏览器可用；首帧 < 500 ms |

## 11. 风险

| 风险 | 缓解 |
|------|------|
| AOT 性能不达 65% | UI 逻辑大多无热点；退化到 `native_static`，wasm 只用于插件 |
| 5 个适配器维护成本 | 契约只有 8 个方法、IR 唯一、测试自动矩阵化；新增适配器成本被有意压低 |
| 调试体验（wasm） | DWARF 行号 + IR 可视化 + devtools；`native_static` 始终可用来复现 |
| 插件 ABI 冻结过早 | 1.0 只冻结 `native_*`，wasm 契约在 M10 后再承诺 |

## 12. 待确认

1. `native_static` 与 `native_dylib` 是否在产品版**同时**提供？（建议：只有 static 进产品，dylib 仅用于内测/热重载构建）
2. 插件是否强制 wasm（即"没有 native 插件"）？（建议：是，安全边界不应有例外）
3. `nq_ui_module` 契约何时对外冻结、承诺几年兼容？（决定生态，也决定我们要多早停止演进）

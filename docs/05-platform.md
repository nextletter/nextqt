# 05 · 跨平台

## 1. 平台矩阵

| 平台 | 图形后端 | 窗口/合成 | 输入/IME | 无障碍 | 阶段 |
|------|---------|-----------|----------|--------|------|
| Linux (X11) | Vulkan 1.1+ / GLES3 兜底 | XCB 自绘窗口（无 GTK/Qt 依赖） | XIM/IBus/Fcitx5 (DBus) | AT-SPI2 (DBus) | M4 |
| Linux (Wayland) | Vulkan / GLES3 | `wl_surface` + `xdg_shell` + `fractional-scale-v1` | `zwp_text_input_v3` | AT-SPI2 | M6 |
| Windows 10+ | D3D12 主 / Vulkan 次 / WARP 兜底 | Win32 + `DirectComposition`（可选） | TSF（IMM32 兜底） | UIAutomation | M7 |
| macOS 11+ | Metal | `NSView` + `CAMetalLayer` | `NSTextInputClient` | `NSAccessibility` | M8 |
| iOS 14+ | Metal | `UIView` + `CAMetalLayer` | `UITextInput` | `UIAccessibility` | M14 |
| Android 8+ | Vulkan / GLES3 | `SurfaceView`/`TextureView` | `InputConnection` | `AccessibilityNodeProvider` | M14 |
| WASM | WebGPU (Canvas2D 兜底) | `canvas` + `requestAnimationFrame` | DOM 隐藏输入元素 | ARIA 映射 | M12（引擎自身也编译为 wasm） |

**平台层铁律**：`src/platform/*` 之外禁止 include 任何 OS 头文件；平台层只实现 `nq::platform` 里声明的接口，不反向依赖 UI 层。新增平台 = 实现 7 个接口 + 1 个 CMake target。

## 2. 窗口与合成

```cpp
namespace nq::platform {
class Window {          // 创建/关闭/尺寸/DPI/标题/图标/最小尺寸/全屏/置顶
  Surface* create_surface(gpu::Device&);   // 返回可被 HAL 接管的原生 surface
  void     set_cursor(CursorId);  void set_hit_test(bool);  // 自绘标题栏用
  std::vector<MonitorInfo> monitors() const;  // 名称/DPI/色域/HDR/刷新率
};
class App {             // 事件循环、RunLoop、唤醒、定时器、剪贴板、文件对话框、托盘
};
}
```

- **窗口内容自绘**（客户区 + 可选自绘标题栏），但**菜单栏、右键菜单、文件对话框、通知、托盘、DnD 全部原生**。Flutter 在这些地方长期"像网页套壳"，这是我们最直接的差异化。
- 多窗口：共享 `gpu::Device`、字形 atlas、图像池；每窗口独立 swapchain + 独立 `Scene` + 独立 present 定时。窗口间可共享元素/信号（同一 App 状态）。
- 高 DPI：每显示器 DPI 感知（Windows PMv2、macOS `backingScaleFactor`、Wayland `fractional-scale-v1`）；窗口跨屏时按新 scale 重光栅，布局不重算（DIP 坐标不变）。
- 唤醒/节流：窗口不可见 → 停止 present；最小化 → 释放 swapchain；后台 → 降帧（`NQ_BACKGROUND_FPS`，默认 15 或 0）。

## 3. 输入与 IME（最容易被低估的部分）

| 能力 | 实现要点 |
|------|----------|
| 键盘 | 物理键码 → 字符/快捷键，区分 `keydown/keyup/char`，修饰键状态、死键（`´`+`e`）、`dead key` 组合、键盘布局切换 |
| 文本输入 | 引擎维护"当前 IME 会话"：`set_client_rect`（候选窗锚点）、`set_surrounding_text`（上下文）、`preedit/marked range`、`commit`、`cancel` |
| IME 差异 | Linux 走 IBus/Fcitx 的 DBus 协议（不依赖 GTK IM module）；Windows 用 TSF（`ITfThreadMgr`）；macOS/iOS `NSTextInputClient`；Android `InputConnection` |
| 中文/日文/韩文 | 组合期间的候选窗定位、`preedit` 下划线绘制、组合取消、`unmark` 与撤销的交互；**目标：与原生编辑框的手感无法区分** |
| 输入法在合成线程 | 光标位置变化时同步"锚点矩形"给平台，避免候选窗漂移（Qt 在这里常年有 1 帧延迟） |
| 手势/指针 | 鼠标/触摸/笔/滚轮（含横向、像素级精度、`high-resolution wheel`）、`pointer capture`、触摸板手势（macOS `scrollWheel` phase） |
| 输入延迟 | 合成线程就地处理"可本地响应"的部分（按压态、滚动、悬停）；测量口径：硬件光敏探针 + `desiredPresentTime` 差值 |

**IME 的验收标准**（写进测试）：中文/日文输入 100 个词，候选窗位置误差 0 帧、组合文本与原生编辑框逐字符一致、撤销粒度一致。

## 4. 无障碍

| 平台 | API | 关键难点 |
|------|-----|---------|
| Linux | AT-SPI2 over DBus | 树同步延迟、`GetChildren` 递归、文本接口 |
| Windows | UIAutomation (Provider) | `IRawElementProviderSimple`、`TextPattern`、事件节流 |
| macOS/iOS | `NSAccessibility` / `UIAccessibility` | 角色映射、`accessibilityFocused` 双向 |
| Android | `AccessibilityNodeProvider` | 虚拟节点 id 稳定、`ACTION_*` 映射 |
| WASM | ARIA 镜像 DOM 树 | 隐藏 DOM 与 canvas 的焦点同步、`aria-live` |

策略：引擎维护**语义树**，平台桥只做"增量 diff → 平台对象操作"。每平台写一个"屏幕阅读器实测清单"（NVDA / VoiceOver / Orca / TalkBack），作为发版门槛。

## 5. 系统字体与系统图像解码（零依赖政策的两个例外）

| 例外 | 理由 | 边界 |
|------|------|------|
| 系统字体枚举/取数 | iOS/Android 上系统字体数据只能通过 CoreText/AssetManager 拿到；桌面需要尊重用户安装的字体 | `platform::FontProvider` 只负责"列出字体 + 给出文件字节/mmap"，**解析与整形仍是自研**；它是端口 `port::font` 的一个来源适配器 |
| 系统图像解码 | WIC(macOS ImageIO / Android BitmapFactory) 覆盖 HEIC/AVIF/WebP 等格式，自研不现实 | 仅作为 `ImageProvider` 的一档；自有 PNG/JPEG/QOI 解码器优先（可控、可 fuzz、跨平台一致） |

对应编译开关：`NQ_SYSTEM_FONTS=ON|OFF`、`NQ_SYSTEM_IMAGE_CODECS=ON|OFF`。两者都实现为端口适配器（`port::font::source`、`port::image`），关闭后仍能完整运行（只是字体集合/格式受限），用于嵌入式与 WASM。

## 6. 移动端与 WASM

| 平台 | 关键差异 | 处理 |
|------|----------|------|
| iOS | 无 JIT（本就无 VM）、内存告警、ProMotion 120Hz、`UIScrollView` 手感预期 | `CADisplayLink` 取帧、`didReceiveMemoryWarning` → `trim_to(hard)` |
| Android | 碎片化 GPU 驱动、`Choreographer`、低内存机 | GLES3 兜底、Tier 3 光栅、可选"低端机降级档" |
| WASM | 无文件系统/线程默认关闭、Canvas 尺寸限制、IME 靠隐藏 DOM | 引擎与 UI 执行层**同栈编译**（同一线性内存，零拷贝），见 [09-ui-runtime.md](09-ui-runtime.md)；单线程默认（`SharedArrayBuffer` opt-in）、字体走 `fetch`、光栅回退 Tier 1/3、无 GPU 时 CPU + `putImageData` |

## 7. 打包与分发

| 平台 | 产物 |
|------|------|
| Linux | 单文件 ELF（静态链接 C++ 运行时可选）、`.desktop`、可选 Flatpak |
| Windows | `.exe`（静态 CRT 可选）、可选 MSIX 签名 |
| macOS | `.app` bundle（含 Info.plist、公证、通用二进制 arm64+x86_64） |
| iOS/Android | Framework / AAR |
| WASM | `.wasm` + 极小 JS glue（< 8 KB） |

体积预算见 README；CI 对"引擎 + 20 控件"的 release 产物大小做门禁。

## 8. 原生双向嵌入

```cpp
// (1) 嵌进现有宿主：Qt/MFC/Cocoa/WinForms 里放一个 NextQt 视图
nq_view* v = nq_view_attach(nq_native_view(host_handle), &cfg);  // hwnd / NSView* / XID
// (2) 反向：把宿主原生控件放进我们的树
Element e = platform_view(handle);   // 纹理化或层直挂；输入按 z 顺序转发
```

- 纹理化路径简单、兼容性最好，多一次拷贝；层直挂（DirectComposition / `CALayer` sublayer / X11 subwindow）零拷贝但有 z 序限制。默认纹理化，显式 opt-in 直挂。
- 用于渐进式迁移：老 Qt 应用一屏一屏换到 NextQt，这在与 Qt 的竞争中是决定性的落地路径。

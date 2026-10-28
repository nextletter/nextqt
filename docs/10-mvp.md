# 10 · M0 最小 MVP（已可运行）

## 1. 现在能做什么

```bash
cmake --preset headless && cmake --build --preset headless -j
ctest --preset headless                    # 5 个测试套件 / 49 个用例，全绿

./build/headless/nq_hello out.ppm          # 渲染一张卡片并打印计数器
./build/headless/nq_showcase               # 8 个 showcase 场景（见 docs/11-showcase.md）
./tools/coverage.sh                        # 覆盖率门禁：行 ≥95%、分支 ≥80%
```

产物 `out.ppm` 已人工核对：圆角卡片、抗锯齿圆角、圆形头像、1px 分割线、带裁剪的进度条、占位字形。

```
{
  "raster.ops": {"value": 7, "unit": "op"},
  "raster.paths": {"value": 4, "unit": "op"},
  "raster.pixels_shaded": {"value": 41372, "unit": "px"}
}
```

## 2. 已落地的设计条目

| 设计条目 | 落点 | 证据 |
|----------|------|------|
| 定点布局单位（1/64 px，饱和运算） | `src/base/layout_unit.h` | `test_base`：构造/舍入/饱和/除法 4 组用例 |
| 注入式时钟 | `src/base/clock.h` | `DeterministicClock` 单测，无 `sleep` |
| 行为计数器注册表 | `src/base/diag.{h,cpp}` | `test_raster` 断言 `raster.ops` 精确 +2 |
| 显示列表 IR（不可变、可哈希、4 字节对齐、可遍历） | `src/dl/display_list.{h,cpp}` | `test_dl`：op 流对齐/遍历/bounds/hash 稳定性 |
| 路径展平（自适应细分 + 容差） | `display_list.cpp` | 细容差点数 > 粗容差点数 |
| 裁剪栈 | `Builder::clip_rect/pop_clip` + `raster::replay` | 嵌套裁剪交集 + 弹出后恢复 |
| 光栅 Tier 0 原型（矩形解析覆盖率） | `raster::fill_rect_analytic` | 整数边界精确、0.5 偏移 = 128/255、面积不变量 1.75 px² ±0.01 |
| 光栅参考实现（路径 16×16 超采样） | `raster::fill_polylines` | 圆面积与 πr² 误差 < 1% |
| 预乘 alpha 合成 | `Surface::blend_pixel` | 半透明白覆盖不透明黑 → 颜色 128 且仍不透明 |
| 确定性渲染 | 全链路 | 同一场景两次渲染 `content_hash` 相同 |
| golden 测试（自研 runner + `--update-golden`） | `tests/golden_testing.h` | `tests/golden/card_scene.ppm` 逐字节比对 |
| 零第三方依赖 | 全部 | 仅 C++ 标准库 + libc |
| 覆盖率门禁 | `tools/covreport.py` + `tools/coverage.sh` | 行 98.85%（771/780）、分支 81.61%（364/446），低于 95%/80% 直接失败 |
| Showcase | `examples/showcase.cpp` | 8 个 480×300 场景，含哈希；见 docs/11-showcase.md |
| 图像输出 | `src/raster/image_io.cpp` | 自研 PNG（stored deflate）+ PPM，零依赖 |
| 无异常 / 无 RTTI | `cmake/CompilerWarnings.cmake` | `-fno-exceptions -fno-rtti`，错误走 `Result<T>` |

## 3. 构建矩阵

| 预设 | 参数 | 用途 |
|------|------|------|
| `headless` | Release + `NQ_GOLDEN=ON`，platform/gpu = none | CI 主门禁（无窗口、无 GPU） |
| `dev` | Debug + `NQ_WERROR=ON` | 日常开发，零告警 |
| `rel` | Release | 性能与体积 |
| `asan` | ASan + UBSan | 内存/UB |

一条硬性事实：**`headless` 构建不包含任何 OS 头文件**，这是"可 WASM / 可服务端渲染 / 可 CI"的地基（docs/01 §2）。

## 4. 已知边界（M0 故意不做）

| 项 | 现状 | 计划 |
|----|------|------|
| 光栅质量 | 矩形解析式（Tier 0 原型）；路径/圆/圆角走 16×16 超采样（慢，~65 级灰度） | M1：分层光栅（Tier 1 掩码缓存 + 解析式 rrect） |
| 文本 | 5×7 点阵占位字形 | M1–M5：Unicode → 字体 → 整形 → 编辑内核 |
| 变换 | 仅恒等变换 | M2：仿射矩阵进显示列表，`Similarity` 快路径 |
| 图像 | 无（只有 Surface 合成） | M1：PNG（自研 inflate） |
| 布局/控件 | 无 | M2 |
| GPU / 窗口 / 平台 | 未接入（端口已预留） | M3+ |
| 线程 | 单线程 | M4：光栅池 + 合成线程 |

## 5. 下一步（M1 入口）

1. **光栅 Tier 1**：小路径 → A8 掩码 atlas（键 `(path_hash, scale_bucket)`），把图标与圆角变成一次实例化贴图；同时把 `kSupersample` 降到 1（解析累积覆盖率），让 golden 从"可容忍"变为"精确"。
2. **Unicode 表生成**（`tools/ucdgen`）：先落 general category / script / grapheme / line break 四张表 + `GraphemeBreakTest.txt` 100%。
3. **sfnt 解析**：`cmap(4,12)` + `glyf/loca` + `hmtx`，渲染单字体单脚本的 A8 字形，替换占位字形。
4. **布局骨架**：`Row/Column/Stack + LayoutUnit` + 布局根与 fragment 缓存，用计数器断言"1% 脏区只访问 N 个节点"。

## 6. 提交校验清单（已经通过）

- `ctest --preset headless`：5/5 套件（49 用例）
- `ctest --preset dev`（`-Werror`）：5/5
- `tools/coverage.sh`：行 98.85% / 分支 81.61%，门禁通过
- 无第三方依赖：`ldd build/headless/nq_hello` 只含 libc/libstdc++
- 单次 `cmake --build` 全量 < 10 s（4 核）

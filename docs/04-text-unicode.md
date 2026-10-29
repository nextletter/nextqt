# 04 · 文本与 Unicode

> 这是整个项目**最大的一块工期与最大的技术风险**，也是唯一可以真正拉开与 Qt/Flutter 差距的护城河。
> 自研替代 HarfBuzz + ICU + FreeType，估算 **18–22 人月**（见 06-roadmap）。

## 1. 为什么自研，以及代价

| 收益 | 代价 |
|------|------|
| 无 LGPL（Qt 的商业化最大障碍）、无 FTL 署名条款、单许可证可静态链接 | 整形一致性要自己扛 |
| 跨平台像素一致的前提：不依赖系统字体与系统整形差异 | CFF/Type2 解释器、复杂脚本引擎都是硬骨头 |
| 可裁到 400KB 表 + 300KB 代码（WASM/嵌入式可行） | Unicode 版本升级需要自建流程 |
| 可确定性测试（同一版本引擎在 Win/mac/Linux 输出相同的 run） | 需要 conformance 测试基建 |
| 可深度优化（整形结果缓存、按需惰性表、SIMD 分段） | 无现成生态与专家 |

**回退路径**：整形是一个运行时端口（`port::shape`），自研实现名为 `own`。可选适配器 `harfbuzz`（C ABI 封装、启动时按有序链选择）既不违反零依赖政策，也提供了应急通道；见 [07-adapters.md](07-adapters.md) §2.2。它的另一重身份是 CI 里的对照 oracle。

同样的适配器形态适用于字体解析（FreeType）、Unicode/BiDi（ICU4C / SheenBidi）、图像解码——**任何不得不引入的第三方都只能以这种形态存在**。

## 2. 分层

```
src/unicode   表生成 + 规范化 + 分段 + BiDi + 换行          (无字体、无 I/O)
      ↓
src/font      sfnt/CFF/变体/彩色表解析 + 字形轮廓与度量      (纯数据，无渲染)
      ↓
src/text      itemize → shape(OT layout) → 断行 → 行盒       (纯计算)
      ↓
src/edit      rope/选区/caret/undo/IME 组合/marked text      (状态机)
```

四层都可单独做 headless 单测，不依赖窗口与 GPU。

## 3. Unicode 数据与表生成（`tools/ucdgen`）

输入（`data/ucd/<version>/`，纯数据文件，非代码依赖）：

`UnicodeData.txt` `Scripts.txt` `ScriptExtensions.txt` `LineBreak.txt` `EastAsianWidth.txt`
`BidiBrackets.txt` `BidiMirroring.txt` `DerivedCoreProperties.txt` `PropList.txt`
`GraphemeBreakProperty.txt` `WordBreakProperty.txt` `SentenceBreakProperty.txt`
`emoji-data.txt` `CaseFolding.txt` `SpecialCasing.txt` `DerivedNormalizationProps.txt`
`ArabicShaping.txt` `IndicSyllabicCategory.txt` `IndicPositionalCategory.txt` `HangulSyllableType.txt`

生成物 `build/gen/unicode_tables.cpp`：

- **两级 trie**（`stage1[cp>>7]` → `stage2[block][cp&127]`）存枚举属性；常见块共享（dedup），实测压缩比 8–15×。
- 多值/区间属性（分解、组合类、镜像、GSUB 特性标签）走**分段堆**（`offset + len` 指向压缩数组）。
- 只生成叶子，`constexpr`/`const` 静态数据，**无运行时初始化**（无 `-fno-exceptions` 冲突、无静态构造顺序问题）。
- 体积预算：全部 Unicode 表 **< 400 KB**（opt-in `NQ_UNICODE_FULL=OFF` 可裁到 80 KB：只保留拉丁/CJK/常用符号；用于嵌入式档）。

升级流程：`ucdgen --version 16.0 --check` 在 CI 中对比"表哈希 + conformance 测试通过率"，升级必须过全部 Unicode 官方测试文件。

## 4. 规范化与大小写（UAX #15/#44）

- NFD/NFC 用"分解映射 + 组合类排序 + 组合排除表"，带 **quick-check**（绝大多数文本直接 `NFC_QC=Yes` 早退）。
- 只在必要处规范化：用户输入 → NFC；整形前不做全局规范化（OpenType 字体按字形序列工作，HarfBuzz 同样不规范化），只在 `cmap` 查找失败时尝试 NFC/NFD 变体。
- 大小写：简单映射 + `SpecialCasing`（ß/İ/ſ/词尾 Σ）；提供 `to_upper/to_lower(locale)`，土耳其/阿塞拜疆/希腊的特殊规则显式实现。

## 5. 分段（UAX #29 + #14）

| 分段 | 算法 | 用途 |
|------|------|------|
| Grapheme | 状态机（含 emoji ZWJ 序列、区域旗帜、扩展字形簇） | 光标移动、删除、选区 |
| Word | 状态机（含 CJK 词边界近似、URL/邮箱启发式） | 双击选词、搜索 |
| Sentence | 状态机 | 无障碍朗读、摘要 |
| **Line break** | UAX #14 成对表 + **可选的 CJK 避头尾/西文孤儿词**定制 | 断行 |

**性能关键**：断行机会表**只依赖文本内容，不依赖宽度**。段落缓存的键是文本哈希；改变宽度时只做一次贪心/DP 分配，不做重新整形 —— 这是窗口拉伸能保持 60fps 的原因。

## 6. 双向文本（UAX #9）

完整实现：`P2/P3`（段落级别，含 `R`/`AL`/`L` 首强字符）→ `X1–X10`（显式嵌入/覆盖/隔离，含 `FSI/PDI`）→ `W1–W7` → `N0`（成对括号，用 `BidiBrackets.txt`）→ `N1/N2` → `I1/I2` → `L1/L2`（行内重排、尾随空白）。

- 输出：每个行的"视觉片段序列 + 方向 level"，供布局按视觉顺序摆放、供 caret 做 **affinity（左/右偏）**。
- caret 与选区的方向性：`anchor/focus` 语义 + 视觉位置映射；在方向边界处的点击必须落在正确一侧（Qt 这里错了很多年）。
- 数字/标点在 RTL 中的镜像与编号（`BidiMirroring.txt` → 括号镜像）。
- 测试直接跑 Unicode 官方 `BidiTest.txt` / `BidiCharacterTest.txt`（数万条），目标 **100% 通过**。

## 7. 整形（`src/text/shape`）

### 7.1 流程

```
itemize  →  按 (script, 字体链, bidi level) 切 run
shape    →  单 run 内：cmap 映射 → OT layout (GSUB→GPOS) → 位置
fallback →  缺字按 grapheme cluster 粒度换字体（不拆 cluster）
linebreak→  用 §5 的机会表 + 每行预算
reorder  →  按 bidi level 排视觉片段
```

### 7.2 OpenType Layout（自研核心）

- **GSUB**：Lookup 1–8（含 7 扩展、8 反向链式），`FeatureList/ScriptList/LangSys` 选择，按 `LookupIndex` 顺序执行，同一 lookup 内按 coverage 二分。
- **GPOS**：Lookup 1–9（单、对、草写、mark-to-base、mark-to-ligature、mark-to-mark、上下文、链式、扩展）；`ValueRecord` 的 X/Y 位移、设备表（ppem 相关偏差）。
- **GDEF**：字形类别、MarkAttachClass、MarkGlyphSets（`UseMarkFilteringSet`）、LigCaretList。
- 特性默认集：`ccmp locl rlig liga clig calt kern mark mkmk curs dist` + 脚本特性（`init medi fina isol`、`nukt akhn rphf blwf half pstf` 等）。用户可显式开关（`tnum`/`smcp`/`ss01`/`dlig`）。
- 递归深度、lookup 迭代次数、输出长度全部**有硬上限**（防恶意字体与死循环），并进 fuzz 覆盖。
- **一致性目标**（对比 CI 中的 HarfBuzz oracle）：拉丁/希腊/西里尔/日文假名/韩文 **≥99.9%** 字形序列一致；阿拉伯/希伯来/天城文 **≥98%**。

### 7.3 复杂脚本分级（按阶段交付，绝不留"看起来能跑"的假象）

| Tier | 脚本 | 需要的额外处理 | 阶段 |
|------|------|---------------|------|
| A | 拉丁、希腊、西里尔、亚美尼亚、格鲁吉亚、CJK、韩文、泰文 | OT 布局 + 泰文 SARA AM 重排 + 韩文 Jamo 组合 | M3 |
| B | 阿拉伯/波斯/乌尔都、希伯来、天城文、孟加拉、泰米尔 | 连接形、mark 重排、音节重排、reph、连字 | M5–M7 |
| C | 缅甸、高棉、僧伽罗、藏、埃塞俄比亚、其他印度语系 | 音节状态机 | M12+ |

Tier 之外的脚本**回退到逐字符渲染并明确标记**（devtools 显示"降级整形"），不静默出错。

## 8. 字体解析（`src/font`）

> **当前实现状态（M1 第一片）**：已支持 `sfnt`(0x00010000/'true')、`cmap` 格式 4/12、
> `loca`(short/long)、`glyf`（简单 + 复合，含 F2Dot14 变换与递归深度限制）、`hmtx`、`hhea`、`head`、`maxp`、`name`。
> 字形轮廓 → 二次贝塞尔自适应展平 → 覆盖率掩码 → 4× 亚像素字形缓存（LRU + 预算 + 命中率计数器）。
> 待办：CFF/CFF2、变体（gvar/HVAR）、COLR、GSUB/GPOS（§7）、WOFF2。

| 能力 | 表 | 备注 |
|------|----|------|
| 容器 | `sfnt`、`ttcf`（TTC）、WOFF1 | WOFF2 需 brotli 解压，推迟到 M9（自研 brotli 解码器或构建期解压） |
| 字符映射 | `cmap` 格式 0/4/6/12/13（+14 变体选择） | 12/13 是 CJK 必需 |
| 轮廓 | `glyf`/`loca`（含复合字形、指令跳过） | |
| 轮廓 | `CFF` / `CFF2`（Type2 charstring 解释器、subrs、hint 操作忽略） | CJK OTF 与很多现代拉丁字体必需 |
| 度量 | `hmtx`/`vmtx`/`hhea`/`OS/2`/`head`/`maxp`/`post` | 垂直度量、下划线位置、x-height 全部要读对 |
| 布局 | `GDEF`/`GSUB`/`GPOS` | §7 |
| 彩色 | `COLR`/`CPAL`（v0/v1 绘制图）、`sbix`、`CBDT/CBLC` | COLRv1 的渐变/裁剪/变换需递归光栅 |
| 变体 | `fvar`/`gvar`/`HVAR`/`VVAR`/`avar`/`MVAR`/`STAT` | 轴值实例化 + 度量插值 |
| 元信息 | `name`（多语言字体名）、`OS/2`（字重/字宽） | 字体选择 UI |

- **所有解析有界检查**（偏移/长度/循环引用/点数上限），进 libFuzzer 持续跑；损坏字体降级到"系统兜底字体 + 诊断日志"，绝不崩溃。
- 字体来源：`data/fonts/`（随包，SIL OFL，默认 UI 字体与 CJK 子集）+ 系统字体提供者（平台层）+ 应用注册（内存/文件/压缩包）。
- 内存：字体文件优先 `mmap`（零拷贝），字形轮廓按需解码 + LRU（`(font, glyph, 轴值桶)`）。

## 9. 编辑内核（`src/edit`）

不是"塞个 string 进 TextField"，而是完整内核：

| 能力 | 说明 |
|------|------|
| 缓冲区 | piece table / rope（按行索引），UTF-8 存储，O(log n) 插入删除 |
| 光标 | grapheme 粒度移动、词/行/段移动（视觉行）、bidi affinity、虚拟空格（tabs） |
| 选区 | anchor/focus 语义，跨 bidi 边界的视觉选择、双击/三击、矩形选择（可选） |
| 撤销 | 事务化 + 时间/类型合并，可与 IME 组合操作原子化 |
| IME | marked range（组合文本）、preedit 绘制、候选窗定位（返回锚点矩形给平台）、提交/取消 |
| 剪贴板 | 文本 + 可选富文本/HTML 往返；粘贴大文本的限流与撤销 |
| 其他 | 只读/密码（不回显语义树）、Tab 停靠、软换行、Unicode 不可见字符可视化、拼写检查钩子 |

## 10. 文本布局与缓存

- `TextBlob` = 一次整形结果（glyph ids + 位置 + 簇映射 + 断行机会），不可变、可共享、可跨线程。
- 段落缓存键：`(文本哈希, style 哈希, 可用宽度桶, 字体版本)`。
- 行盒计算（行高、基线、`vertical_align`）在布局层，与整形解耦。
- 整形吞吐目标：拉丁 run **< 1.5 µs / 32 字符**（缓存命中 ~50 ns）；10 万字文档首次全量整形 < 120 ms（可后台协程渐进完成，首屏只整形可见区）。

## 11. 一致性验证策略（关键工程手段）

**第三方库只做 CI oracle，绝不进产物**：

| 被测 | Oracle | 语料 |
|------|--------|------|
| 规范化/分段/BiDi/换行 | Unicode 官方测试文件（权威，无需第三方） | `NormalizationTest` `GraphemeBreakTest` `LineBreakTest` `BidiTest` `BidiCharacterTest` |
| 整形 | HarfBuzz（CI 容器内 + 可选运行时适配器） | 官方 conformance + 多语种语料（Noto 全脚本样本） |
| 字形光栅 | FreeType（CI 容器内） | 掩码逐像素对比，允许 ≤2/255 差异 |
| 文本布局 | 自建黄金用例（截图对比） | 50 个真实 UI 场景 |

通过门槛写进 CI 门禁：BiDi/分段/换行 **100%**；整形 Latin Tier A ≥99.9%，Tier B ≥98%。不达标不允许合并到 `main`。

## 12. 体积与性能预算

| 项 | 预算 |
|----|------|
| Unicode 表 | < 400 KB（嵌入式档 < 80 KB） |
| 字体解析 + 整形 + 编辑 代码 | < 900 KB（`-Os + LTO`） |
| 默认随包字体 | Inter 子集 120 KB + Noto Sans CJK SC 子集 1.2 MB（可按平台裁） |
| 整形缓存 | 4 MB 默认，可配 |
| 首次打开 10 万字文档 | < 120 ms（可见区优先） |

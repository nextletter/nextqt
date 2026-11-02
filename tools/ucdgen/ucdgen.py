#!/usr/bin/env python3
"""NextQt · UCD 表生成器（构建期工具，产物入库，运行时零依赖）

用法:
  python3 tools/ucdgen/ucdgen.py generate --ucd build/ucd/16.0.0 --out src/unicode
  python3 tools/ucdgen/ucdgen.py fetch    --ucd build/ucd/16.0.0   # 带 sha256 校验

输出:
  src/unicode/unicode_tables.h   —— 枚举与访问函数声明
  src/unicode/unicode_tables.cpp —— 两级 trie（块大小 256，块去重）+ 访问函数实现

设计要点见 docs/04-text-unicode.md §3：
  - 两级 trie：stage1[cp>>8] -> block index，stage2 为 256 字节块
  - 块去重：大量码点区间共用默认块，实测压缩 8-15 倍
  - 无运行时初始化（const 静态数据），无第三方依赖
"""

from __future__ import annotations

import argparse
import hashlib
import os
import sys
import urllib.request
from array import array
from pathlib import Path

UNICODE_VERSION = "16.0.0"
MAX_CP = 0x110000
BLOCK = 256

BASE_URL = "https://www.unicode.org/Public/16.0.0/ucd"

FILES = {
    # 名称: (相对路径, sha256)
    "UnicodeData.txt": ("UnicodeData.txt",
                        "ff58e5823bd095166564a006e47d111130813dcf8bf234ef79fa51a870edb48f"),
    "LineBreak.txt": ("LineBreak.txt",
                      "e97e4259d0d20fab150b9c7b4b28abfae5cd78ca97e7f4ac6ed20d685d5f4a7c"),
    "GraphemeBreakProperty.txt": ("auxiliary/GraphemeBreakProperty.txt",
                                  "c29360bd6f7132811d701d29069541e827eb44bfc4c8fbde8c370d6982689dc1"),
    "DerivedCoreProperties.txt": ("DerivedCoreProperties.txt",
                                  "39d35161f2954497f69e08bdb9e701493f476a3d30222de20028feda36c1dabd"),
    "emoji-data.txt": ("emoji/emoji-data.txt",
                       "f1365a5173eee18e1f98b240cdc492e84a25f1ce7e0c9d1094eb29c41a22696a"),
}
TESTS = {
    "GraphemeBreakTest.txt": ("auxiliary/GraphemeBreakTest.txt",
                              "ee2b9354d270ac061b29f09662cafea06341d77e704b8cc6bd72aaeeda363cb5"),
}

# 固定的枚举顺序：ID 稳定，不随数据版本漂移
GCB = ["Other", "CR", "LF", "Control", "Extend", "ZWJ", "Regional_Indicator", "Prepend",
       "SpacingMark", "L", "V", "T", "LV", "LVT"]
INCB = ["None", "Linker", "Consonant", "Extend"]
LB = ["XX", "AI", "AK", "AL", "AP", "AS", "B2", "BA", "BB", "BK", "CB", "CJ", "CL", "CM", "CP",
      "CR", "EB", "EM", "EX", "GL", "H2", "H3", "HL", "HY", "ID", "IN", "IS", "JL", "JT", "JV",
      "LF", "NL", "NS", "NU", "OP", "PO", "PR", "QU", "RI", "SA", "SG", "SP", "SY", "VF", "VI",
      "WJ", "ZW", "ZWJ"]
BC = ["L", "R", "AL", "EN", "ES", "ET", "AN", "CS", "B", "S", "WS", "ON", "LRE", "LRO", "RLE",
      "RLO", "PDF", "NSM", "BN", "FS", "LRI", "RLI", "FSI", "PDI"]
GC = ["Cn", "Lu", "Ll", "Lt", "Lm", "Lo", "Mn", "Mc", "Me", "Nd", "Nl", "No", "Pc", "Pd", "Ps",
      "Pe", "Pi", "Pf", "Po", "Sm", "Sc", "Sk", "So", "Zs", "Zl", "Zp", "Cc", "Cf", "Cs", "Co"]


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(ucd_dir: Path) -> None:
    for name, (rel, digest) in {**FILES, **TESTS}.items():
        dest = ucd_dir / name
        if dest.exists() and sha256_of(dest) == digest:
            print(f"  [ok] {name}")
            continue
        url = f"{BASE_URL}/{rel}"
        print(f"  [get] {name} <- {url}")
        dest.parent.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(url, dest)
        actual = sha256_of(dest)
        if actual != digest:
            raise SystemExit(f"sha256 mismatch for {name}:\n  expected {digest}\n  actual   {actual}")
        print(f"  [ok] {name} ({dest.stat().st_size} bytes)")


def parse_ranges(path: Path, *, filter_lines=None) -> list[tuple[int, int, str]]:
    """解析 `XXXX..YYYY ; Value # comment` 形式；返回 (start, end, value)。"""
    out: list[tuple[int, int, str]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [f.strip() for f in line.split(";")]
        if len(fields) < 2:
            continue
        rng = fields[0]
        if ".." in rng:
            start, end = (int(x, 16) for x in rng.split(".."))
        else:
            start = end = int(rng, 16)
        if filter_lines is not None:
            value = filter_lines(fields)
            if value is None:
                continue
        else:
            value = fields[1]
        out.append((start, end, value))
    return out


def parse_unicode_data(path: Path) -> tuple[array, array]:
    """返回 (general_category, bidi_class) 两张 0..0x10FFFF 的表（默认 Cn / L）。"""
    gc = array("B", [GC.index("Cn")]) * MAX_CP
    bc = array("B", [BC.index("L")]) * MAX_CP
    pending_first = None
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw:
            continue
        fields = raw.split(";")
        cp = int(fields[0], 16)
        name = fields[1]
        if name.endswith(", First>"):
            pending_first = cp
            continue
        if name.endswith(", Last>"):
            if pending_first is not None:
                g = GC.index(fields[2]) if fields[2] in GC else 0
                b = BC.index(fields[4]) if fields[4] in BC else 0
                for c in range(pending_first, cp + 1):
                    gc[c] = g
                    bc[c] = b
                pending_first = None
            continue
        gc[cp] = GC.index(fields[2]) if fields[2] in GC else 0
        bc[cp] = BC.index(fields[4]) if fields[4] in BC else 0
    return gc, bc


def fill(table: array, ranges, names: list[str], default: str = None) -> None:
    for start, end, value in ranges:
        if value not in names:
            continue
        idx = names.index(value)
        for cp in range(start, min(end, MAX_CP - 1) + 1):
            table[cp] = idx


def build_property(table: array) -> tuple[list[int], list[bytes]]:
    """把逐码点表编码成两级 trie：返回 (stage1, blocks)。"""
    blocks: list[bytes] = []
    index: dict[bytes, int] = {}
    stage1: list[int] = []
    for base in range(0, MAX_CP, BLOCK):
        chunk = bytes(table[base:base + BLOCK])
        slot = index.get(chunk)
        if slot is None:
            slot = len(blocks)
            index[chunk] = slot
            blocks.append(chunk)
        stage1.append(slot)
    assert len(stage1) == MAX_CP // BLOCK
    return stage1, blocks


def emit_array_u16(name: str, values: list[int], out: list[str]) -> None:
    out.append(f"constexpr uint16_t {name}[{len(values)}] = {{")
    for i in range(0, len(values), 24):
        out.append("  " + ",".join(str(v) for v in values[i:i + 24]) + ",")
    out.append("};")


def emit_blocks(name: str, blocks: list[bytes], out: list[str]) -> None:
    out.append(f"constexpr uint8_t {name}[{len(blocks)}][{BLOCK}] = {{")
    for block in blocks:
        out.append("  {" + ",".join(str(b) for b in block) + "},")
    out.append("};")


def emit_enum(name: str, values: list[str], out: list[str]) -> None:
    out.append(f"enum class {name} : uint8_t {{")
    for i, v in enumerate(values):
        out.append(f"  k{v.replace('_', '')} = {i},")
    out.append("};")
    out.append(f"inline constexpr std::string_view {name.lower()}_name({name} v) noexcept {{")
    out.append("  switch (v) {")
    for i, v in enumerate(values):
        out.append(f"    case {name}::k{v.replace('_', '')}: return \"{v}\";")
    out.append("  }")
    out.append("  return \"?\";")
    out.append("}")
    out.append("")


def generate(ucd_dir: Path, out_dir: Path) -> None:
    print(f"reading UCD {UNICODE_VERSION} from {ucd_dir}")
    gcb_table = array("B", [0]) * MAX_CP
    fill(gcb_table, parse_ranges(ucd_dir / "GraphemeBreakProperty.txt"), GCB, "Other")

    incb_table = array("B", [0]) * MAX_CP

    def incb_filter(fields):
        return fields[2] if len(fields) >= 3 and fields[1] == "InCB" else None

    fill(incb_table, parse_ranges(ucd_dir / "DerivedCoreProperties.txt", filter_lines=incb_filter),
         INCB, "None")

    lb_table = array("B", [0]) * MAX_CP
    fill(lb_table, parse_ranges(ucd_dir / "LineBreak.txt"), LB, "XX")

    extpict = array("B", [0]) * MAX_CP
    fill(extpict, parse_ranges(ucd_dir / "emoji-data.txt",
                               filter_lines=lambda f: f[1] if f[1] == "Extended_Pictographic" else None),
         ["No", "Extended_Pictographic"], "No")

    gc_table, bc_table = parse_unicode_data(ucd_dir / "UnicodeData.txt")

    props = [
        ("Gcb", "gcb", GCB, gcb_table),
        ("IncB", "incb", INCB, incb_table),
        ("Lb", "lbc", LB, lb_table),
        ("Bc", "bidi_class", BC, bc_table),
        ("Gc", "general_category", GC, gc_table),
    ]

    h: list[str] = []
    cpp: list[str] = []
    h.append("// 由 tools/ucdgen/ucdgen.py 生成，请勿手改。")
    h.append(f"// Unicode 版本: {UNICODE_VERSION}")
    h.append("#pragma once")
    h.append("")
    h.append("#include <cstdint>")
    h.append("#include <string_view>")
    h.append("")
    h.append("namespace nq::unicode {")
    h.append("")
    for enum_name, _, values, _ in props:
        emit_enum(enum_name, values, h)
    h.append("// 字素簇边界属性（UAX #29）")
    h.append("[[nodiscard]] Gcb gcb(char32_t cp) noexcept;")
    h.append("// 印度语系连写属性（UAX #29 GB9c）")
    h.append("[[nodiscard]] IncB incb(char32_t cp) noexcept;")
    h.append("// 换行类（UAX #14，待实现）")
    h.append("[[nodiscard]] Lb lbc(char32_t cp) noexcept;")
    h.append("// 双向类（UAX #9，待实现）")
    h.append("[[nodiscard]] Bc bidi_class(char32_t cp) noexcept;")
    h.append("// 通用类别")
    h.append("[[nodiscard]] Gc general_category(char32_t cp) noexcept;")
    h.append("// 扩展象形（emoji，UAX #29 GB11）")
    h.append("[[nodiscard]] bool is_extended_pictographic(char32_t cp) noexcept;")
    h.append("")
    h.append(f"inline constexpr std::string_view kUnicodeVersion = \"{UNICODE_VERSION}\";")
    h.append("")
    h.append("}  // namespace nq::unicode")
    h.append("")

    cpp.append("// 由 tools/ucdgen/ucdgen.py 生成，请勿手改。")
    cpp.append(f"// Unicode 版本: {UNICODE_VERSION}")
    cpp.append('#include "unicode/unicode_tables.h"')
    cpp.append("")
    cpp.append("namespace nq::unicode {")
    cpp.append("namespace {")
    for enum_name, fn, _, table in props:
        stage1, blocks = build_property(table)
        emit_array_u16(f"k{enum_name}Stage1", stage1, cpp)
        emit_blocks(f"k{enum_name}Blocks", blocks, cpp)
        print(f"  {enum_name}: {len(stage1)} stage1, {len(blocks)} blocks "
              f"({len(blocks) * BLOCK // 1024 + len(stage1) * 2 // 1024} KiB)")
    stage1, blocks = build_property(extpict)
    emit_array_u16("kExtPictStage1", stage1, cpp)
    emit_blocks("kExtPictBlocks", blocks, cpp)
    print(f"  ExtPict: {len(stage1)} stage1, {len(blocks)} blocks")

    cpp.append("")
    cpp.append("[[nodiscard]] inline uint8_t lookup(const uint16_t* stage1, const uint8_t* blocks,")
    cpp.append("                                  char32_t cp) noexcept {")
    cpp.append("  const uint32_t block = stage1[cp >> 8];")
    cpp.append("  return blocks[static_cast<size_t>(block) * 256u + (cp & 0xFFu)];")
    cpp.append("}")
    cpp.append("}  // namespace")
    cpp.append("")

    def accessor(ret: str, name: str, stage1: str, blocks: str, enum_name: str) -> None:
        cpp.append(f"{ret} {name}(char32_t cp) noexcept {{")
        cpp.append("  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;")
        cpp.append(f"  return static_cast<{enum_name}>(lookup({stage1}, {blocks}, cp));")
        cpp.append("}")
        cpp.append("")

    accessor("Gcb", "gcb", "kGcbStage1", "&kGcbBlocks[0][0]", "Gcb")
    accessor("IncB", "incb", "kIncBStage1", "&kIncBBlocks[0][0]", "IncB")
    accessor("Lb", "lbc", "kLbStage1", "&kLbBlocks[0][0]", "Lb")
    accessor("Bc", "bidi_class", "kBcStage1", "&kBcBlocks[0][0]", "Bc")
    accessor("Gc", "general_category", "kGcStage1", "&kGcBlocks[0][0]", "Gc")
    cpp.append("bool is_extended_pictographic(char32_t cp) noexcept {")
    cpp.append("  if (cp > 0x10FFFF) return false;")
    cpp.append("  return lookup(kExtPictStage1, &kExtPictBlocks[0][0], cp) != 0;")
    cpp.append("}")
    cpp.append("")
    cpp.append("}  // namespace nq::unicode")
    cpp.append("")

    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "unicode_tables.h").write_text("\n".join(h), encoding="utf-8")
    (out_dir / "unicode_tables.cpp").write_text("\n".join(cpp), encoding="utf-8")
    total = sum(p.stat().st_size for p in [out_dir / "unicode_tables.h", out_dir / "unicode_tables.cpp"])
    print(f"wrote {out_dir}/unicode_tables.{{h,cpp}}  ({total // 1024} KiB)")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["fetch", "generate"])
    parser.add_argument("--ucd", default="build/ucd/" + UNICODE_VERSION)
    parser.add_argument("--out", default="src/unicode")
    args = parser.parse_args()

    ucd_dir = Path(args.ucd)
    if args.command == "fetch":
        fetch(ucd_dir)
        return 0
    generate(ucd_dir, Path(args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""PPM(P6) → PNG 转换（开发工具，用 zlib 压缩以便在文档中展示）

用法: python3 tools/ppm2png.py out/*.ppm   # 就地生成同名 .png
"""
import struct
import sys
import zlib
from pathlib import Path


def read_ppm(path: Path):
    data = path.read_bytes()
    parts = data.split(b"\n", 3)
    if parts[0] != b"P6":
        raise SystemExit(f"{path}: not a P6 ppm")
    width, height = map(int, parts[1].split())
    return width, height, parts[3]


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    def chunk(tag: bytes, body: bytes) -> bytes:
        return (struct.pack(">I", len(body)) + tag + body +
                struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    stride = width * 3
    raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(height))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


for arg in sys.argv[1:]:
    src = Path(arg)
    w, h, pixels = read_ppm(src)
    dst = src.with_suffix(".png")
    write_png(dst, w, h, pixels)
    print(f"{dst}  {w}x{h}  {dst.stat().st_size // 1024} KB")

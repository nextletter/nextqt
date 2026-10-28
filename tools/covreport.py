#!/usr/bin/env python3
"""NextQt 覆盖率报告（零第三方依赖，只用 gcov 的 JSON 输出）

用法:
  python3 tools/covreport.py --build build/coverage --src . --out docs/coverage --min-lines 95

产物:
  <out>/index.html      逐文件 + 逐行高亮的报告
  <out>/summary.json    机器可读汇总（CI 门禁用）
  <out>/badge.svg       shields 风格徽章（可直接放进 README）

退出码: 覆盖率低于阈值时返回 1（CI 直接失败）。
"""

from __future__ import annotations

import argparse
import gzip
import json
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

# 不计入分母：示例程序（人工/截图验证）、测试代码与测试框架（自测无意义）
EXCLUDE_PREFIXES = ("examples/", "tests/", "tools/", "build/")
# 只统计引擎自己的源文件与头文件（含头文件才能反映真实覆盖）
INCLUDE_DIRS = ("src/",)


@dataclass
class FileCov:
    name: str
    lines: dict[int, int] = field(default_factory=dict)      # line -> count
    branches: list[tuple[int, int, bool]] = field(default_factory=list)  # (line, count, taken)

    @property
    def line_total(self) -> int:
        return len(self.lines)

    @property
    def line_hit(self) -> int:
        return sum(1 for c in self.lines.values() if c > 0)

    @property
    def branch_total(self) -> int:
        return len(self.branches)

    @property
    def branch_hit(self) -> int:
        return sum(1 for _, _, taken in self.branches if taken)


def find_gcno(build: Path) -> list[Path]:
    return sorted(build.rglob("*.gcno"))


def source_for(gcno: Path, src_root: Path) -> str | None:
    marker = ".dir/"
    text = str(gcno)
    if marker not in text:
        return None
    rel = text.split(marker, 1)[1]
    if rel.endswith(".gcno"):
        rel = rel[: -len(".gcno")]
    if not (src_root / rel).exists():
        return None
    return rel


def run_gcov(gcno: Path, src_root: Path, rel: str) -> dict | None:
    # gcov 的 -o 需要指向具体目标文件（否则会去找 <stem>.gcno）
    obj = gcno.parent / (gcno.name[: -len(".gcno")] + ".o")  # xxx.cpp.gcno -> xxx.cpp.o
    with tempfile.TemporaryDirectory() as tmp:
        cmd = [
            "gcov",
            "-b",
            "-c",
            "-j",
            "-o",
            str(obj),
            str(src_root / rel),
        ]
        proc = subprocess.run(cmd, cwd=tmp, capture_output=True, text=True)
        if proc.returncode != 0:
            print(f"  [warn] gcov failed for {rel}: {proc.stderr.strip()[:200]}", file=sys.stderr)
            return None
        stem = Path(rel).name
        out = Path(tmp) / (stem + ".gcov.json.gz")
        if not out.exists():
            candidates = list(Path(tmp).glob("*.gcov.json.gz"))
            if not candidates:
                return None
            out = candidates[0]
        with gzip.open(out, "rt") as fh:
            return json.load(fh)


def classify(name: str, src_root: Path) -> str | None:
    if not name:
        return None
    path = Path(name).resolve()
    try:
        rel = path.relative_to(src_root).as_posix()
    except ValueError:
        return None  # 系统头文件
    if rel.startswith(EXCLUDE_PREFIXES) or not rel.startswith(INCLUDE_DIRS):
        return None
    return rel


def merge(cov: dict[str, FileCov], payload: dict, src_root: Path) -> None:
    """只接受本项目 src/ 下的源码与头文件（gcov 会一并输出系统头文件）。"""
    for file_obj in payload.get("files", []):
        rel = classify(file_obj.get("file", ""), src_root)
        if rel is None:
            continue
        entry = cov.setdefault(rel, FileCov(rel))
        for line in file_obj.get("lines", []):
            number = int(line["line_number"])
            count = int(line.get("count", 0))
            entry.lines[number] = max(entry.lines.get(number, 0), count)
            for branch in line.get("branches", []):
                entry.branches.append((number, int(branch.get("count", 0)),
                                       int(branch.get("count", 0)) > 0))


def pct(hit: int, total: int) -> float:
    return 100.0 * hit / total if total else 100.0


def badge_svg(value: float, label: str = "coverage") -> str:
    if value >= 95:
        color = "#2ea44f"
    elif value >= 90:
        color = "#97ca00"
    elif value >= 75:
        color = "#dfb317"
    else:
        color = "#e05d44"
    text = f"{value:.1f}%"
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="150" height="20" role="img" aria-label="{label}: {text}">
  <linearGradient id="s" x2="0" y2="100%">
    <stop offset="0" stop-color="#bbb" stop-opacity=".1"/>
    <stop offset="1" stop-opacity=".1"/>
  </linearGradient>
  <rect width="150" height="20" rx="3" fill="#555"/>
  <rect x="65" width="85" height="20" rx="3" fill="{color}"/>
  <rect x="65" width="85" height="20" rx="3" fill="url(#s)"/>
  <g fill="#fff" text-anchor="middle" font-family="Verdana,Geneva,DejaVu Sans,sans-serif" font-size="11">
    <text x="33" y="14">{label}</text>
    <text x="107" y="14">{text}</text>
  </g>
</svg>
"""


def render_html(cov: dict[str, FileCov], src_root: Path, total_lines: float,
                total_branches: float, min_lines: float) -> str:
    def bar(value: float) -> str:
        color = "#2ea44f" if value >= 95 else ("#dfb317" if value >= 75 else "#e05d44")
        return (f'<span class="bar"><span style="width:{value:.1f}%;background:{color}"></span></span>'
                f'<b>{value:.1f}%</b>')

    rows = []
    details = []
    for rel in sorted(cov):
        f = cov[rel]
        lp = pct(f.line_hit, f.line_total)
        bp = pct(f.branch_hit, f.branch_total)
        rows.append(
            f'<tr><td><a href="#{rel}">{rel}</a></td>'
            f'<td>{f.line_hit}/{f.line_total}</td><td>{bar(lp)}</td>'
            f'<td>{f.branch_hit}/{f.branch_total}</td><td>{bar(bp)}</td></tr>'
        )
        try:
            src_lines = (src_root / rel).read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            src_lines = []
        body = []
        for number, text in enumerate(src_lines, start=1):
            count = f.lines.get(number)
            if count is None:
                cls, badge_text = "neutral", ""
            elif count == 0:
                cls, badge_text = "miss", "0"
            else:
                cls, badge_text = "hit", str(count)
            body.append(
                f'<span class="ln {cls}"><i>{number:>4}</i><em>{badge_text:>6}</em>'
                f'<code>{text.replace("&", "&amp;").replace("<", "&lt;")}</code></span>'
            )
        details.append(
            f'<details id="{rel}"><summary>{rel} — {lp:.1f}% lines</summary>'
            f'<pre class="src">{"".join(body)}</pre></details>'
        )

    status = "PASS" if total_lines >= min_lines else "FAIL"
    return f"""<!doctype html>
<html lang="zh"><head><meta charset="utf-8">
<title>NextQt 覆盖率</title>
<style>
  body {{ font: 14px/1.5 -apple-system, "Segoe UI", "Noto Sans SC", sans-serif; margin: 32px; color:#1b1f24; }}
  h1 {{ font-size: 20px; margin: 0 0 4px; }}
  .muted {{ color:#6b7280; margin-bottom: 20px; }}
  .cards {{ display:flex; gap:12px; margin: 18px 0 24px; }}
  .card {{ border:1px solid #e5e7eb; border-radius:10px; padding:14px 18px; min-width:180px; }}
  .card b {{ font-size: 22px; display:block; }}
  .card span {{ color:#6b7280; font-size:12px; }}
  table {{ border-collapse: collapse; width: 100%; }}
  th, td {{ text-align:left; padding:6px 10px; border-bottom:1px solid #eef0f3; font-size:13px; }}
  th {{ color:#6b7280; font-weight:600; }}
  .bar {{ display:inline-block; width:110px; height:8px; background:#eef0f3; border-radius:4px; margin-right:8px; vertical-align:middle; }}
  .bar span {{ display:block; height:8px; border-radius:4px; }}
  details {{ margin: 10px 0; }}
  summary {{ cursor:pointer; font-family: ui-monospace, monospace; font-size:12px; }}
  pre.src {{ background:#fafbfc; border:1px solid #eef0f3; border-radius:8px; padding:10px; overflow:auto; max-height:460px; }}
  .ln {{ display:block; white-space:pre; font-family: ui-monospace, monospace; font-size:12px; }}
  .ln i {{ color:#9aa3af; font-style:normal; }}
  .ln em {{ color:#9aa3af; font-style:normal; margin-right:10px; }}
  .ln.hit {{ background:#e9f7ee; }}
  .ln.miss {{ background:#fdecec; }}
  .status {{ font-weight:700; color:{'#2ea44f' if status == 'PASS' else '#e05d44'}; }}
</style></head><body>
<h1>NextQt 覆盖率 <span class="status">{status}</span></h1>
<div class="muted">阈值 {min_lines:.0f}%（行） · 统计范围：<code>src/**</code>（不含 examples 与测试框架）</div>
<div class="cards">
  <div class="card"><b>{total_lines:.1f}%</b><span>行覆盖率</span></div>
  <div class="card"><b>{total_branches:.1f}%</b><span>分支覆盖率</span></div>
  <div class="card"><b>{len(cov)}</b><span>文件</span></div>
</div>
<table><thead><tr><th>文件</th><th>行</th><th>行覆盖</th><th>分支</th><th>分支覆盖</th></tr></thead>
<tbody>{''.join(rows)}</tbody></table>
<h2 style="font-size:16px;margin-top:28px">逐行</h2>
{''.join(details)}
</body></html>
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True, help="coverage 构建目录")
    parser.add_argument("--src", default=".", help="源码根目录")
    parser.add_argument("--out", required=True, help="报告输出目录")
    parser.add_argument("--min-lines", type=float, default=95.0)
    parser.add_argument("--min-branches", type=float, default=0.0)
    args = parser.parse_args()

    build = Path(args.build).resolve()
    src_root = Path(args.src).resolve()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    gcno_files = find_gcno(build)
    if not gcno_files:
        print(f"no .gcno under {build}; build with --coverage first", file=sys.stderr)
        return 1

    cov: dict[str, FileCov] = {}
    for gcno in gcno_files:
        rel = source_for(gcno, src_root)
        if rel is None or rel.startswith(EXCLUDE_PREFIXES):
            continue
        payload = run_gcov(gcno, src_root, rel)
        if payload is not None:
            merge(cov, payload, src_root)

    line_total = sum(f.line_total for f in cov.values())
    line_hit = sum(f.line_hit for f in cov.values())
    branch_total = sum(f.branch_total for f in cov.values())
    branch_hit = sum(f.branch_hit for f in cov.values())
    total_lines = pct(line_hit, line_total)
    total_branches = pct(branch_hit, branch_total)

    (out / "index.html").write_text(
        render_html(cov, src_root, total_lines, total_branches, args.min_lines), encoding="utf-8")
    (out / "badge.svg").write_text(badge_svg(total_lines), encoding="utf-8")
    summary = {
        "lines": {"hit": line_hit, "total": line_total, "percent": round(total_lines, 2)},
        "branches": {"hit": branch_hit, "total": branch_total,
                     "percent": round(total_branches, 2)},
        "thresholds": {"min_lines": args.min_lines, "min_branches": args.min_branches},
        "files": {rel: {"lines": {"hit": f.line_hit, "total": f.line_total},
                        "branches": {"hit": f.branch_hit, "total": f.branch_total}}
                  for rel, f in sorted(cov.items())},
    }
    (out / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

    print(f"lines    {line_hit}/{line_total}  {total_lines:.2f}%  (threshold {args.min_lines:.0f}%)")
    print(f"branches {branch_hit}/{branch_total}  {total_branches:.2f}%")
    print(f"report   {out / 'index.html'}")
    print(f"badge    {out / 'badge.svg'}")

    ok = total_lines >= args.min_lines and total_branches >= args.min_branches
    if not ok:
        print(f"FAILED: coverage below threshold", file=sys.stderr)
        return 1
    print("OK: coverage gate passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

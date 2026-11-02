# UCD 测试数据

| 文件 | 来源 | sha256 |
|------|------|--------|
| `GraphemeBreakTest-16.0.0.txt` | https://www.unicode.org/Public/16.0.0/ucd/auxiliary/GraphemeBreakTest.txt | `ee2b9354d270ac061b29f09662cafea06341d77e704b8cc6bd72aaeeda363cb5` |

UNICODE LICENSE V3 — 见 https://www.unicode.org/license.txt
数据文件仅用于一致性测试；生成的表在 `src/unicode/unicode_tables.cpp`，
重新生成：`python3 tools/ucdgen/ucdgen.py fetch && python3 tools/ucdgen/ucdgen.py generate`

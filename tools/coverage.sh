#!/usr/bin/env bash
# 覆盖率门禁：配置 → 构建 → 跑测试 → 生成报告 → 低于阈值直接失败
set -euo pipefail
cd "$(dirname "$0")/.."

MIN_LINES="${MIN_LINES:-95}"
MIN_BRANCHES="${MIN_BRANCHES:-70}"
BUILD_DIR="build/coverage"

cmake --preset coverage >/dev/null
cmake --build --preset coverage -j"$(nproc)" >/dev/null
# 清理上一次的 profile，避免 "different timestamp" 警告
find "${BUILD_DIR}" -name '*.gcda' -delete 2>/dev/null || true
ctest --preset coverage --output-on-failure

# 集成覆盖：把 8 个 showcase 场景真实跑一遍（覆盖全部 opcode / 阴影 / 渐变 / 文本）
"${BUILD_DIR}/nq_showcase" --out "${BUILD_DIR}/showcase" --no-png > /dev/null

python3 tools/covreport.py \
  --build "${BUILD_DIR}" \
  --src . \
  --out docs/coverage \
  --min-lines "${MIN_LINES}" \
  --min-branches "${MIN_BRANCHES}"

#!/bin/bash
#
# 快速对比测试 — 用于日常开发后的快速回归验证
#
# 相比 run_benchmark.sh (默认 4 档连接 × 3 轮 × 10 秒 ≈ 4 分钟):
#   本脚本: 4 档连接 × 1 轮 × 5 秒 ≈ 40 秒
#
# 用法:
#   cd benchmark && bash run_quick_test.sh
#   bash run_quick_test.sh --sessions "1 10"      # 只测部分连接数
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

exec bash "$SCRIPT_DIR/run_benchmark.sh" \
    --sessions "${SESSIONS:-1 10 100 1000}" \
    --runs 1 \
    --time 5 \
    "$@"

#!/bin/bash
#
# 一键编译 benchmark 所有二进制 (Release 模式)
#
# 用法:
#   cd benchmark && bash build.sh          # Release 编译
#   bash build.sh clean                    # 清理 build 目录
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

if [ "${1:-}" = "clean" ]; then
    echo "[CLEAN] 删除 $BUILD_DIR ..."
    rm -rf "$BUILD_DIR"
    echo "[CLEAN] 完成."
    exit 0
fi

for cmd in cmake g++; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "[ERROR] 缺少依赖: $cmd"
        echo "  Ubuntu/Debian: sudo apt install build-essential cmake"
        exit 1
    fi
done

echo "[BUILD] Release 编译 (-O2 -DNDEBUG -march=native) ..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Release "$SCRIPT_DIR"
make -j"$(nproc)"

echo ""
echo "[BUILD] 编译完成. 生成二进制:"
ls -lh "$BUILD_DIR"/muduo_pingpong_server \
       "$BUILD_DIR"/muduo_pingpong_client \
       "$BUILD_DIR"/cpp17_pingpong_server 2>/dev/null
echo ""
echo "用法示例:"
echo "  bash run_quick_test.sh          # 快速测试 (~40 秒)"
echo "  bash run_benchmark.sh           # 完整测试 (~4 分钟)"
echo "  bash run_benchmark.sh --threads \"1 2\"  # 多线程测试"

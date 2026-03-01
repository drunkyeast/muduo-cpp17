#!/bin/bash
#
# Muduo 原版 vs C++17 魔改版 Ping Pong 吞吐量对比测试
#
# 用法:
#   cd benchmark && bash run_benchmark.sh                       # 默认: 单线程, 连接数 1 10 100 1000, 3 轮
#   bash run_benchmark.sh --sessions "1 10 100" --runs 5 --time 15
#   bash run_benchmark.sh --threads "1 2 4"                     # 多线程测试
#
# 前置条件: cmake, g++ (支持 C++17), Linux (epoll), bc, taskset
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
RESULT_DIR="$SCRIPT_DIR/results"

PORT=12345
BUFSIZE=16384
TIMEOUT=10
SESSIONS_LIST="1 10 100 1000"
THREADS_LIST="1"
RUNS=3

# ======================== 解析命令行参数 ========================
while [[ $# -gt 0 ]]; do
    case "$1" in
        --sessions) SESSIONS_LIST="$2"; shift 2 ;;
        --threads)  THREADS_LIST="$2"; shift 2 ;;
        --runs)     RUNS="$2"; shift 2 ;;
        --time)     TIMEOUT="$2"; shift 2 ;;
        --port)     PORT="$2"; shift 2 ;;
        --bufsize)  BUFSIZE="$2"; shift 2 ;;
        --help|-h)
            echo "用法: bash run_benchmark.sh [选项]"
            echo "  --sessions \"1 10 100 1000\"  连接数列表 (默认: 1 10 100 1000)"
            echo "  --threads  \"1 2 4\"          服务端线程数列表 (默认: 1)"
            echo "  --runs N                     每档重复次数 (默认: 3)"
            echo "  --time N                     每轮测试秒数 (默认: 10)"
            echo "  --port N                     端口号 (默认: 12345)"
            echo "  --bufsize N                  消息大小字节 (默认: 16384)"
            exit 0 ;;
        *) echo "[ERROR] 未知参数: $1"; exit 1 ;;
    esac
done

MUDUO_SERVER="$BUILD_DIR/muduo_pingpong_server"
MUDUO_CLIENT="$BUILD_DIR/muduo_pingpong_client"
CPP17_SERVER="$BUILD_DIR/cpp17_pingpong_server"

# ======================== 依赖检查 ========================
check_deps() {
    local missing=()
    for cmd in cmake g++ bc taskset ss; do
        if ! command -v "$cmd" &>/dev/null; then
            missing+=("$cmd")
        fi
    done
    if [ ${#missing[@]} -gt 0 ]; then
        echo "[ERROR] 缺少依赖: ${missing[*]}"
        echo "  Ubuntu/Debian: sudo apt install build-essential cmake bc util-linux iproute2"
        exit 1
    fi
}

# ======================== 系统信息 ========================
print_sysinfo() {
    echo "============================================================"
    echo "  系统环境信息"
    echo "============================================================"
    echo "  主机名:    $(hostname)"
    echo "  内核:      $(uname -r)"
    echo "  CPU:       $(lscpu 2>/dev/null | grep 'Model name' | sed 's/.*:\s*//' || echo 'unknown')"
    echo "  CPU 核数:  $(nproc)"
    echo "  内存:      $(free -h 2>/dev/null | awk '/Mem:/{print $2}' || echo 'unknown')"
    echo "  编译器:    $(g++ --version | head -1)"
    echo "  CMake:     $(cmake --version | head -1)"
    echo "  日期:      $(date '+%Y-%m-%d %H:%M:%S')"
    echo "============================================================"
    echo ""
}

# ======================== 编译 ========================
build_all() {
    echo "[BUILD] 编译所有二进制 (Release -O2 -march=native) ..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake -DCMAKE_BUILD_TYPE=Release "$SCRIPT_DIR" 2>&1 | tail -3
    make -j"$(nproc)" 2>&1 | tail -5
    cd "$SCRIPT_DIR"

    for bin in "$MUDUO_SERVER" "$MUDUO_CLIENT" "$CPP17_SERVER"; do
        if [ ! -x "$bin" ]; then
            echo "[ERROR] 编译失败, 找不到: $bin"
            exit 1
        fi
    done
    echo "[BUILD] 编译完成."
    echo ""
}

# ======================== CPU 绑定策略 ========================
# 将可用核心对半分: 下半部分给 server, 上半部分给 client, 避免互相争抢
NUM_CPUS=$(nproc)
HALF_CPUS=$((NUM_CPUS / 2))

get_taskset_server() {
    local threads="$1"
    if [ "$NUM_CPUS" -lt 2 ]; then
        echo ""
        return
    fi
    local max_core=$(( threads - 1 ))
    if [ "$max_core" -ge "$HALF_CPUS" ]; then
        max_core=$(( HALF_CPUS - 1 ))
    fi
    echo "taskset -c 0-${max_core}"
}

get_taskset_client() {
    if [ "$NUM_CPUS" -lt 2 ]; then
        echo ""
        return
    fi
    echo "taskset -c ${HALF_CPUS}-$((NUM_CPUS - 1))"
}

# ======================== 工具函数 ========================
cleanup() {
    pkill -f "pingpong_server.*$PORT" 2>/dev/null || true
    sleep 0.5
}

wait_port_free() {
    for _ in $(seq 1 30); do
        if ! ss -tlnp 2>/dev/null | grep -q ":${PORT} "; then
            return 0
        fi
        sleep 0.5
    done
    echo "[WARN] 端口 $PORT 仍被占用, 尝试强制清理"
    cleanup
    sleep 1
}

run_one_test() {
    local server_bin="$1"
    local server_name="$2"
    local sessions="$3"
    local run_id="$4"
    local threads="$5"
    local outfile="$RESULT_DIR/${server_name}_t${threads}_s${sessions}_r${run_id}.txt"
    local taskset_srv taskset_cli

    taskset_srv=$(get_taskset_server "$threads")
    taskset_cli=$(get_taskset_client)

    wait_port_free

    $taskset_srv "$server_bin" 127.0.0.1 "$PORT" "$threads" &
    local srv_pid=$!
    sleep 1

    if ! kill -0 "$srv_pid" 2>/dev/null; then
        echo "    [ERROR] Server 启动失败: $server_name"
        return 1
    fi

    $taskset_cli "$MUDUO_CLIENT" 127.0.0.1 "$PORT" 1 "$BUFSIZE" "$sessions" "$TIMEOUT" \
        > "$outfile" 2>&1 || true

    kill "$srv_pid" 2>/dev/null || true
    wait "$srv_pid" 2>/dev/null || true
    sleep 0.5

    local throughput
    throughput=$(grep -oP '[\d.]+ MiB/s' "$outfile" | tail -1 | grep -oP '[\d.]+' || echo "N/A")
    printf "    %-8s t=%-2s run %d/%d: %s MiB/s\n" "$server_name" "$threads" "$run_id" "$RUNS" "$throughput"
}

calc_avg() {
    local server_name="$1"
    local threads="$2"
    local sessions="$3"
    local sum=0
    local count=0
    for r in $(seq 1 "$RUNS"); do
        local f="$RESULT_DIR/${server_name}_t${threads}_s${sessions}_r${r}.txt"
        if [ -f "$f" ]; then
            local val
            val=$(grep -oP '[\d.]+ MiB/s' "$f" | tail -1 | grep -oP '[\d.]+' || echo "0")
            if [ "$val" != "0" ] && [ -n "$val" ]; then
                sum=$(echo "$sum + $val" | bc)
                count=$((count + 1))
            fi
        fi
    done
    if [ "$count" -gt 0 ]; then
        echo "scale=1; $sum / $count" | bc
    else
        echo "0"
    fi
}

# ======================== 生成 Markdown 报告 ========================
generate_report() {
    local report="$RESULT_DIR/report.md"
    {
        echo "# Ping Pong 吞吐量对比测试报告"
        echo ""
        echo "生成时间: $(date '+%Y-%m-%d %H:%M:%S')"
        echo ""
        echo "## 测试环境"
        echo ""
        echo "| 项目 | 值 |"
        echo "|------|-----|"
        echo "| 主机名 | $(hostname) |"
        echo "| 内核 | $(uname -r) |"
        echo "| CPU | $(lscpu 2>/dev/null | grep 'Model name' | sed 's/.*:\s*//' || echo 'unknown') |"
        echo "| CPU 核数 | $(nproc) |"
        echo "| 内存 | $(free -h 2>/dev/null | awk '/Mem:/{print $2}' || echo 'unknown') |"
        echo "| 编译器 | $(g++ --version | head -1) |"
        echo "| 消息大小 | ${BUFSIZE} bytes |"
        echo "| 每轮时间 | ${TIMEOUT} 秒 |"
        echo "| 重复次数 | ${RUNS} |"
        echo ""

        for threads in $THREADS_LIST; do
            echo "## 服务端线程数: $threads"
            echo ""
            echo "| 连接数 | 原版 muduo (MiB/s) | C++17 魔改 (MiB/s) | 差异 |"
            echo "|--------|--------------------|--------------------|------|"

            for sessions in $SESSIONS_LIST; do
                local avg_muduo avg_cpp17 diff_pct
                avg_muduo=$(calc_avg "muduo" "$threads" "$sessions")
                avg_cpp17=$(calc_avg "cpp17" "$threads" "$sessions")
                if [ "$(echo "$avg_muduo > 0" | bc)" -eq 1 ]; then
                    diff_pct=$(echo "scale=1; ($avg_cpp17 - $avg_muduo) * 100 / $avg_muduo" | bc)
                    if [ "$(echo "$diff_pct >= 0" | bc)" -eq 1 ]; then
                        diff_pct="+${diff_pct}%"
                    else
                        diff_pct="${diff_pct}%"
                    fi
                else
                    diff_pct="N/A"
                fi
                echo "| $sessions | $avg_muduo | $avg_cpp17 | $diff_pct |"
            done
            echo ""
        done

        echo "## 每轮详细数据"
        echo ""
        for threads in $THREADS_LIST; do
            for sessions in $SESSIONS_LIST; do
                echo "### 线程=$threads, 连接数=$sessions"
                echo ""
                echo "| Run | 原版 (MiB/s) | C++17 (MiB/s) |"
                echo "|-----|-------------|---------------|"
                for r in $(seq 1 "$RUNS"); do
                    local mf="$RESULT_DIR/muduo_t${threads}_s${sessions}_r${r}.txt"
                    local cf="$RESULT_DIR/cpp17_t${threads}_s${sessions}_r${r}.txt"
                    local m_val c_val
                    m_val=$(grep -oP '[\d.]+ MiB/s' "$mf" 2>/dev/null | tail -1 | grep -oP '[\d.]+' || echo "N/A")
                    c_val=$(grep -oP '[\d.]+ MiB/s' "$cf" 2>/dev/null | tail -1 | grep -oP '[\d.]+' || echo "N/A")
                    echo "| $r | $m_val | $c_val |"
                done
                echo ""
            done
        done
    } > "$report"
    echo "[REPORT] Markdown 报告已保存: $report"
}

# ======================== 主流程 ========================
main() {
    check_deps
    build_all
    print_sysinfo

    mkdir -p "$RESULT_DIR"
    rm -f "$RESULT_DIR"/*.txt "$RESULT_DIR"/report.md

    local cpu_bind_info="无绑核 (仅 ${NUM_CPUS} 核)"
    if [ "$NUM_CPUS" -ge 2 ]; then
        cpu_bind_info="server=CPU 0~$((HALF_CPUS-1)) (按线程数), client=CPU ${HALF_CPUS}~$((NUM_CPUS-1))"
    fi

    echo "============================================================"
    echo "  Muduo 原版 vs C++17 魔改版  Ping Pong 吞吐量对比"
    echo "============================================================"
    echo "  消息大小:   ${BUFSIZE} bytes"
    echo "  每轮时间:   ${TIMEOUT} 秒"
    echo "  重复次数:   ${RUNS}"
    echo "  连接数:     ${SESSIONS_LIST}"
    echo "  服务端线程: ${THREADS_LIST}"
    echo "  CPU 绑定:   ${cpu_bind_info}"
    echo "  客户端:     统一使用原版 muduo (消除客户端侧变量)"
    echo "============================================================"
    echo ""

    trap cleanup EXIT

    for threads in $THREADS_LIST; do
        echo "==================== 服务端线程数: $threads ===================="
        for sessions in $SESSIONS_LIST; do
            echo ">>> $sessions 连接, $threads 线程 ..."
            for r in $(seq 1 "$RUNS"); do
                run_one_test "$MUDUO_SERVER" "muduo" "$sessions" "$r" "$threads"
            done
            for r in $(seq 1 "$RUNS"); do
                run_one_test "$CPP17_SERVER" "cpp17" "$sessions" "$r" "$threads"
            done
            echo ""
        done
    done

    echo "============================================================"
    echo "  测试结果汇总 (${RUNS} 轮平均)"
    echo "============================================================"

    for threads in $THREADS_LIST; do
        echo ""
        echo "--- 服务端线程: $threads ---"
        printf "%-12s %16s %16s %10s\n" "连接数" "原版(MiB/s)" "魔改(MiB/s)" "差异"
        echo "------------------------------------------------------------"

        for sessions in $SESSIONS_LIST; do
            avg_muduo=$(calc_avg "muduo" "$threads" "$sessions")
            avg_cpp17=$(calc_avg "cpp17" "$threads" "$sessions")
            if [ "$(echo "$avg_muduo > 0" | bc)" -eq 1 ]; then
                diff_pct=$(echo "scale=1; ($avg_cpp17 - $avg_muduo) * 100 / $avg_muduo" | bc)
                if [ "$(echo "$diff_pct >= 0" | bc)" -eq 1 ]; then
                    diff_pct="+${diff_pct}%"
                else
                    diff_pct="${diff_pct}%"
                fi
            else
                diff_pct="N/A"
            fi
            printf "%-12s %16s %16s %10s\n" "$sessions" "$avg_muduo" "$avg_cpp17" "$diff_pct"
        done
    done

    echo ""
    echo "============================================================"

    echo ""
    echo "--- 每轮详细数据 ---"
    for threads in $THREADS_LIST; do
        for sessions in $SESSIONS_LIST; do
            echo "  线程=$threads, 连接数=$sessions:"
            for r in $(seq 1 "$RUNS"); do
                mf="$RESULT_DIR/muduo_t${threads}_s${sessions}_r${r}.txt"
                cf="$RESULT_DIR/cpp17_t${threads}_s${sessions}_r${r}.txt"
                m_val=$(grep -oP '[\d.]+ MiB/s' "$mf" 2>/dev/null | tail -1 || echo "N/A")
                c_val=$(grep -oP '[\d.]+ MiB/s' "$cf" 2>/dev/null | tail -1 || echo "N/A")
                printf "    Run %d: muduo=%-16s cpp17=%-16s\n" "$r" "$m_val" "$c_val"
            done
        done
    done

    generate_report

    echo ""
    echo "详细输出保存在: $RESULT_DIR/"
    echo "Done!"
}

main "$@"

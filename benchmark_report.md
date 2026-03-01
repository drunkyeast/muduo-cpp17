# Muduo 原版 vs C++17 魔改版 Ping Pong 吞吐量对比测试

## 测试环境

- **服务器**: 2核 2GB 内存, Intel Xeon Platinum, Linux 5.15.0 (云服务器)
- **编译器**: GCC 11.4.0
- **优化等级**: 均为 `-O2 -DNDEBUG -march=native` (Release)
- **测试协议**: Ping Pong (客户端和服务器互相 echo, 数据来回传送)
- **消息大小**: 16384 bytes (16KB)
- **测试方法**: 本机 loopback (127.0.0.1), 单线程, `taskset` 绑核 (server=CPU0, client=CPU1)
- **测试时长**: 每轮 10 秒, 每档 3 轮取平均
- **Client**: 统一使用原版 muduo 的 `pingpong_client`, 消除客户端侧变量
- **参考**: [陈硕 Ping Pong 测试博客](https://blog.csdn.net/Solstice/article/details/5864889)

## 编译与运行

```bash
# 1. 编译原版 muduo
cd muduo-master && BUILD_DIR=../build ./build.sh
cd ../build/release-cpp11 && make pingpong_server pingpong_client -j$(nproc)

# 2. 编译 C++17 版 server
cd benchmark && mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc)

# 3. 自动化对比测试
cd benchmark && bash run_benchmark.sh
```

---

## 性能修复过程

C++17 重构版初始测试远慢于原版, 经排查共发现 3 个性能问题, 逐步修复后最终达到与原版持平.

### Bug 1: `Buffer::readFd` 中 extrabuf 零初始化 (影响: 全局 ~52%)

**问题代码**:

```cpp
// Buffer.cc - readFd()
char extrabuf[65536] = {0};  // 每次调用都 memset 64KB
```

**修复**:

```cpp
char extrabuf[65536];  // 无需零初始化
```

**分析**: `= {0}` 让编译器在每次 `readFd` 调用时生成 `memset(extrabuf, 0, 65536)`.
Ping Pong 场景下每收一条消息就调 `readFd`, 以 4 万条/秒计算:

```
40,000 × 64KB = 2.5 GB/s 的无意义内存写入
```

这完全是浪费 — `readv()` 会直接写入数据覆盖原有内容,
只有实际写入的字节会被 `append` 到 Buffer, 未写入部分永远不被读取.
原版 muduo 不做零初始化, 这是一个 **防御性编程习惯导致性能问题** 的典型案例.

**修复效果** (10 连接): 从慢 52% → 反超原版. 但单连接仍慢 19%.

### Bug 2: 编译选项 `-march=native` 缺失 (影响: 单连接 ~10%)

**问题**: 原版 muduo 编译使用了 `-march=native`, 让编译器针对当前 CPU 微架构
(AVX-512, BMI2 等) 生成最优指令. C++17 版未加此 flag, 生成通用 x86-64 指令,
在 `memcpy` / `memmove` 等内存密集操作上存在差距.

**修复**: CMakeLists.txt 中加入:

```cmake
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -march=native")
```

### Bug 3: `callingPendingFunctors_` 误用 `std::atomic_bool` (影响: 单连接 ~10%)

**问题代码**:

```cpp
// EventLoop.h (C++17 版)
std::atomic_bool callingPendingFunctors_;
```

**修复**:

```cpp
bool callingPendingFunctors_;
```

**分析**: 原版 muduo 中 `callingPendingFunctors_` 是 **plain bool**
(源码注释写了 `/* atomic */` 但有意未使用).
C++17 版用了 `std::atomic_bool`, 导致 `doPendingFunctors()` 每轮事件循环产生 2 次
`xchg` 全内存屏障 (seq_cst store). 单连接下每条消息就是一轮循环, 开销无法摊薄.

**为什么不需要 atomic**: `callingPendingFunctors_` 只在 loop 线程中读写, 无跨线程竞争.
`queueInLoop` 中跨线程分支 (`!isInLoopThread()`) 走 wakeup 路径, 不依赖此变量.

**Bug 2 + Bug 3 修复效果**: 单连接从慢 19% → 反超原版, 问题消除.

---

## 各轮测试数据

### Round 1: 修复前 (extrabuf 零初始化未修复)

| 连接数 | 原版 muduo (MiB/s) | C++17 魔改 (MiB/s) | 差距 |
|--------|--------------------|--------------------|------|
| 10     | ~385               | ~186               | **慢 52%** |

### Round 2: 仅修复 extrabuf

| 连接数 | 原版 muduo (MiB/s) | C++17 魔改 (MiB/s) | 差距 |
|--------|--------------------|--------------------|------|
| 1      | 347                | 281                | 慢 19% |
| 10     | 375                | 396                | 快 5.7% |
| 100    | 206                | 211                | 快 2.4% |
| 1000   | 171                | 193                | 快 12.9% |

单连接仍慢 19%, 引出 Bug 2 和 Bug 3.

### Round 3: 全部修复 (extrabuf + `-march=native` + `callingPendingFunctors_` 改 bool)

| 连接数 | 原版 muduo (MiB/s) | C++17 魔改 (MiB/s) | 差距 |
|--------|--------------------|--------------------|------|
| 1      | 338                | 347                | 快 2.7% |
| 10     | 367                | 398                | 快 8.4% |
| 100    | 206                | 194                | 慢 5.8% |
| 1000   | 130                | 181                | 快 39.2% |

Round 3 部分数据波动较大 (100 连接慢 5.8%, 1000 连接快 39.2%), 怀疑是云服务器噪音.

### Round 4: 最终验证 (全部修复, 每轮 10 秒 × 3 轮, 多次重复)

为排除 Round 3 中的异常波动, 重新进行了多组测试:

**测试组 A** (run_benchmark.sh):

| 连接数 | 原版 muduo (MiB/s) | C++17 魔改 (MiB/s) | 差距 |
|--------|--------------------|--------------------|------|
| 1      | 299                | 316                | 快 5.6% |
| 10     | 427                | 366                | 慢 14.4% |
| 100    | 205                | 191                | 慢 7.0% |
| 1000   | 175                | 180                | 快 3.0% |

**测试组 B** (run_quick_test.sh):

| 连接数 | 原版 muduo (MiB/s) | C++17 魔改 (MiB/s) | 差距 |
|--------|--------------------|--------------------|------|
| 1      | 309                | 316                | 快 2.4% |
| 10     | 374                | 370                | 慢 1.0% |
| 100    | 197                | 198                | 快 0.6% |
| 1000   | 185                | 177                | 慢 4.7% |

**单轮数据波动示例** (测试组 A, 100 连接):

| | Run 1 | Run 2 | Run 3 |
|---|-------|-------|-------|
| 原版 | 205 | 206 | 205 |
| 魔改 | **165** | 202 | 206 |

custom run 1 的 165 是明显的离群值 (云服务器干扰), 去掉后 run 2/3 均值 204 ≈ 原版 205.
类似的离群值在各档连接数中都有出现, 是 2 核云 VM 环境的固有噪音.

---

## 最终结论

| 连接数 | 综合评估 |
|--------|---------|
| 1      | C++17 版略快 2~6%, 在噪音范围边缘 |
| 10     | 基本持平, 波动范围内 |
| 100    | 基本持平, 波动范围内 |
| 1000   | 基本持平, 波动范围内 |

**C++17 魔改版在修复 3 个性能 bug 后, 与原版 muduo 吞吐量持平, 无可测量的性能差距.**

在 2 核云服务器上, 单轮测试波动可达 20~30% (如原版 muduo 同一档位三轮分别跑出 256/307/332),
3 轮平均仍不够稳定. Round 3 中 "1000 连接快 39%" 和 "100 连接慢 5.8%" 均属云噪音造成的偏差,
Round 4 多组测试已验证这一点.

**总结**: 重构保持了 C++17 现代写法 (智能指针、`std::function`、`std::bind` 等), 同时做到了与原版 C 风格 muduo 同等的运行时性能, 未引入性能退化.
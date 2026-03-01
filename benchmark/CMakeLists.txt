cmake_minimum_required(VERSION 3.10)
project(pingpong_benchmark)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 与原版 muduo 编译 flags 对齐 (必须一致才有意义)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -march=native")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "CXX flags (Release): ${CMAKE_CXX_FLAGS_RELEASE}")

# 所有可执行文件输出到 build 目录根
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ======================== 原版 muduo 静态库 ========================
set(MUDUO_ORIGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/muduo-origin)

if(NOT EXISTS ${MUDUO_ORIGIN_DIR}/muduo/base)
    message(FATAL_ERROR
        "找不到原版 muduo 源码: ${MUDUO_ORIGIN_DIR}/muduo/base\n"
        "请确保 muduo-origin/ 目录完整 (git clone 时不要漏掉).")
endif()

file(GLOB MUDUO_BASE_SRCS   ${MUDUO_ORIGIN_DIR}/muduo/base/*.cc)
file(GLOB MUDUO_NET_SRCS    ${MUDUO_ORIGIN_DIR}/muduo/net/*.cc)
file(GLOB MUDUO_POLLER_SRCS ${MUDUO_ORIGIN_DIR}/muduo/net/poller/*.cc)

add_library(muduo_origin STATIC
    ${MUDUO_BASE_SRCS}
    ${MUDUO_NET_SRCS}
    ${MUDUO_POLLER_SRCS}
)
target_include_directories(muduo_origin PUBLIC ${MUDUO_ORIGIN_DIR})
target_compile_options(muduo_origin PRIVATE -Wno-deprecated-declarations)

# ======================== C++17 魔改版静态库 ========================
set(MUDUO_CPP17_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

file(GLOB MUDUO_CPP17_SRCS ${MUDUO_CPP17_DIR}/src/*.cc)

add_library(muduo_cpp17 STATIC ${MUDUO_CPP17_SRCS})
target_include_directories(muduo_cpp17 PUBLIC ${MUDUO_CPP17_DIR}/include)

# ======================== 可执行文件 ========================

# 原版 muduo pingpong server
add_executable(muduo_pingpong_server muduo_pingpong_server.cc)
target_link_libraries(muduo_pingpong_server muduo_origin pthread)

# 原版 muduo pingpong client (两个版本共用, 消除客户端侧变量)
add_executable(muduo_pingpong_client muduo_pingpong_client.cc)
target_link_libraries(muduo_pingpong_client muduo_origin pthread)

# C++17 魔改版 pingpong server
add_executable(cpp17_pingpong_server cpp17_pingpong_server.cc)
target_link_libraries(cpp17_pingpong_server muduo_cpp17 pthread)

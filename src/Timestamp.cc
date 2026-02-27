#include <chrono> // 获取微妙级的时间
#include <time.h> // 格式化年月日

#include "Timestamp.h"

Timestamp Timestamp::now()
{
    using namespace std::chrono;
    const auto us = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
    return Timestamp(us);
}

std::string Timestamp::toString() const
{
    char buf[64];
    // 1. 分离出秒
    const time_t seconds = microSecondsSinceEpoch_ / 1'000'000;
    // 2. 分离出微秒
    const int microseconds = microSecondsSinceEpoch_ % 1'000'000; // 计算剩余的微秒数
    tm tm_time{};
    localtime_r(&seconds, &tm_time); // 负责把“时间戳”转换成“年月日时分秒”的结构体 tm。可惜没有微妙.
    // 3. 拼接: 秒级时间 + 微秒
    snprintf(buf, sizeof(buf), "%4d/%02d/%02d %02d:%02d:%02d.%06d",
             tm_time.tm_year + 1900,
             tm_time.tm_mon + 1,
             tm_time.tm_mday,
             tm_time.tm_hour,
             tm_time.tm_min,
             tm_time.tm_sec,
             microseconds); // 这里填微秒.
    return buf;
}

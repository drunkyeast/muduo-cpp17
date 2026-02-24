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
    const time_t seconds = microSecondsSinceEpoch_ / 1'000'000;
    tm tm_time{};
    localtime_r(&seconds, &tm_time);
    snprintf(buf, sizeof(buf), "%4d/%02d/%02d %02d:%02d:%02d",
             tm_time.tm_year + 1900,
             tm_time.tm_mon + 1,
             tm_time.tm_mday,
             tm_time.tm_hour,
             tm_time.tm_min,
             tm_time.tm_sec);
    return buf;
}

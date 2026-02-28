#pragma once

#include <cstdio> // 用到了snprintf

#include "noncopyable.h"

// LOG_INFO("%s %d", arg1, arg2)
// ##__VA_ARGS__ 是 GCC 扩展，处理可变参数为空时消除多余逗号
// do while(0) 使宏在语法上等价于一条语句，支持 LOG_INFO("x"); 这样的写法
#define LOG_INFO(logmsgFormat, ...)                                         \
    do                                                                      \
    {                                                                       \
        char buf[1024];                                                     \
        snprintf(buf, sizeof(buf), logmsgFormat, ##__VA_ARGS__);            \
        Logger::instance().log(LogLevel::INFO, buf);                        \
    } while (0)

#define LOG_ERROR(logmsgFormat, ...)                                        \
    do                                                                      \
    {                                                                       \
        char buf[1024];                                                     \
        snprintf(buf, sizeof(buf), logmsgFormat, ##__VA_ARGS__);            \
        Logger::instance().log(LogLevel::ERROR, buf);                       \
    } while (0)

#define LOG_FATAL(logmsgFormat, ...)                                        \
    do                                                                      \
    {                                                                       \
        char buf[1024];                                                     \
        snprintf(buf, sizeof(buf), logmsgFormat, ##__VA_ARGS__);            \
        Logger::instance().log(LogLevel::FATAL, buf);                       \
        exit(-1);                                                           \
    } while (0)

#ifdef MUDEBUG
#define LOG_DEBUG(logmsgFormat, ...)                                        \
    do                                                                      \
    {                                                                       \
        char buf[1024];                                                     \
        snprintf(buf, sizeof(buf), logmsgFormat, ##__VA_ARGS__);            \
        Logger::instance().log(LogLevel::DEBUG, buf);                       \
    } while (0)
#else
#define LOG_DEBUG(logmsgFormat, ...)
#endif

enum class LogLevel
{
    INFO,
    ERROR,
    FATAL,
    DEBUG,
};

class Logger : noncopyable
{
public:
    static Logger &instance();
    void log(LogLevel level, const char* msg);
};

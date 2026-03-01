#pragma once

#include <cstdio> // 用到了snprintf

#include "noncopyable.h"

// LOG_INFO("%s %d", arg1, arg2)
// ##__VA_ARGS__ 是 GCC 扩展，处理可变参数为空时消除多余逗号
// do while(0) 使宏在语法上等价于一条语句，支持 LOG_INFO("x"); 这样的写法
// TODO: 可用 C++17 变参模板 + fold expression 替代宏, 实现类型安全的日志:
//   template<typename... Args> void log_info(std::string_view fmt, Args&&... args);
//   或引入 fmtlib/std::format(C++20) 彻底告别 snprintf.
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
    DEBUG = 0,
    INFO = 1,
    ERROR = 2,
    FATAL = 3,
};

class Logger : noncopyable
{
public:
    static Logger &instance();
    void setLogLevel(LogLevel level) { logLevel_ = level; }
    LogLevel logLevel() const { return logLevel_; }
    void log(LogLevel level, const char* msg);
private:
    LogLevel logLevel_ = LogLevel::INFO;
};

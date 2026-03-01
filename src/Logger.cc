#include <iostream> // 虽然只有两个函数, 但也不适合放到头文件中, 因为这个iostream有点大, 影响编译速度.

#include "Logger.h"
#include "Timestamp.h"

Logger &Logger::instance() // 又是一个单例
{
    static Logger logger;
    return logger;
}

void Logger::log(LogLevel level, const char* msg)
{
    if (level < logLevel_)
        return;

    const char* pre = nullptr;
    switch (level)
    {
    case LogLevel::INFO:
        pre = "[INFO] ";
        break;
    case LogLevel::ERROR:
        pre = "[ERROR] ";
        break;
    case LogLevel::FATAL:
        pre = "[FATAL] ";
        break;
    case LogLevel::DEBUG:
        pre = "[DEBUG] ";
        break;
    }

    fprintf(stdout, "%s%s : %s\n", pre, Timestamp::now().toString().c_str(), msg);
}

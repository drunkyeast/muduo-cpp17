#pragma once

#include <functional>
#include <mutex>
#include <condition_variable>
#include <string>

#include "noncopyable.h"
#include "Thread.h"

class EventLoop;

class EventLoopThread : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop *)>;

    // C++11 起就可以用 = {} 代替显式构造临时对象，更简洁：
    // EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback(),
    //                 const std::string &name = std::string());
    EventLoopThread(ThreadInitCallback cb = {},
                    const std::string &name = {});
    ~EventLoopThread();

    EventLoop *startLoop();

private:
    void threadFunc();

    EventLoop* loop_;
    Thread thread_;
    std::mutex mutex_;             // 互斥锁
    std::condition_variable cond_; // 条件变量
    ThreadInitCallback callback_;
};
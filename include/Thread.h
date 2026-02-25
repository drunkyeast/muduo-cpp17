#pragma once

#include <functional>
#include <thread>
#include <unistd.h>
#include <string>
#include <atomic>

#include "noncopyable.h"

class Thread : noncopyable
{
public:
    using ThreadFunc = std::function<void()>;

    explicit Thread(ThreadFunc, const std::string& name = {});
    ~Thread();

    void start();
    void join();

    bool started() { return started_; }
    pid_t tid() const { return tid_; }
    const std::string &name() const { return name_; }

    static int numCreated() { return numCreated_; }

private:
    bool started_;
    bool joined_;
    // std::shared_ptr<std::thread> thread_;
    std::thread thread_; // 默认构造为空, 不持有线程, 不用写成智能指针, 且调用Thread的地方也没有用到共享.
    pid_t tid_;       // 在线程创建时再绑定
    ThreadFunc func_; // 线程回调函数
    std::string name_;
    inline static std::atomic_int numCreated_{0};
};
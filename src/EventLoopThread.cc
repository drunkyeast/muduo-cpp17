#include "EventLoopThread.h"
#include "EventLoop.h"
#include "Thread.h"

EventLoopThread::EventLoopThread(ThreadInitCallback cb,
                                 const std::string &name)
    : loop_(nullptr)
    // , thread_(std::bind(&EventLoopThread::threadFunc, this), name) // bind是C++11的遗留物, 不如lambda
    , thread_([this] { threadFunc(); }, name)
    , mutex_() // 不写也行, 它会默认初始化的.
    , cond_() // 不写也行, 它会默认初始化的.
    , callback_(std::move(cb))
{
}

EventLoopThread::~EventLoopThread()
{
    // 主线程(调用这个析构函数), 以及threadFunc末尾, 都会用到loop_, 所以要加锁. 非常细节.
    EventLoop* loop = nullptr; // nullptr是防御性编程
    {
        std::scoped_lock lock(mutex_);
        loop = loop_;
    }
    if (loop)
    {
        loop->quit();
        thread_.join();
    }
}

EventLoop *EventLoopThread::startLoop()
{
    thread_.start(); // 启用底层线程Thread类对象thread_中通过start()创建的线程

    EventLoop *loop = nullptr;
    {
        std::unique_lock lock(mutex_); // 条件变量wait要求unique_lock(需手动unlock), 不能用scoped_lock
        cond_.wait(lock, [this](){return loop_ != nullptr;});
        loop = loop_;
    }
    return loop;
}

// 下面这个方法 是在单独的新线程里运行的
void EventLoopThread::threadFunc()
{
    EventLoop loop; // 创建一个独立的EventLoop对象 和上面的线程是一一对应的, one loop per thread

    if (callback_)
    {
        callback_(&loop); // 我沿着代码看过去, 发现TcpServer根本没有实现这个回调, 没用上
    }

    {
        std::scoped_lock lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }
    loop.loop();    // 执行EventLoop的loop() 开启了底层的Poller的poll()

    std::scoped_lock lock(mutex_);
    loop_ = nullptr;
}
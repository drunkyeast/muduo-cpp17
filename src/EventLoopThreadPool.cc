#include <memory>

#include "EventLoopThreadPool.h"
#include "EventLoopThread.h"

EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg)
    : baseLoop_(baseLoop)
    , name_(nameArg)
    // , started_(false) // 从C++11开始就能类内初始这种简单变量了. 甚至还用不上inline变量
    // , numThreads_(0) 
    // , next_(0)
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{
    // Don't delete loop, it's stack variable // 是的, threadFunc是在栈上创建的EventLoop.
}

// TcpServer构造的时候设置numThreads_, 构造完后再调用TcpServer::start --> 再调用这个start
void EventLoopThreadPool::start(ThreadInitCallback cb)
{
    started_ = true;

    for (int i = 0; i < numThreads_; ++i)
    {
        // 我这里, 相比原来代码优雅太多了.
        auto t = std::make_unique<EventLoopThread>(cb, name_ + std::to_string(i)); // 这里不符合sink argument, 不能move(cb)
        loops_.push_back(t->startLoop()); // 底层创建线程 绑定一个新的EventLoop 并返回该loop的地址
        threads_.push_back(std::move(t)); // 注意, unique_ptr要用move, LSP没有报错, 但编译器会报错.
    }

    if (numThreads_ == 0 && cb) // 关于baseLoop_是main函数中的Loop的多个身份的一个, 在EventLoopThreadPool中叫做baseLoop_, 在acceptor中有, 在main函数中也有... 从TcpServer的构造函数可以看出.
    { // 如果numThreads_ == 0, 那么那个主Reactor线程就要完成从Reactor的事情, 调用一次从Reactor相关的ThreadInitCallback的回调. 搜嘎搜嘎
        cb(baseLoop_); // 没用上啊, testserver没设置
    }
}

// 如果工作在多线程中，baseLoop_(mainLoop)会默认以轮询的方式分配Channel给subLoop
EventLoop *EventLoopThreadPool::getNextLoop()
{
    // 如果只设置一个线程 也就是只有一个mainReactor 无subReactor 
    // 那么轮询只有一个线程 getNextLoop()每次都返回当前的baseLoop_
    EventLoop *loop = baseLoop_;    

    // 通过轮询获取下一个处理事件的loop
    // 如果没设置多线程数量，则不会进去，相当于直接返回baseLoop
    if(!loops_.empty())             
    {
        loop = loops_[next_];
        ++next_;
        // 轮询, 不如模运算
        // if(next_ >= loops_.size())
        // {
        //     next_ = 0;
        // }
        next_ %= loops_.size(); // 可读性也还行吧.
    }

    return loop;
}



std::vector<EventLoop *> EventLoopThreadPool::getAllLoops()
{
    if (loops_.empty())
    {
        // return std::vector<EventLoop *>(1, baseLoop_); // 复杂
        return {baseLoop_};
    }
    else
    {
        return loops_;
    }
}
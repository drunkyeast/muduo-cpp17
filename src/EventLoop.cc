#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <memory>

#include "EventLoop.h"
#include "Logger.h"
#include "Channel.h"
#include "Poller.h"

// 防止一个线程创建多个EventLoop
// __thread就是thread_local, 每个线程独占的变量, 之前是用于线程id
thread_local EventLoop *t_loopInThisThread = nullptr;

/* 补充, eventfd与进程间通信的区别
进程间通信 (IPC)
├─ 管道 (pipe/FIFO)
├─ 消息队列 (message queue)
├─ 共享内存 (shared memory)
├─ 信号 (signal)
└─ Socket

线程间同步
├─ 互斥锁 (mutex)
├─ 条件变量 (condition variable)
├─ 读写锁 (rwlock)
├─ 自旋锁 (spinlock)
└─ 原子操作 (atomic)

两者都可以
├─ ⭐eventfd
├─ ⭐信号量 (semaphore)
└─ 文件锁 (file lock)
*/
// 定义默认的Poller IO复用接口的超时时间
constexpr int kPollTimeMs = 10000; // 10000毫秒 = 10秒钟

/* 创建线程之后主线程和子线程谁先运行是不确定的。
 * 通过一个eventfd在线程之间传递数据的好处是多个线程无需上锁就可以实现同步。
 * eventfd支持的最低内核版本为Linux 2.6.27,在2.6.26及之前的版本也可以使用eventfd，但是flags必须设置为0。
 * 函数原型：
 *     #include <sys/eventfd.h>
 *     int eventfd(unsigned int initval, int flags);
 * 参数说明：
 *      initval,初始化计数器的值。
 *      flags, EFD_NONBLOCK,设置socket为非阻塞。
 *             EFD_CLOEXEC，执行fork的时候，在父进程中的描述符会自动关闭，子进程中的描述符保留。
 * 场景：
 *     eventfd可以用于同一个进程之中的线程之间的通信。
 *     eventfd还可以用于同亲缘关系的进程之间的通信。
 *     eventfd用于不同亲缘关系的进程之间通信的话需要把eventfd放在几个进程共享的共享内存中（没有测试过）。
 */
// 创建wakeupfd 用来notify唤醒subReactor处理新来的channel
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_FATAL("eventfd error:%d\n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)
    , threadId_(CurrentThread::tid()) // good
    , poller_(Poller::newDefaultPoller(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(std::make_unique<Channel>(this, wakeupFd_))
{
    LOG_DEBUG("EventLoop created %p in thread %d\n", this, threadId_);
    if (t_loopInThisThread)
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d\n", t_loopInThisThread, threadId_);
    }
    else
    {
        t_loopInThisThread = this;
    }
    
    // 设置wakeupfd的事件类型以及发生事件后的回调操作, 
    // wakeupChannel_->setReadCallback(
    //     std::bind(&EventLoop::handleRead, this));
    // 用lambda表达式, 逻辑多么清晰
    wakeupChannel_->setReadCallback([this](Timestamp) { 
        handleRead(); 
    });
    
    // 每一个EventLoop都将监听其wakeupChannel_的EPOLL读事件了
    wakeupChannel_->enableReading(); 
}
EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll(); // 给Channel移除所有感兴趣的事件
    wakeupChannel_->remove();     // 把Channel从EventLoop上删除掉
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

// 开启事件循环
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop %p start looping\n", this);

    // 监听两类fd  一种是client的fd,   一种是mainReactor的wakeupfd(在loop初始化时绑定了操作)
    while (!quit_)
    {
        activeChannels_.clear();
        // activeChannels_是一个vector却用指针传入而不是用引用. 是因为, google代码规范曾经规定, 入参constT&, 出参T*
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        for (Channel *channel : activeChannels_)
        {
            // Poller监听哪些channel发生了事件 然后上报给EventLoop 通知channel处理相应的事件
            channel->handleEvent(pollReturnTime_);
        }
        /**
         * 执行当前EventLoop事件循环需要处理的回调操作 对于线程数 >=2 的情况 IO线程 mainloop(mainReactor) 主要工作：
         * accept接收连接 => 将accept返回的connfd打包为Channel => TcpServer::newConnection通过轮询将TcpConnection对象分配给subloop处理
         *
         * mainloop调用queueInLoop将回调加入subloop（该回调需要subloop执行 但subloop还在poller_->poll处阻塞） queueInLoop通过wakeup将subloop唤醒
         **/
        doPendingFunctors();
    }
    LOG_INFO("EventLoop %p stop looping.\n", this);
    looping_ = false;
}

void EventLoop::handleRead()
{
    uint64_t one = 1;
    ssize_t n = read(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one))
    {
        LOG_ERROR("EventLoop::handleRead() reads %lu bytes instead of 8\n", n);
    }
}


void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;
    {
        // C++17 scoped_lock: 替代 lock_guard, 单mutex时性能相同(编译器会退化为lock_guard等价实现),
        // 优势在于支持同时锁多个mutex防死锁. CTAD省去<std::mutex>模板参数.
        // 注: unique_lock 有额外开销(存储是否持有锁的bool + 支持手动unlock), 仅在需要条件变量wait或手动unlock时使用.
        std::scoped_lock lock(mutex_);
        functors.swap(pendingFunctors_); // 交换的方式减少了锁的临界区范围 提升效率 同时避免了死锁 如果执行functor()在临界区内 且functor()中调用queueInLoop()就会产生死锁
    }

    for (const Functor &functor : functors)
    {
        functor(); // 执行当前loop需要执行的回调操作
    }

    callingPendingFunctors_ = false;
}

 // 施磊注释: 1. loop在自己的线程中调用quit   2.在非loop的线程中, 调用loop的quit
 //                 mainLoop
 //
 // subLoop1        subLoop2        subLoop3
void EventLoop::quit()
{
    quit_ = true; // 每个loop不是有一个while(!quit_)的循环嘛

    if (!isInLoopThread()) // 如果是在另一个线程调用loop的quit, 就需要wakeup.
    {
        wakeup();
    }
}

// 用来唤醒loop所在线程 向wakeupFd_写一个数据 wakeupChannel就发生读事件 当前loop线程就会被唤醒
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one))
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8\n", n);
    }
}


// 在当前loop中执行cb, 这注释写的有点垃圾. 什么叫当前loop, 当前线程, 还得靠自己去梳理.
// mainloop/~TcpServer中执行: conn->getLoop()->runInLoop(...), conn是一个TcpConnection(对应一个socket/channel)
void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread()) // 当前EventLoop中执行回调
    {
        cb();
    }
    else // 在非当前EventLoop线程中执行cb，就需要唤醒EventLoop所在线程执行cb
    {
        queueInLoop(std::move(cb)); // move后, queueInLoop还是会拷贝, 但移动构造开销远小于拷贝构造
    }
}

// 把cb放入队列中 唤醒loop所在的线程执行cb
void EventLoop::queueInLoop(Functor cb)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(std::move(cb)); // 这个pendingFunctors也是当前loop的成员变量.
    }

    // isInLoopThread 逻辑好理解, callingPendingFunctors_不好理解, 但我花了半小时想通了.
    // 施磊: || callingPendingFunctors_是: 当前loop正在执行回调, 但loop又有了新的回调. 
    // 但此时还在执行doPendingFunctors, 遍历pendingFunctors_(经过了swap), pendingFunctors_又增加了新的cb(mainReactor又给subReactor发消息了)
    // 当doPendingFunctors执行完后, 到下一个循环, 就会阻塞到epoll_wait中, 但因为有wake, 所以epoll_wait不会阻塞.
    // 我这讲得多好啊, 顺着代码执行逻辑在讲, 他妈的, 卡码笔记就是一坨, 它还魔改施磊的话.
    if (!isInLoopThread() || callingPendingFunctors_)
    {
        wakeup(); // 唤醒loop所在线程
    }
}

// EventLoop的方法 => Poller的方法
void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}



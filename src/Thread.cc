#include "Thread.h"
#include "CurrentThread.h"

#include <semaphore.h>

Thread::Thread(ThreadFunc func, const std::string& name)
    : started_(false)
    , joined_(false)
    , tid_(0)
    , func_(std::move(func))
    , name_(name)
{
    int num = ++ numCreated_; // 线程数量 +1
    if (name_.empty()) name_ = "Thread" + std::to_string(num); // 为空就设置默认名字.
}

Thread::~Thread()
{
    if (started_ && !joined_)
    {
        thread_.detach();                                                  // thread类提供了设置分离线程的方法 线程运行后自动销毁（非阻塞）
    }
}

void Thread::start()                                                        // 一个Thread对象 记录的就是一个新线程的详细信息
{
    started_ = true;
    sem_t sem;
    sem_init(&sem, false, 0);                                               // false指的是 不设置进程间共享
    
    // 开启线程, 旧写法用的shared_ptr, 用new或make_shared.
    thread_ = std::thread([&]() {   // 移动赋值，不需要 new 或 make_shared
        tid_ = CurrentThread::tid();
        sem_post(&sem);
        func_();
    });

    // 这里必须等待获取上面新创建的线程的tid值
    sem_wait(&sem);
}

// C++ std::thread 中join()和detach()的区别：https://blog.nowcoder.net/n/8fcd9bb6e2e94d9596cf0a45c8e5858a
void Thread::join()
{
    joined_ = true;
    thread_.join();
}
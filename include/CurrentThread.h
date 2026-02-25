#pragma once

#include <unistd.h>
#include <sys/syscall.h>

namespace CurrentThread
{
    // __thread 完全等价于 thread_local, 每个线程拥有该变量的独立副本，互不干扰!!!
    inline thread_local int t_cachedTid = 0; // 保存tid缓存 因为系统调用非常耗时 拿到tid后将其保存

    inline void cacheTid()
    {
        if (t_cachedTid == 0)
        {
            // 通过linux系统调用, 获取当前线程的tid值. 虽然<thread>中有std::this_thread::get_id(), 但没这个快啊.
            t_cachedTid = static_cast<pid_t>(::syscall(SYS_gettid));
        }
    }

    inline int tid()
    {
        if (__builtin_expect(t_cachedTid == 0, 0)) // 因为tid 只在第一次调用时为 0，之后一直是缓存值. 所以告诉 CPU 分支预测器不要跳入 if 分支, 减少 CPU 流水线预测错误的开销。
        {
            cacheTid();
        }
        return t_cachedTid;
    }
}
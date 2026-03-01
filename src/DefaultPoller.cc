#include <cstdlib>
#include <memory>

#include "Poller.h"
#include "EPollPoller.h"

std::unique_ptr<Poller> Poller::newDefaultPoller(EventLoop *loop)
{
    if (::getenv("MUDUO_USE_POLL"))
    {
        return nullptr; // 生成poll的实例
    }
    else
    {
        return std::make_unique<EPollPoller>(loop);
    }
}
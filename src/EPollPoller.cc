#include <cerrno>
#include <unistd.h>

#include "EPollPoller.h"
#include "Logger.h"
#include "Channel.h"

// 某个channel的状态
namespace  { // 匿名 namespace 赋予内部链接
    constexpr int kNew = -1;    // 某个channel还没添加至Poller          // channel的成员index_初始化为-1
    constexpr int kAdded = 1;   // 某个channel已经添加至Poller
    constexpr int kDeleted = 2; // 某个channel已经从Poller删除
}


EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop)
    , epollfd_(::epoll_create1(EPOLL_CLOEXEC)) // 使用::前缀, 明确调用全局命名空间的系统函数，避免命名冲突。
    // EPOLL_CLOEXEC: 当进程执行exec系列函数时，自动关闭该fd，防止fd泄漏到子进程。
    // epoll_create(int size) - 旧版本，size参数已无意义（内核2.6.8后被忽略）
    // epoll_create1(int flags) - 新版本，可以设置标志位
    , events_(kInitEventListSize) // vector<epoll_event>(16)
{
    if (epollfd_ < 0)
    {
        LOG_FATAL("epoll_create error:%d \n", errno);
    }
}

EPollPoller::~EPollPoller()
{
    ::close(epollfd_);
}

// channel update remove => EventLoop updateChannel removeChannel => Poller updateChannel removeChannel
void EPollPoller::updateChannel(Channel *channel) // 这是修改Channel感兴趣的事情. Channel会修改events_成员变量, 就是它感兴趣的事情.
{
    const int index = channel->index();
    LOG_DEBUG("func=%s => fd=%d events=%d index=%d\n", __FUNCTION__, channel->fd(), channel->events(), index);

    if (index == kNew || index == kDeleted) // 未被注册或者已经被Poller删除.
    {
        if (index == kNew) // 未被注册.
        {
            int fd = channel->fd();
            channels_[fd] = channel; // 注册到Poller中. 通过哈希表管理
        }
        else // index == kDeleted. muduo源码中有些assert, 删掉了.
        {
        }
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }
    else // channel已经在Poller中注册过了
    {
        if (channel->isNoneEvent()) // 当前channel对任何事情都不感兴趣
        {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        }
        else
        {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

// 从Poller中删除channel
void EPollPoller::removeChannel(Channel *channel)
{
    int fd = channel->fd();
    channels_.erase(fd);

    LOG_DEBUG("func=%s => fd=%d\n", __FUNCTION__, fd);

    int index = channel->index();
    if (index == kAdded)
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew);
}


Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    // 由于频繁调用poll 实际上应该用LOG_DEBUG输出日志更为合理 当遇到并发场景 关闭DEBUG日志提升效率
    LOG_DEBUG("func=%s => fd total count:%lu\n", __FUNCTION__, channels_.size());

    // 这里用&*events_.begin(), 不错. events_是一个vector.   但也可以用events_.data().
    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    int saveErrno = errno;
    Timestamp now(Timestamp::now());

    if (numEvents > 0)
    {
        LOG_DEBUG("%d events happend\n", numEvents);
        fillActiveChannels(numEvents, activeChannels); // 每个事件 = 某个fd + 事件类型, 然后封装成Channel, 然后存到activeChannels中.
        if (numEvents == events_.size()) // 扩容操作, 因为有maxevents上限.
        {
            events_.resize(events_.size() * 2); // 手动扩容
        }
    }
    else if (numEvents == 0)
    {
        LOG_DEBUG("%s timeout!\n", __FUNCTION__);
    }
    else
    {
        if (saveErrno != EINTR)
        {
            errno = saveErrno;
            LOG_ERROR("EPollPoller::poll() error!");
        }
    }
    return now;
}

// 填写活跃的连接
// EventLoop调用poll, poll调用fillActiveChannels, activeChannels是一个vector, 感觉就是epoll_wait返回的东西, 就是EventList, 转化成channel.
// EventLoop调用poll, 施磊原话: EventLoop就拿到了它的Poller给它返回的所有发生事件的channel列表了.
void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i = 0; i < numEvents; ++i)
    {
        Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
        /* 这个events_[i]就是epoll_event类型, 长这个样子, 
        TCPIP网络编程书中, if(ep_events[i].data.fd == serv_sock)这样来判断是哪个套接字有事件发生.
        明白了, 以前是直接存fd, 现在是存Channel*指针, Channel封装了fd + 事件 + 回调函数.

        struct epoll_event {
            uint32_t events;      // 发生的事件类型 (EPOLLIN/EPOLLOUT等)
            epoll_data_t data;    // 用户数据(联合体)
        };

        typedef union epoll_data {
            void *ptr;    // 可以存指针, 转化成Channel*指针, Channel就是对fd的封装.
            int fd;       // 可以存fd
            uint32_t u32;
            uint64_t u64;
        } epoll_data_t;
        */

        channel->set_revents(events_[i].events);
        // 把epoll返回的实际发生的事件存到Channel中。
        // 之前我理解的事: 套接字有事件发送, 然后去判断是监听套接字还是连接套接字有事件发生, 然后处理, 没有涉及对事件的分类啊. 具体见Channel
        // Channel的成员变量含义如下:
        // int events_;      // 注册fd感兴趣的事件. 
        // int revents_;     // Poller返回的具体发生的事件

        activeChannels->push_back(channel); // EventLoop就拿到了它的Poller给它返回的所有发生事件的channel列表了
    }
}

// 更新channel通道 其实就是调用epoll_ctl add/mod/del
void EPollPoller::update(int operation, Channel *channel) // 这是Channel中
{
    epoll_event event{}; // 值初始化, 等价于memset零初始化, 更C++
    event.events = channel->events(); // 感兴趣的事件, 比特位表示
    event.data.fd = channel->fd();
    event.data.ptr = channel; // 这里是联合体, fd会被channel指针覆盖掉.

    if (::epoll_ctl(epollfd_, operation, channel->fd(), &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR("epoll_ctl del error:%d\n", errno);
        }
        else
        {
            LOG_FATAL("epoll_ctl add/mod error:%d\n", errno);
        }
    }
}
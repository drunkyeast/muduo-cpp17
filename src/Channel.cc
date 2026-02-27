// #include <sys/epoll.h>

#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"

// EventLoop: ChannelList Poller
Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop)
    , fd_(fd)
    , events_(0)
    , revents_(0)
    , index_(-1)
    , tied_(false)
{
}

Channel::~Channel()
{
    // 原本muduo作者在这是写了一些asset断言的, 用于debug. 直接删掉. realease版本会直接抹除assert.
}

// 卡码笔记的注释不靠谱, 傻逼一个.
// 何时调用? TcpServer中newConnection是给Acceptor的回调, 有新链接建立=> newConnection回调执行=> 创建TcpConnection对象conn=> conn->connectEstablished => 里面执行channel_->tie(shared_from_this()); 且执行onConnection回调(testserver传递到Channel的)
// 我还洞察到了一点: 用户设置的onConnection传递到TcpConnection就ok了, 没有进一步传递到Channel, 而OnMessage则是进一步传递到了Channel. 这也合理.

void Channel::tie(const std::shared_ptr<void> &obj)
{
    tie_ = obj; // 这个weak_ptr是Channel的成员变量
    tied_ = true;
}
//update 和remove => EpollPoller 更新channel在poller中的状态
/**
 * 当改变channel所表示的fd的events事件后，update负责再poller里面更改fd相应的事件epoll_ctl
 **/
void Channel::update()
{
    // 通过channel所属的eventloop，调用poller的相应方法，注册fd的events事件
    loop_->updateChannel(this);
}

// 在channel所属的EventLoop中把当前的channel删除掉
void Channel::remove()
{
    loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime)
{
    if (tied_)
    {
        std::shared_ptr<void> guard = tie_.lock();
        if (guard)
        {
            handleEventWithGuard(receiveTime);
        }
        // 如果提升失败了 就不做任何处理 说明Channel的TcpConnection对象已经不存在了
    }
    else
    {
        handleEventWithGuard(receiveTime);
    }
}

// 根据poller通知的channel发生的具体事件, 由channel负责调用具体的回调操作  调试还可以用__FILE__, __LINE__, __func__
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    LOG_INFO("[%s:%d]%s\n", __FILE__, __LINE__, __func__);
    LOG_INFO("channel handleEvent revents:%d\n", revents_);
    // 关闭
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) // 当发生挂起事件（如对端RST或连接意外断开）,客户端主动断开连接不会触发这个. 最后触发handleClose.
    {
        if (closeCallback_)
        {
            closeCallback_();
        }
    }
    // 错误
    if (revents_ & EPOLLERR)
    {
        if (errorCallback_)
        {
            errorCallback_();
        }
    }
    // 读
    if (revents_ & (EPOLLIN | EPOLLPRI)) // 客户端主动断开连接也是触发这个, readCallback_里面的handleRead里面读取到 0 byte, 表示客户端断开. 最后触发handleClose. 与上面殊途同归.
    {
        if (readCallback_)
        {
            readCallback_(receiveTime);
        }
    }
    // 写
    if (revents_ & EPOLLOUT)
    {
        if (writeCallback_)
        {
            writeCallback_();
        }
    }
}
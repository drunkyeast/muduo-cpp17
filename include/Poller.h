#pragma once

#include <vector>
#include <unordered_map>
#include <memory>

#include "noncopyable.h"
#include "Timestamp.h"
#include "Channel.h"
class EventLoop;

// muduo库中多路事件分发器demultiplex的核心IO复用模块
class Poller : noncopyable
{
public:
    using ChannelList = std::vector<Channel *>;

    Poller(EventLoop *loop): ownerLoop_(loop) {}
    virtual ~Poller() = default;

    // 给所有IO复用保留统一的接口
    virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels) = 0;
    virtual void updateChannel(Channel *channel) = 0;
    virtual void removeChannel(Channel *channel) = 0;

    // 判断参数channel是否在当前的Poller当中
    bool hasChannel(Channel *channel) const {
        auto it = channels_.find(channel->fd());
        return it != channels_.end() && it->second == channel;
    }

    // EventLoop可以通过该接口获取默认的IO复用的具体实现
    static std::unique_ptr<Poller> newDefaultPoller(EventLoop *loop);

protected:
    // map的key:sockfd value:sockfd所属的channel通道类型
    using ChannelMap = std::unordered_map<int, Channel *>;
    ChannelMap channels_;

private:
    EventLoop *ownerLoop_; // 定义Poller所属的事件循环EventLoop
};
#pragma once

#include <sys/epoll.h>
#include <functional>
#include <memory>

#include "noncopyable.h"
#include "Timestamp.h"

class EventLoop; // 前置声明, 这儿没必要包含它的头文件.

/**
 * 理清楚 EventLoop、Channel、Poller之间的关系  他们在Reactor模型上对应多路事件分发器(Demultiplex)
 * Channel理解为通道 封装了sockfd和其感兴趣的event 如EPOLLIN、EPOLLOUT事件 还绑定了poller返回的具体事件
 **/
class Channel : noncopyable
{
public:
    using EventCallback = std::function<void()>; // muduo仍使用typedef
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop *loop, int fd);
    ~Channel();

    // fd得到Poller通知以后 处理事件 handleEvent在EventLoop::loop()中调用
    void handleEvent(Timestamp receiveTime);

    // 设置回调函数对象
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 防止当channel被手动remove掉 channel还在执行回调操作
    void tie(const std::shared_ptr<void> &);

    int fd() const { return fd_; }
    int events() const { return events_; }
    void set_revents(int revt) { revents_ = revt; } // poller调用这个来设置啊

    // 设置fd相应的事件状态 相当于epoll_ctl add delete
    void enableReading() { events_ |= kReadEvent; update(); } // 也是poller修改, 相当于调用epoll_ctl
    void disableReading() { events_ &= ~kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_ &= ~kWriteEvent; update(); }
    void disableAll() { events_ = kNoneEvent; update(); }

    // 返回fd当前的事件状态, isWriting和isReading的位运算就不去纠结了, 不好理解.
    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isWriting() const { return events_ & kWriteEvent; }
    bool isReading() const { return events_ & kReadEvent; }

    int index() const { return index_; }
    void set_index(int idx) { index_ = idx; }

    // one loop per thread
    EventLoop *ownerLoop() { return loop_; }
    void remove(); // 删除channel
private:

    void update();
    void handleEventWithGuard(Timestamp receiveTime); // 就是handleEvent的一部分, 底层

    // static const int x = 0;早在C++98就可以. 但这个特例只针对 int、char 这种整型. 
    // 我改成inline更通用, 1. int换成string也可以  2. const去掉也可以. 更通用. 
    // constexpr能自带inline, 但不能作用于string. 所以我下面这写法是更通用的.
    // inline static const std::string t1 = "hello"; // 这是一个例子
    inline static const int kNoneEvent = 0; //空事件
    inline static const int kReadEvent = EPOLLIN | EPOLLPRI; //读事件 // EPOLLIN是: 只有数据到达时才触发 // 0x00000011
    inline static const int kWriteEvent  = EPOLLOUT; //写事件 // EPOLLOUT是: 只要发送缓冲区有空间就触发!!! // 0x00000100

    EventLoop *loop_; // 事件循环
    const int fd_;    // fd，Poller监听的对象
    int events_;      // 注册fd感兴趣的事件
    int revents_;     // Poller返回的具体发生的事件
    int index_;       // used by poller

    std::weak_ptr<void> tie_; // 与TcpConnection绑定, TcpConnection和channel的是否要跨线程, 线程不安全. 过程中channel因为epoll_wait还可能去处理消息, 要判断TcpConnection是否释放, 释放了就不能去做事了.
    bool tied_;

    // 因为channel通道里可获知fd最终发生的具体的事件revents，所以它负责调用具体事件的回调操作.
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};
#pragma once

#include <memory>
#include <string>
#include <atomic>
#include <string_view>

#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Buffer.h"
#include "Timestamp.h"
#include "EventLoop.h"

class Channel;
// class EventLoop; // 写了模板函数, 不能前置申明, 而是要include了.
class Socket;

/**
 * TcpServer => Acceptor => 有一个新用户连接，通过accept函数拿到connfd
 * => TcpConnection设置回调 => 设置到Channel => Poller => Channel回调
 **/

class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    TcpConnection(EventLoop *loop,
                  const std::string &nameArg,
                  int sockfd,
                  const InetAddress &localAddr,
                  const InetAddress &peerAddr);
    ~TcpConnection();

    EventLoop *getLoop() const { return loop_; }
    const std::string &name() const { return name_; }
    const InetAddress &localAddress() const { return localAddr_; }
    const InetAddress &peerAddress() const { return peerAddr_; }

    bool connected() const { return state_ == kConnected; }

    void setTcpNoDelay(bool on);

    // 发送数据
    // 故事线如下: 
    // 1. 一开始只写了const string&, 这样不能移动啊, 
    // 2. 改成string版本+move, 但字符串字面量不是右值会触发拷贝, 且传左值时要手动move.
    // 3. 改成string_view(这样可以接受字符串字面量, 避免拷贝), 但不能移动了, 所以还需要string&&. 但是字符串字面量, 会有二义性
    // 4. 为了解决二义性, 再写一个const char* 接口拦截字符串字面量, 就没有二义性了.
    // 5. 但是const char*, string_view, string&& 一共有3个接口, 好烦, 所以用了万能引用+完美转发.🥰🥰
    // 完美转发模板，处理所有字符串类型（左值、右值、字面量）,把send(const char*), send(string&&), send(string), send(string_view)全部统一了起来.
    template <typename StringLike>
    void send(StringLike&& message)
    {
        if (state_ == kConnected)
        {
            if (loop_->isInLoopThread())
            {
                // 【情况 A：当前 IO 线程】
                // 无论是 string 左值、右值 还是 const char*，
                // 都能极其轻量地隐式构造为 string_view（仅仅赋值一个指针和长度）。
                // 绝对的 0 拷贝！
                std::string_view sv(message); 
                sendInLoop(sv.data(), sv.size());
            }
            else
            {
                // 【情况 B：跨线程投递】
                // 重点来了！这行代码是性能分水岭：
                // 1. 如果 message 是右值 string (std::move传进来的)，这里触发 Move 构造，0 拷贝！
                // 2. 如果 message 是左值 string 或 const char*，这里触发 Copy 构造/分配。这是跨线程保证内存安全的必须代价。
                loop_->runInLoop([self = shared_from_this(),
                                    msg = std::string(std::forward<StringLike>(message))]() {
                    self->sendInLoop(msg.data(), msg.size());
                });
            }
        }
    }
    // 再后来补充了这两个接口, 第一个转化成string_view复用就好了.🥰🥰
    void send(const void* data, size_t len);
    // 第二个, 增加Buffer的swap逻辑, 因为Buffer底层是vector<char>, 最后可以用空Buffer来swap窃取资源. 这是真的极致优化了. 🥰🥰
    void send(Buffer* buf);
    void sendFile(int fileDescriptor, off_t offset, size_t count); 
    
    // 关闭半连接
    void shutdown();

    // 这一坨是上层TcpServer传递给TcpConnection的.
    void setConnectionCallback(ConnectionCallback cb)
    { connectionCallback_ = std::move(cb); }
    void setMessageCallback(MessageCallback cb)
    { messageCallback_ = std::move(cb); }
    void setWriteCompleteCallback(WriteCompleteCallback cb)
    { writeCompleteCallback_ = std::move(cb); }
    void setCloseCallback(CloseCallback cb)
    { closeCallback_ = std::move(cb); }
    void setHighWaterMarkCallback(HighWaterMarkCallback cb, size_t highWaterMark)
    { highWaterMarkCallback_ = std::move(cb); highWaterMark_ = highWaterMark; }

    // 连接建立
    void connectEstablished();
    // 连接销毁
    void connectDestroyed();

private:
    enum StateE
    {
        kDisconnected, // 已经断开连接
        kConnecting,   // 正在连接
        kConnected,    // 已连接
        kDisconnecting // 正在断开连接
    };
    void setState(StateE state) { state_ = state; }

    // 这些就是给channel用的回调. TcpConnection把回调传递给Channel的, 具体绑定在构造函数中使用.
    void handleRead(Timestamp receiveTime);
    void handleWrite();//处理写事件
    void handleClose();
    void handleError();

    void sendInLoop(const void *data, size_t len);
    void shutdownInLoop();
    void sendFileInLoop(int fileDescriptor, off_t offset, size_t count);
    EventLoop *loop_; // 这里是baseloop还是subloop由TcpServer中创建的线程数决定 若为多Reactor 该loop_指向subloop 若为单Reactor 该loop_指向baseloop
    const std::string name_;
    std::atomic_int state_;
    bool reading_;//连接是否在监听读事件

    // Socket Channel 这里和Acceptor类似    Acceptor => mainloop    TcpConnection => subloop
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress localAddr_;
    const InetAddress peerAddr_;

    // 这些回调TcpServer也有 用户通过写入TcpServer注册 TcpServer再将注册的回调传递给TcpConnection TcpConnection再将回调注册到Channel中
    ConnectionCallback connectionCallback_;       // 有新连接时的回调, main中设置的回调扔给TcpServer,再扔给TcpConnection然后扔给Channel(监听套接字的)
    MessageCallback messageCallback_;             // 有读写消息时的回调, main中设置的回调扔给TcpServer,再扔给TcpConnection然后扔给Channel(非监听套接字的)
    WriteCompleteCallback writeCompleteCallback_; // 消息发送完成以后的回调
    HighWaterMarkCallback highWaterMarkCallback_; // 高水位回调
    CloseCallback closeCallback_; // 关闭连接的回调
    size_t highWaterMark_; // 高水位阈值

    // 数据缓冲区
    Buffer inputBuffer_;    // 接收数据的缓冲区
    Buffer outputBuffer_;   // 发送数据的缓冲区 用户send向outputBuffer_发
};

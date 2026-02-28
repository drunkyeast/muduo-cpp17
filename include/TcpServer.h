#pragma once

/**
 * 用户使用muduo编写服务器程序
 **/

#include <functional>
#include <string>
#include <memory>
#include <atomic>
#include <unordered_map>

#include "EventLoop.h"
#include "Acceptor.h"
#include "InetAddress.h"
#include "noncopyable.h"
#include "EventLoopThreadPool.h"
#include "Callbacks.h"
#include "TcpConnection.h"
#include "Buffer.h"

// 对外的服务器编程使用的类
class TcpServer : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop *)>;

    enum Option
    {
        kNoReusePort,//不允许重用本地端口
        kReusePort,//允许重用本地端口
    };

    TcpServer(EventLoop *loop,
              const InetAddress &listenAddr,
              const std::string &nameArg,
              Option option = kNoReusePort);
    ~TcpServer();

    void setThreadInitCallback(const ThreadInitCallback &cb) { threadInitCallback_ = cb; } // 没用到
    void setConnectionCallback(const ConnectionCallback &cb) { connectionCallback_ = cb; } // example的main中只用到了这几个, 连接建立和断开都是这个.
    void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; } // example的main中只用到了这几个
    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { writeCompleteCallback_ = cb; } // 没用到, 意义在于传输1GB这样的大文件

    // 设置底层subloop的个数
    void setThreadNum(int numThreads); // example的main中只用到了这几个
    /**
     * 如果没有监听, 就启动服务器(监听).
     * 多次调用没有副作用.
     * 线程安全.
     */
    void start();

private:
    // 陈硕: Not thread safe, but in loop  这就是one loop per thread设计哲学: 把并发问题转化成单线程问题. 这函数只会在mainloop对应的线程中执行.
    void newConnection(int sockfd, const InetAddress &peerAddr); // 这个绝对的核心!!, 后面两个和前面两个remove回调也在这里面. 以及后面的 connectionCallback_等等也在这里面.
    // 陈硕: Thread safe. 它利用runInLoop把remove操作切回了mainLoop执行下面那个, 这里面涉及EventLoop的实现细节, 略.
    void removeConnection(const TcpConnectionPtr &conn);
    // 陈硕: Not thread safe, but in loop
    void removeConnectionInLoop(const TcpConnectionPtr &conn);

    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

    EventLoop *loop_; // baseloop 用户自定义的loop

    const std::string ipPort_;
    const std::string name_;

    std::unique_ptr<Acceptor> acceptor_; // 运行在mainloop 任务就是监听新连接事件

    std::shared_ptr<EventLoopThreadPool> threadPool_; // one loop per thread

    ConnectionCallback connectionCallback_;       // 有新连接时的回调, TcpServer扔给TcpConnection然后扔给Channel
    MessageCallback messageCallback_;             // 有读写事件发生时的回调
    WriteCompleteCallback writeCompleteCallback_; // 消息发送完成后的回调, testserver中没有使用.

    ThreadInitCallback threadInitCallback_; // loop线程初始化的回调
    int numThreads_;//线程池中线程的数量。
    // std::atomic_int started_; // 用atomic<int>更C++morden, 然后用bool语义更明确.
    std::atomic<bool> started_;
    int nextConnId_;
    ConnectionMap connections_; // 保存所有的连接
};
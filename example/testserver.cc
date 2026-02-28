#include <string>

#include "TcpServer.h"
#include "Logger.h"

class EchoServer
{
public:
    EchoServer(EventLoop *loop, const InetAddress &addr, const std::string &name)
        : server_(loop, addr, name)
        , loop_(loop)
    {
        // 注册回调函数
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        
        server_.setMessageCallback(
            std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        // 设置合适的subloop线程数量, 设置成0或1, 方便观察终端输出. 
        server_.setThreadNum(0);
    }
    void start() // 在 Muduo 的设计哲学（以及现代 C++ 设计）中，通常推荐优先使用组合而不是继承。TcpServer是private成员函数, 而不是用作基类.
    {
        server_.start();
    }

private:
    // 连接建立或断开的回调函数
    void onConnection(const TcpConnectionPtr &conn)   
    {
        if (conn->connected())
        {
            LOG_INFO("Connection UP : %s", conn->peerAddress().toIpPort().c_str());
        }
        else
        {
            LOG_INFO("Connection DOWN : %s", conn->peerAddress().toIpPort().c_str());
        }
    }

    // 可读写事件回调, EchoServer的Echo逻辑就体现在这里.
    void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time)
    {
        std::string msg = buf->retrieveAllAsString();
        conn->send(std::move(msg));
        conn->send("hello");
        // conn->shutdown();  // send一次就shutdown吗? 这是短连接.
        // shutdown() 只是发送了 FIN 包，它本身不会立即导致本地产生 EPOLLHUP 事件。底层是EPOLLIN，最后read返回0，从而执行handleRead->handleClose->closeCallback_。
    }
    TcpServer server_;
    EventLoop *loop_;

};

int main() {
    EventLoop loop; //这个EventLoop就是main EventLoop，即负责循环事件监听处理新用户连接事件的事件循环器。
    InetAddress addr(8080); //InetAddress其实是对socket编程中的sockaddr_in进行封装，使其变为更友好简单的接口而已。
    EchoServer server(&loop, addr, "EchoServer"); // 创建TcpServer, Acceptor(未listen), 线程池(未启动).
    server.start(); // 线程池的线程开~始~启~动~且subLoop.loop()  Acceptor开始listen   特殊情况:如果setThreadNum=0, 那么就不会启动, 轮询subLoop时就用baseLooop即mainLoop
    loop.loop(); // 主循环开始epoll_wait  有新的连接(TcpServer::newConnection非常核心, 连接的创建与关闭), 就轮询给subReactor/subLoop. 以上逻辑还是在主Reactor里面, 之后就交给subReactor去干了.
    return 0;
}
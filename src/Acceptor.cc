#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

#include "Acceptor.h"
#include "Logger.h"
#include "InetAddress.h"

static int createNonblocking()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0)
    {
        LOG_FATAL("%s:%s:%d listen socket create err:%d\n", __FILE__, __FUNCTION__, __LINE__, errno);
    }
    return sockfd;
}

Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : loop_(loop)
    , acceptSocket_(createNonblocking()) // 注意: 这里只是创建监听套接字, 设置了非阻塞等等, Socket::accept调用的accept4也设置非阻塞是针对新来的连接套接字的.
    , acceptChannel_(loop, acceptSocket_.fd()) // 也要加到loop的poller上面, 这个主Reactor, 主线程, 主loop的poller只监听 监听套接字? 是的, 只盯着这一个. // 施磊也补充: 为什么channel要传入loop? 答: channel要通过loop去调用poller来管理channel
    , listenning_(false)
{
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(true);
    acceptSocket_.bindAddress(listenAddr); // socket -> bind -> listen(下面有), 具体是TcpServer构造函数中创建Acceptor, 完成socket->bind. 然后TcpServer.start里面用mainloop来执行listen.
    
    // 下面这块逻辑都是以前写Channel/Poller/EventLoop里面的. 这里只是设置了handleRead这个回调.
    // TcpServer::start() => Acceptor.listen() 如果有新用户连接 要执行一个回调(accept => connfd => 打包成Channel => 唤醒subloop)  这个逻辑梳理得很好.
    // baseloop监听到有事件发生 => acceptChannel_(即listenfd)被放到了activeChannels_中遍历执行handleEvent => 具体而言是在handleEventWithGuard中执行该handleRead回调函数
    acceptChannel_.setReadCallback([this](Timestamp) { // std::bind的可读性真的垃圾.
        handleRead();
    });
}

Acceptor::~Acceptor()
{
    acceptChannel_.disableAll();    // 把从Poller中感兴趣的事件删除掉
    acceptChannel_.remove();        // 调用EventLoop->removeChannel => Poller->removeChannel 把Poller的ChannelMap对应的部分删除
}

void Acceptor::listen()
{
    listenning_ = true;
    acceptSocket_.listen();         // listen
    acceptChannel_.enableReading(); // acceptChannel_注册至Poller !重要
}

// listenfd有事件发生了，就是有新用户连接了
// 此时连接套接字已经在内核中创建好了, accept只是从已连接的队列中捞一个出来.
void Acceptor::handleRead()
{
    InetAddress peerAddr;
    int connfd = acceptSocket_.accept(&peerAddr);
    if (connfd >= 0)
    {
        if (NewConnectionCallback_) // 这个回调在TcpServer中, 这个很关键, 这个就是轮询分发给subReactor, 并不是main函数中设置的setConnectionCallback, 后者是在前者里面, 妙蛙, 梳理通了.
        {
            NewConnectionCallback_(connfd, peerAddr); // 轮询找到subLoop 唤醒并分发当前的新客户端的Channel
        }
        else
        {
            ::close(connfd);
        }
    }
    else
    {
        LOG_ERROR("%s:%s:%d accept err:%d\n", __FILE__, __FUNCTION__, __LINE__, errno);
        if (errno == EMFILE) // 文件描述符fd耗尽, 与inode耗尽(磁盘相关)完全不是一个概念
        { // 这里还有个优化点, 略, 不重要啊.
            LOG_ERROR("%s:%s:%d sockfd reached limit\n", __FILE__, __FUNCTION__, __LINE__);
        }
    }
}
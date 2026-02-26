#include <functional>
#include <string.h>

#include "TcpServer.h"
#include "Logger.h"
#include "TcpConnection.h"

static EventLoop *CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainLoop is null!\n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpServer::TcpServer(EventLoop *loop,
                     const InetAddress &listenAddr,
                     const std::string &nameArg,
                     Option option)
    : loop_(CheckLoopNotNull(loop))
    , ipPort_(listenAddr.toIpPort())
    , name_(nameArg)
    // , acceptor_(new Acceptor(loop, listenAddr, option == kReusePort))
    // , threadPool_(new EventLoopThreadPool(loop, name_))
    , acceptor_(std::make_unique<Acceptor>(loop, listenAddr, option == kReusePort)) // make_unique 和 make_shared 返回的都是右值, 不用move
    , threadPool_(std::make_shared<EventLoopThreadPool>(loop, name_))
    , connectionCallback_()
    , messageCallback_()
    , nextConnId_(1)
    , started_(false)
{
    // 当有新用户连接时，Acceptor类中绑定的acceptChannel_会有读事件发生，执行handleRead()调用TcpServer::newConnection回调
    // acceptor_->setNewConnectionCallback(
    //     std::bind(&TcpServer::newConnection, this, std::placeholders::_1, std::placeholders::_2));
    acceptor_->setNewConnectionCallback([this](int sockfd, const InetAddress &peerAddr) {
        this->newConnection(sockfd, peerAddr);
    });
}

TcpServer::~TcpServer()
{
    for(auto &item : connections_)
    {
        // TcpConnectionPtr conn(item.second);
        // item.second.reset();    // 把原始的智能指针复位 让栈空间的TcpConnectionPtr conn指向该对象 当conn出了其作用域 即可释放智能指针指向的对象
        TcpConnectionPtr conn(std::move(item.second)); // 这样就不需要手动reset了.

        // 销毁连接
        // conn->getLoop()->runInLoop(
        //     std::bind(&TcpConnection::connectDestroyed, conn)); // 这个bind会拷贝conn参数, 导致引用计数+1.
        conn->getLoop()->runInLoop([conn](){
            conn->connectDestroyed();
        });

        // 要想彻底删除一个TcpConnection对象，就必须要调用这个对象的connecDestroyed()方法
        // 因为引用计数归0会触发~TcpConnection, 这个析构就无所谓再主线程还是工作线程了.

        // bind会内部会拷贝一个conn, 有点类似thread传参机制.
        // bind可以用lambda代替, 但不能是引用捕获. 如果是引用捕获, 后续runInLoop拷贝整个lambda表达式, 对里面的conn也会用引用拷贝, 而不是值拷贝.
    }
}

// 设置底层subloop的个数
void TcpServer::setThreadNum(int numThreads)
{
    int numThreads_=numThreads;
    threadPool_->setThreadNum(numThreads_);
}

// 开启服务器监听
void TcpServer::start()
{
    // if (started_ == false) {
    //     started_ = true;
    // } // 这样写是线程不安全的, 所以用exchange, 或者对于atomic<int>变量用fetch_add(1), 但他们返回的都是旧值. 不影响判断逻辑.
    if (started_.exchange(true) == false)    // 防止一个TcpServer对象被start多次, 用线程安全的写法, 
    {
        threadPool_->start(threadInitCallback_);    // 启动底层的loop线程池
        // loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get())); //让这个EventLoop，也就是mainloop来执行Acceptor的listen函数，开启服务端监听
        loop_->runInLoop([this](){
            this->acceptor_->listen();
        });
    }
}

// 有一个新用户连接，acceptor会执行这个回调操作，负责将mainLoop接收到的请求连接(acceptChannel_会有读事件发生)通过回调轮询分发给subLoop去处理
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
   // 轮询算法 选择一个subLoop 来管理connfd对应的channel
    EventLoop *ioLoop = threadPool_->getNextLoop();
    // char buf[64] = {0};
    // snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
    // std::string connName = name_ + buf;
    std::string connName = name_ + "-" + ipPort_ + "#" + std::to_string(nextConnId_);
    ++nextConnId_;  // 这里没有设置为原子类是因为其只在mainloop中执行 不涉及线程安全问题

    LOG_INFO("TcpServer::newConnection [%s] - new connection [%s] from %s\n",
             name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());
    
    // 通过sockfd获取其绑定的本机的ip地址和端口信息
    sockaddr_in local;
    ::memset(&local, 0, sizeof(local));
    socklen_t addrlen = sizeof(local);
    if(::getsockname(sockfd, (sockaddr *)&local, &addrlen) < 0)
    {
        LOG_ERROR("sockets::getLocalAddr");
    }

    InetAddress localAddr(local);
    // new改成make_shared
    TcpConnectionPtr conn = std::make_shared<TcpConnection> (ioLoop,
                                            connName,
                                            sockfd,
                                            localAddr,
                                            peerAddr);
    connections_[connName] = conn;

    // 下面的回调都是用户设置给TcpServer => TcpConnection的，至于Channel绑定的则是TcpConnection设置的四个，handleRead,handleWrite... 这下面的回调用于handlexxx函数中
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // 设置了如何关闭连接的回调
    // conn->setCloseCallback(
    //     std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));
    conn->setCloseCallback([this](const TcpConnectionPtr &conn) { // 注意, 这儿的conn与外面的conn重名了, 小心
        this->removeConnection(conn);
    });

    // 这个runInLoop就是切换线程执行回调, 从主Reactor/主线程/mainLoop 切换 到从Reactor/subLoop, 之前也讲过, "切换线程"这个词很准确啊. 都以前讲EventLoop好好讨论过的逻辑.
    // ioLoop->runInLoop(
    //     std::bind(&TcpConnection::connectEstablished, conn));
    ioLoop->runInLoop([conn]() {
        conn->connectEstablished();
    });
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{
    // loop_->runInLoop(
    //     std::bind(&TcpServer::removeConnectionInLoop, this, conn));
    loop_->runInLoop([this, conn](){
        this->removeConnectionInLoop(conn);
    });
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s\n",
             name_.c_str(), conn->name().c_str());

    connections_.erase(conn->name());
    EventLoop *ioLoop = conn->getLoop();
    // ioLoop->queueInLoop(
    //     std::bind(&TcpConnection::connectDestroyed, conn));
    ioLoop->queueInLoop([conn](){
        conn->connectDestroyed();
    });
}
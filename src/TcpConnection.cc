#include <functional>
#include <string>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/tcp.h>
#include <sys/sendfile.h>
#include <fcntl.h> // for open
#include <unistd.h> // for close

#include "TcpConnection.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

static EventLoop *CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainLoop is null!\n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop *loop,
                             const std::string &nameArg,
                             int sockfd,
                             const InetAddress &localAddr,
                             const InetAddress &peerAddr)
    : loop_(CheckLoopNotNull(loop))
    , name_(nameArg)
    , state_(kConnecting)
    , reading_(true)
    , socket_(new Socket(sockfd))
    , channel_(new Channel(loop, sockfd))
    , localAddr_(localAddr)
    , peerAddr_(peerAddr)
    , highWaterMark_(64 * 1024 * 1024) // 64M
{
    channel_->setReadCallback([this](Timestamp receiveTime){
        handleRead(receiveTime);
    });
    channel_->setWriteCallback([this] { handleWrite(); });
    channel_->setCloseCallback([this] { handleClose(); });
    channel_->setErrorCallback([this] { handleError(); });

    LOG_INFO("TcpConnection::ctor[%s] at fd=%d\n", name_.c_str(), sockfd);
    socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[%s] at fd=%d state=%d\n", name_.c_str(), channel_->fd(), (int)state_);
}

/*
我感觉send非常关键啊, 
1. 目前代码中send是在OnMessage中调用的, 而OnMessage回调, 从main函数->TcpServer->TcpConnection->channel这样一层层传递回调的. 
2. send和OnMessage回调一定是在subReactor中执行, 不可能在mainLoop和mainReactor中执行. 这底层是channel的逻辑.和Acceptor的逻辑. 
3. 但subReactor如果有工作线程, 如下面例子, send依然可能跨线程执行, 但不是从subReactor跨到主Reactor, 而是跨到worker线程池, 但worker线程池中的TcpConnection conn, 里面保存了loop, 在完成复制的图像等耗时计算后, 会继续调用conn->send, 此时就跨线程了. 
void onMessage(...) {
    // 1. 读数据 (耗时 0.1ms)
    string msg = buf->retrieveAllAsString();
    
    // 2. 业务逻辑：比如图像识别 (耗时 100ms)
    string result = heavyCompute(msg); // <--- 这里阻塞了 SubReactor 100ms！ 
    
    // 3. 发送结果 (耗时 0.1ms)
    conn->send(result);

    // 2,3 的逻辑改造, 引入Worker线程池, 这样就会有跨线程,  但不是从subReactor跨到主Reactor, 而是跨到worker线程池,
    threadPool.run([conn, msg] {
        // --- Worker 线程 ---
        // 3. 业务逻辑 (耗时 100ms)
        // 注意：这里是在 Worker 线程里跑的！SubReactor 早就去干别的活了！
        string result = heavyCompute(msg); 
        
        // 4. 发送结果 (耗时 0.01ms)
        // 只是把结果塞进 SubReactor 的队列，非常快
        conn->send(result); 
    });
}
4. 最后是buf的生命周期问题, 施磊老师重构的muduo, 没考虑清楚这一点, 应该参考muduo源码, buf要额外处理. 我把buf命名成message, 并已处理.
5. isInLoopThread()的逻辑已经在runInLoop中有了, 不可以直接调用loop_->runInLoop吗? 我一开始就是从这个逻辑点, 延伸出这么一大片逻辑思考. 这里这样做是性能优化.
*/

// 这段代码终究是错付了, 字符串字面量, string左值, string右值, string_view等情况. 要覆盖所有情况, 需要写const char*, string&&, string_view这三种情况, 细节我不说了. 后面直接统一成完美引用, 模板函数.
// void TcpConnection::send(std::string message)
// {
//     if (state_ == kConnected)
//     {
//         if (loop_->isInLoopThread())
//         {
//             // 在当前线程，直接调用 sendInLoop
//             sendInLoop(message.data(), message.size()); // data和c_str都一样, 后者c风格. data语义更明确.
//         }
//         else
//         {
//             // 跨线程调用：必须把 message 移动到 Lambda 中！
//             // lambda的[this, msg = std::move(message)]里面相当于自带auto. [&]全捕获几乎不会影响性能, 只是按需捕获更安全.
//             auto ptr = shared_from_this(); // 跨线程, cb在排队执行, 如果客户端断开连接, TcpConnection对象销毁了, 就不能执行了.
//             loop_->runInLoop([ptr, msg = std::move(message)](){
//                 ptr->sendInLoop(msg.data(), msg.size());
//             });
//             // loop_->runInLoop(
//             //     std::bind(&TcpConnection::sendInLoop, this, message.c_str(), message.size()));
//         }
//     }
// }

void TcpConnection::send(const void* data, size_t len) 
{
    send(std::string_view(static_cast<const char*>(data), len));
}

// 极致优化, 陈硕优化未遂, 可能是当年的bind局限性.
void TcpConnection::send(Buffer* buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            // 同线程：零拷贝，直接用裸指针
            sendInLoop(buf->peek(), buf->readableBytes());
            buf->retrieveAll();
        }
        else
        {
            // 跨线程：swap 把 buffer 内容"偷"走，O(1) // 经典swap惯用法.
            Buffer tempBuf;
            tempBuf.swap(*buf);  // 只交换3个字段，不拷贝数据
            loop_->runInLoop([self = shared_from_this(), buf = std::move(tempBuf)] {
                self->sendInLoop(buf.peek(), buf.readableBytes());
            });
        }
    }
}

/**
 * 发送数据 应用写的快 而内核发送数据慢 需要把待发送数据写入缓冲区，而且设置了水位回调
 **/
void TcpConnection::sendInLoop(const void *data, size_t len)
{
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;

    if (state_ == kDisconnected) // 之前调用过该connection的shutdown 不能再进行发送了
    {
        LOG_ERROR("disconnected, give up writing");
        return;
    }

    // if no thing in output queue, try writing directly.
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(channel_->fd(), data, len); // 明白, 你这儿发, 也不会保证全部发完啊, 有remaing.
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            if (remaining == 0 && writeCompleteCallback_)
            {
                // 既然在这里数据全部发送完成，就不用再给channel设置epollout事件了
                // loop_->queueInLoop(
                //     std::bind(writeCompleteCallback_, shared_from_this()));
                loop_->queueInLoop([self = shared_from_this()] {
                    self->writeCompleteCallback_(self);
                });
            }
        }
        else // nwrote < 0
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK) // EWOULDBLOCK表示非阻塞情况下没有数据后的正常返回 等同于EAGAIN
            {
                LOG_ERROR("TcpConnection::sendInLoop");
                if (errno == EPIPE || errno == ECONNRESET) // SIGPIPE RESET
                {
                    faultError = true;
                }
            }
        }
    }
    /**
     * 说明当前这一次write并没有把数据全部发送出去 剩余的数据需要保存到缓冲区当中(append到outputBuffer_中)
     * 然后给channel注册EPOLLOUT事件，Poller发现tcp的发送缓冲区有空间后会通知
     **/ 
    // 卡码笔记有些傻逼注释, 不会写就别写, 我已经删除. 所以学东西要学一手的, 二手的什么垃圾.
    if (!faultError && remaining > 0)
    {
        // 目前发送缓冲区剩余的待发送的数据的长度
        size_t oldLen = outputBuffer_.readableBytes();
        if (oldLen + remaining >= highWaterMark_ && oldLen < highWaterMark_ && highWaterMarkCallback_) // testserver中没设置这个回调, 程序比较简单, 不用也罢. 但  在生产环境中，不设置高水位回调是一个巨大的隐患，可能会导致内存耗尽（OOM）。
        {
            loop_->queueInLoop([self = shared_from_this(), waterMark = oldLen + remaining] {
                self->highWaterMarkCallback_(self, waterMark);
            });
        }
        outputBuffer_.append((char *)data + nwrote, remaining);
        if (!channel_->isWriting())
        {
            channel_->enableWriting(); // 这里一定要注册channel的写事件 否则poller不会给channel通知epollout
        }
    }
}

void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        setState(kDisconnecting);
        // loop_->runInLoop([this] { shutdownInLoop(); }); // 这里跨线程了, 要用shared_from_this保护.
        loop_->runInLoop([self = shared_from_this()] { self->shutdownInLoop(); });
    }
}

void TcpConnection::shutdownInLoop()
{
    if (!channel_->isWriting()) // 说明当前outputBuffer_的数据全部向外发送完成? 
    { // isWriting是表示对可以事件感兴趣啊, 应该命名成isWritable吧? isWritable也表示数据在应用层Buffer中没有发完.
      // 见TcpConnection::sendInLoop这个函数最后面, channel_->enableWriting(), TcpConnection::handleWrite有disableWriting
        socket_->shutdownWrite();
    }
}

// 连接建立
void TcpConnection::connectEstablished()
{
    setState(kConnected);
    channel_->tie(shared_from_this()); // 考虑一个情况, TcpConnection销毁(connections_.erase)后, 再销毁channel, 然后从epoll_ctl Delete. 
    // 那如果在TcpConnection和Channel销毁(包括fd从epoll销毁)的中间, epoll又有事件发生, Channel还要执行吗? 通过tie_这个weak_ptr去检查TcpConnection是否挂掉了. 挂掉了就别干了.
    // 他们的销毁会跨线程吗? 答: connections_是在TcpServer中, 主Reactor, 所以connections_.erase时会跨线程.

    channel_->enableReading(); // 向poller注册channel的EPOLLIN读事件

    // 新连接建立 执行回调  这个回调就是testserver里面的用户注册的onConnection
    connectionCallback_(shared_from_this());
}
// 连接销毁, 销毁全流程: channel那儿开始, 调用回调closeCallback_, 定义在TcpConnection的handleClose中, 然后调用connectionCallback_(定义在testserver的onConnection)和closeCallback_(定义在TcpServer::removeConnection), 然后connections_.erase销毁对象, 紧接着执行conn->connectDestroyed();
// 线程分析: 关键是OnMessage和TcpServer::removeConnection, 他们都在TcpConnection的上层定义的, 但前者是有subLoop执行的, 后者有loop_->runInLoop, 而这里的loop_因为是TcpServer的loop_所以切换到主线程了. 那如何从主线程切回去呢? EventLoop *ioLoop = conn->getLoop(); ioLoop->queueInLoop(...conn->connectDestroyed();...)
// AI说的: TcpConnection对象的销毁, 除了“户口登记”和“户口注销”是在 MainLoop，其他的“生老病死”全都在 SubLoop。
void TcpConnection::connectDestroyed()
{
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll(); // 把channel的所有感兴趣的事件从poller中删除掉
        connectionCallback_(shared_from_this()); // 这儿调用用户注册的回调函数 删除连接和建立连接都是onConnection, 应该分开的.
    }
    channel_->remove(); // 把channel从poller中删除掉
}

// 读是相对服务器而言的 当对端客户端有数据到达 服务器端检测到EPOLLIN 就会触发该fd上的回调 handleRead取读走对端发来的数据
void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0) // 有数据到达
    {
        // 已建立连接的用户有可读事件发生了 调用用户传入的回调操作onMessage shared_from_this就是获取了TcpConnection的智能指针
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime); // 这个很重要啊, 这个函数就是main函数中设置的用户回调onMessage, 而这个handleRead又是注册给channel的回调, 最终是在subLoop中调用的.
        /*
        举例: sp1->对象(this), sp2 = sp1, 这样才能共享(共享一个控制块). 如果你用this创建一个sp2(创建一个新的控制块), 那么sp2和sp1不知道对方的存在, 导致double delete.
        底层原理: TcpConnection继承了public std::enable_shared_from_this<TcpConnection>, 其底层有一个weak_ptr. 这里就是把weak_ptr升级成shared_ptr返回而已. 其他细节就别说了.
        */
    }
    else if (n == 0) // 客户端断开
    {
        handleClose();
    }
    else // 出错了
    {
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead");
        handleError();
    }
}

void TcpConnection::handleWrite()
{
    if (channel_->isWriting()) // isWritable命名更合理吧, 判断是否可写. 看它对EPOLLOUT事件是否感兴趣.
    {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0)
        {
            outputBuffer_.retrieve(n);//从缓冲区读取reable区域的数据移动readindex下标
            if (outputBuffer_.readableBytes() == 0)
            {
                channel_->disableWriting();
                if (writeCompleteCallback_)
                {
                    // TcpConnection对象在其所在的subloop中 向pendingFunctors_中加入回调
                    // 质疑: 这儿有必要这样写吗? 线程切换问题?
                    loop_->queueInLoop([self = shared_from_this()] {
                        self->writeCompleteCallback_(self);
                    });
                }
                if (state_ == kDisconnecting)
                {
                    shutdownInLoop(); // 在当前所属的loop中把TcpConnection删除掉
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite");
        }
    }
    else
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writing", channel_->fd());
    }
}

void TcpConnection::handleClose()
{
    LOG_INFO("TcpConnection::handleClose fd=%d state=%d\n", channel_->fd(), (int)state_);
    setState(kDisconnected);
    channel_->disableAll();

    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr); // 调用用户自定义的连接事件处理函数onConnectionCallback, 新连接和断开连接都可以调用.
    closeCallback_(connPtr);      // 执行关闭连接的回调 执行的是TcpServer::removeConnection回调方法   // must be the last line
}

void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof optval;
    int err = 0;
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        err = errno;
    }
    else
    {
        err = optval;
    }
    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d\n", name_.c_str(), err);
}

// 新增的零拷贝发送函数
void TcpConnection::sendFile(int fileDescriptor, off_t offset, size_t count) {
    if (connected()) {
        if (loop_->isInLoopThread()) { // 判断当前线程是否是loop循环的线程
            sendFileInLoop(fileDescriptor, offset, count);
        }else{ // 如果不是，则唤醒运行这个TcpConnection的线程执行Loop循环
            loop_->runInLoop(
                std::bind(&TcpConnection::sendFileInLoop, shared_from_this(), fileDescriptor, offset, count));
        }
    } else {
        LOG_ERROR("TcpConnection::sendFile - not connected");
    }
}

// 在事件循环中执行sendfile
void TcpConnection::sendFileInLoop(int fileDescriptor, off_t offset, size_t count) {
    ssize_t bytesSent = 0; // 发送了多少字节数
    size_t remaining = count; // 还要多少数据要发送
    bool faultError = false; // 错误的标志位

    if (state_ == kDisconnecting) { // 表示此时连接已经断开就不需要发送数据了
        LOG_ERROR("disconnected, give up writing");
        return;
    }

    // 表示Channel第一次开始写数据或者outputBuffer缓冲区中没有数据
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
        bytesSent = sendfile(socket_->fd(), fileDescriptor, &offset, remaining);
        if (bytesSent >= 0) {
            remaining -= bytesSent;
            if (remaining == 0 && writeCompleteCallback_) {
                // remaining为0意味着数据正好全部发送完，就不需要给其设置写事件的监听。
                loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
            }
        } else { // bytesSent < 0
            if (errno != EWOULDBLOCK) { // 如果是非阻塞没有数据返回错误这个是正常显现等同于EAGAIN，否则就异常情况
                LOG_ERROR("TcpConnection::sendFileInLoop");
            }
            if (errno == EPIPE || errno == ECONNRESET) {
                faultError = true;
            }
        }
    }
    // 处理剩余数据
    if (!faultError && remaining > 0) {
        // 继续发送剩余数据
        loop_->queueInLoop(
            std::bind(&TcpConnection::sendFileInLoop, shared_from_this(), fileDescriptor, offset, remaining));
    }
}
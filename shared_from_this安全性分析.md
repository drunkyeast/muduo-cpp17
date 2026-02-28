需要 shared_from_this() 的场景是：回调被投递到 queueInLoop / runInLoop。因为这些 functor 存储在 pendingFunctors_ 队列里，执行时 TcpConnection 可能已经从 TcpServer 的 map 中移除了，如果不持有 shared_ptr，对象可能已经析构。


TcpConnection的构造函数中. 
channel_->setReadCallback(
    std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
channel_->setReadCallback([this](Timestamp receiveTime){
    handleRead(receiveTime);
});
这里用 this 就够了
就考虑一点, 回调函数执行时, this是否有可能消亡? channel有个tie机制兜底.channel_ 是 TcpConnection 的成员（unique_ptr<Channel>），生命周期严格短于 TcpConnection




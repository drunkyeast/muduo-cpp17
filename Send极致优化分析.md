## 陈硕muduo源码, 以及我的改进.
```cpp
void TcpConnection::send(const void* data, int len)
{
  send(StringPiece(static_cast<const char*>(data), len));
}

void TcpConnection::send(const StringPiece& message)
{
  if (state_ == kConnected)
  {
    if (loop_->isInLoopThread())
    {
      sendInLoop(message);
    }
    else
    {
      void (TcpConnection::*fp)(const StringPiece& message) = &TcpConnection::sendInLoop;
      loop_->runInLoop(
          std::bind(fp,
                    this,     // FIXME                          // shared_from_this 线程安全, 这个cb再队列中等待执行, 但是客户端断开连接了, TcpConnection即这个this销毁了, 再执行就出错了.
                    message.as_string()));
                    //std::forward<string>(message)));          // 当年std::bind不可避免要拷贝? 不能移动? C++14的lambda+move解决了这个问题?

    }
  }
}

// FIXME efficiency!!!                                          // 我用了swap惯用法, 窃取Buffer资源. C++14的lambda+move解决了这个问题.
void TcpConnection::send(Buffer* buf)
{
  if (state_ == kConnected)
  {
    if (loop_->isInLoopThread())
    {
      sendInLoop(buf->peek(), buf->readableBytes());
      buf->retrieveAll();
    }
    else
    {
      void (TcpConnection::*fp)(const StringPiece& message) = &TcpConnection::sendInLoop;
      loop_->runInLoop(
          std::bind(fp,
                    this,     // FIXME                          // shared_from_this 线程安全, 这个cb再队列中等待执行, 但是客户端断开连接了, TcpConnection即这个this销毁了, 再执行就出错了.
                    buf->retrieveAllAsString()));
                    //std::forward<string>(message)));          // 当年std::bind不可避免要拷贝? 不能移动? C++14的lambda+move解决了这个问题?

    }
  }
}

// 此外我还用万能引用+完美转发, 统一const char*, string_view, string, string&& 这些接口.
```
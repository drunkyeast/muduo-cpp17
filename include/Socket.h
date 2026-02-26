#pragma once

#include "noncopyable.h"

class InetAddress;

// 封装socket fd
class Socket : noncopyable
{
public:
    explicit Socket(int sockfd)
        : sockfd_(sockfd)
    {
    }
    ~Socket();

    int fd() const { return sockfd_; }
    void bindAddress(const InetAddress &localaddr);
    void listen();
    int accept(InetAddress *peeraddr);

    void shutdownWrite(); // 优雅的半关闭

    void setTcpNoDelay(bool on); // 禁用Nagle
    void setReuseAddr(bool on); // Time_wait导致暂时不能绑定端口
    void setReusePort(bool on); // 负载均衡相关
    void setKeepAlive(bool on); // TCP的keep-alive
    // 这把背的八股都用上了, 只有负载均衡是之前没见过的.

private:
    const int sockfd_;
};

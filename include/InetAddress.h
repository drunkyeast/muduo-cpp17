#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>

// 封装 socket 地址类型
class InetAddress
{
public:
    explicit InetAddress(uint16_t port = 0, const std::string &ip = "127.0.0.1");
    explicit InetAddress(const sockaddr_in &addr) noexcept
        : addr_(addr)
    {
    }

    std::string toIp() const;
    std::string toIpPort() const;
    uint16_t toPort() const noexcept;

    const sockaddr_in *getSockAddr() const noexcept { return &addr_; }
    void setSockAddr(const sockaddr_in &addr) noexcept { addr_ = addr; }

private:
    sockaddr_in addr_;
};

#include <cstring>

#include "InetAddress.h"

InetAddress::InetAddress(uint16_t port, const std::string &ip)
    : addr_{} // 值初始化, 全部清零, 替代memset
{
    addr_.sin_family = AF_INET;
    addr_.sin_port = ::htons(port);                          // 本地字节序转网络字节序
    ::inet_pton(AF_INET, ip.c_str(), &addr_.sin_addr);       // 点分十进制 -> 二进制
}

std::string InetAddress::toIp() const
{
    char buf[64];
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf)); // 二进制 -> 点分十进制
    return buf;
}

std::string InetAddress::toIpPort() const
{
    char buf[64];
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
    size_t end = ::strlen(buf);
    uint16_t port = ::ntohs(addr_.sin_port);                 // 网络字节序转本地字节序
    snprintf(buf + end, sizeof(buf) - end, ":%u", port);
    return buf;
}

uint16_t InetAddress::toPort() const noexcept
{
    return ::ntohs(addr_.sin_port);
}

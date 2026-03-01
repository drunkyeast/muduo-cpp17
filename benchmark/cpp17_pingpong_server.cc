// C++17 魔改版 pingpong server
#include "TcpServer.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include "Logger.h"

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

void onConnection(const TcpConnectionPtr &conn)
{
    if (conn->connected())
    {
        conn->setTcpNoDelay(true);
    }
}

void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp)
{
    conn->send(buf);
}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        fprintf(stderr, "Usage: server <address> <port> <threads>\n");
        return 1;
    }

    const char *ip = argv[1];
    uint16_t port = static_cast<uint16_t>(atoi(argv[2]));
    int threadCount = atoi(argv[3]);

    Logger::instance().setLogLevel(LogLevel::ERROR);

    EventLoop loop;
    InetAddress listenAddr(port, ip);

    TcpServer server(&loop, listenAddr, "PingPong");

    server.setConnectionCallback(onConnection);
    server.setMessageCallback(onMessage);

    if (threadCount > 1)
    {
        server.setThreadNum(threadCount);
    }

    server.start();
    loop.loop();
    return 0;
}

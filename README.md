# 简历相关
## 别细枝末节了, 面向面试了, 面向简历修改了.
1. 最后再改一下格式问题, 内容就差不多了. (以后再做吧.)
2. 直接准备面试, 项目介绍设计模式 + send极致优化 + lambda代替bind且用sink parameter惯用法 + C++17的其他新特性
3. 性能测试QPS.
4. 然后就先写简历了. 写完就开始投几个简历.
5. 之后就吧时间花到八股上面而来. 其实我觉得在这项目上花时间还是挺值得的, 真的能学到东西, 刷题花很多时间, 收获不明显.

## 面试时怎么讲呢?
写一个3分钟项目介绍稿.
例如one loop per thread, Reactor架构等等.
Reactor设计模式.

One Loop Per Thread 不是说"对象只能被一个线程知道"，而是说"对象只能在它所属的线程中被操作".
例如某个socket/channel/TcpConnection要close关闭了, 不是在子Reactor中直接关闭, 而是要绕一圈到主Reactor再到子Reactor. 然后主Reactor(即TcpServer)里面又保存了所有的connection/socket/channel的信息.
AI说TcpServer 只是"持有引用"，不是"操作对象". TcpConnection 的操作权属于子线程.

## 这个项目怎么这么多回调函数? 用了哪些设计模式？
网络库/异步框架：都很多.
原因：异步非阻塞的必然选择——不知道什么时候有连接、数据、可写事件发生。这个我调试是很清楚的，一个while循环中，有事件发生了才会触发相关逻辑执行。
异步编程的必然选择：不知道事件什么时候发生，所以只能先注册回调，事件发生时由框架调用。
事件类型多样：连接建立、数据到达、可写、连接关闭等，每种事件需要不同的回调处理。
分层解耦：用户层通过回调告诉框架层业务逻辑，框架层通过回调告诉网络层事件处理，实现了业务代码和框架代码的解耦。
这是观察者模式和Reactor模式的体现，也是所有事件驱动框架的共同特点，比如libevent、Node.js都是类似的设计。

1. Reactor模式是核心，整个架构是事件驱动的。EventLoop不断轮询等待事件，然后通过Channel分发给对应的处理器。
2. 观察者模式体现在Channel和TcpConnection之间。Channel通过注册回调函数，当事件发生时通知TcpConnection处理。
3. 工厂模式用于创建Poller，通过newDefaultPoller根据环境变量决定使用epoll还是poll。
4. 单例模式用于Logger，保证全局只有一个日志对象。
RAII和线程池算设计模式吗？

## 深入技术点一: send极致优化
深入讲几个技术点, shared_from_this, swap惯用法.

## 深入技术点二: 
bind改成lambda
跨线程引用捕获的bug
讲一下我C++17的优化点。 

修改建议优先级
高优先级：inline变量、std::string_view、lambda替代std::bind、[[nodiscard]]
中优先级：结构化绑定、std::optional、std::scoped_lock
低优先级：其他特性按需使用

## 深入技术点三: Functor也写成完美转发? 回调的开销.

## 
面试怎么聊
数据上说实话，不要编。正确的说法是：
"在同线程 pingpong echo 场景下，吞吐量与原版 muduo 持平（~200 MiB/s）" —— 证明你的重构没有引入性能回退，这本身就是有价值的。
"优化点针对跨线程 send 场景" —— 原版 send(Buffer*) 跨线程时做 retrieveAllAsString() 是 O(n) 拷贝（陈硕自己标了 FIXME efficiency!!!），我用 Buffer swap 做到 O(1)。这个优化在 worker 线程池模式下才体现，即 onMessage 中把耗时任务丢到 worker 线程，worker 线程完成后调 conn->send() 跨线程投递。
lambda 替代 bind —— 编译器更容易内联，避免 std::function 的类型擦除开销。
完美转发统一 send 接口 —— 原版需要 StringPiece、Buffer*、const void* 等多个重载，我用万能引用模板统一了字符串类 send，右值 string 走 move 零拷贝，const char* 走 string_view 轻量构造。
面试时重点讲设计思想和对场景的理解，不要硬吹数字。面试官问"性能提升多少"，你说"同场景持平，但跨线程 send 从 O(n) 拷贝优化到 O(1) swap"，这比编一个数字要有说服力得多。

## Benchmark

### 前置依赖

原版 muduo 源码使用了 `boost::any`，需要安装 Boost 头文件：

```bash
sudo apt-get install -y libboost-dev
```

### GCC 13+ 编译兼容性

原版 muduo 的 `Date.cc` 在 GCC 13 下会报 `incomplete type 'struct tm'` 错误，
已在 `benchmark/muduo-origin/muduo/base/Date.cc` 头部添加 `#include <ctime>` 修复。

### 编译与运行

```bash
cd benchmark

# 编译 (Release -O2)
bash build.sh

# 快速测试 (~40秒, 单线程)
bash run_quick_test.sh

# 完整测试 (~4分钟, 单线程)
bash run_benchmark.sh

# 多线程测试 (1/2/4 线程对比)
bash run_benchmark.sh --threads "1 2 4"
```

脚本会自动将 server 和 client 绑定到不同的 CPU 核心组，避免互相争抢。

# ---------------------------------------------------------

# muduo-core

> **本项目目前只在[知识星球](https://programmercarl.com/other/kstar.html)答疑并维护**。

[知识星球](https://programmercarl.com/other/kstar.html)再添 CPP项目专栏， 关于网络库，知名的就是陈硕的muduo

之前也有不少录友，自己做一个muduo写到简历上。

这次 我们从 面试的角度带大家速成muduo，**【项目细节】【项目面试常见问题汇总】【拓展出的基础知识汇总】【测试相关问题】【简历写法】** 都给大家安排的明明白白。

## 为什么要做 muduo？

*  通过学习muduo网络库源码，一定程度上提升了linux网络编程能力;
*  熟悉了网络编程及其下的线程池，缓冲区等设计，学习了多线程编程;
*  通过深入了解muduo网络库源码，对经典的五种IO模型及Reactor模型有了更深的认识
*  掌握基于事件驱动和事件回调的epoll+线程池面向对象编程。

## 参考书籍

* 陈硕（官方）：https://github.com/chenshuo/muduo/
* 《Linux多线程服务器编程-使用 muduo C++网络库》-陈硕
* 《Linux高性能服务器编程》-游双

## 项目专栏目录

* muduo网络库项目前言
    * 为什么要做 muduo？
    * 所需要的基础知识
    * 参考书籍
* 框架梳理
* 并发框架
    * Channel
        * Channel类重要的成员变量：
        * Channel类重要的成员方法
    * Poller
        * Poller/EpollPoller概述
        * Poller/EpollPoller的重要成员变量：
        * EpollPoller给外部提供的最重要的方法：
    * EventLoop
        * EventLoop概述：
        * One Loop Per Thread 含义介绍
        * 全局概览Poller、Channel和EventLoop在整个Multi-Reactor通信架构中的角色
        * EventLoop重要方法 EventLoop:loop()：
    * Acceptor
        * Acceptor封装的重要成员变量
        * Acceptor封装的重要成员方法
    * tcpconnection
        * TcpConnection的重要变量
        * TcpConnection的重要成员方法：
    * socket
    * buffer
        * 重要的成员方法：
* 项目介绍
    * 简单介绍一下你的项目
* 项目面试常见问题汇总
    * 项目中的难点？
        * 如果TcpConnection中有正在发送的数据，怎么保证在触发TcpConnection关闭机制后，能先让TcpConnection先把数据发送完再释放TcpConnection对象的资源？
    * 项目中遇到的困难？是如何解决的？
        * 怎么保证一个线程只有一个EventLoop对象
        * 怎么保证不该跨线程调用的函数不会跨线程调用
    * 项目当中有什么亮点
        * Channel的tie _ 涉及到的精妙之处
* 项目细节
    * 日志系统
        * 异步日志流程
        * 开启异步日志
        * 把日志写入缓冲区
    * 缓存机制
        * Buffer数据结构
        * 把socket上的数据写入Input Buffer
        * 把用户数据通过output buffer发送给对方
    * muduo定时器实现思路
* 项目拓展出的基础知识汇总
    * IO多路复用
        * 说一下什么是ET，什么是LT，有什么区别？
        * 为什么ET模式不可以文件描述符阻塞，而LT模式可以呢？
        * 你用了epoll，说一下为什么用epoll，还有其他多路复用方式吗？区别是什么？
    * 并发模型
        * reactor、proactor模型的区别？
        * reactor模式中，各个模式的区别？
    * 测试相关问题
* 简历写法 & 面试技巧
    * 本项目简历写法
    * 通用简历写法
    * 面试技巧
        * 八股
        * 算法
        * 实习
        * 项目



## 简历写法

为了避免[知识星球](https://programmercarl.com/other/kstar.html)里大家学习这个项目写简历重复，本项目专栏提供了三种简历写法：

![](https://file1.kamacoder.com/i/algo/20240904205019.png)

## 本项目常见问题

面试中，面试官最喜欢问的就是项目难点，以及这个难点你是如何解决的。

专栏里都给出明确的例子：

![](https://file1.kamacoder.com/i/algo/20240904204734.png)

## 项目亮点以及项目细节

为了更好的掌握这个项目，亮点和细节都给大家讲清楚：

![](https://file1.kamacoder.com/i/algo/20240904204822.png)

## 项目拓展出的基础知识

在做做项目的时候，最好的方式就是 理论基础知识和项目实战相结合。

面试官也喜欢在 项目中问基础知识（八股文），本专栏也给出muduo可以拓展哪些基础知识

![](https://file1.kamacoder.com/i/algo/20240904204936.png)

## 项目专栏部分截图

![](https://file1.kamacoder.com/i/algo/20240904204906.png)

![](https://file1.kamacoder.com/i/algo/20240904205923.png)

## 突击来用

如果大家面试在即，实在没时间做项目了，可以直接按照专栏给出的【简历写法】，写到简历上，然后把项目专栏里的面试问题，都认真背一背就好了，基本覆盖 绝大多数 RPC项目问题。

## 答疑

本项目在[知识星球](https://programmercarl.com/other/kstar.html)里为 文字专栏形式，大家不用担心，看不懂，星球里每个项目有专属答疑群，任何问题都可以在群里问，都会得到解答：

![](https://file1.kamacoder.com/i/web/2025-09-26_11-30-13.jpg)


## 获取本项目专栏

**本文档仅为星球内部专享，大家可以加入[知识星球](https://programmercarl.com/other/kstar.html)里获取，在星球置顶**



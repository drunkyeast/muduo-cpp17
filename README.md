# 简历相关
## 简历项目描述
技术栈： C++17、Epoll、Reactor、多线程、CMake、Protobuf、Zookeeper
项目描述：本项目使用C++17对muduo网络库进行了重构，去除boost库依赖，实现多Reactor多线程的高并发网络库。并在此基础上，结合Protobuf自主实现了分布式环境下本地服务在RPC节点上的注册、发布与远程调用功能。
muduo库架构：非阻塞IO + Epoll水平触发 + 主从Reactor。主Reactor的Acceptor接受新连接，将其封装成TcpConnection并轮询分发给从Reactor。从Reactor采用one loop per thread模型（一个EventLoop和一个线程唯一绑定），EventLoop通过EpollPoller封装epoll_wait来循环监听已注册的Channel，获取就绪的 Channel 列表并依次触发其读写回调。
C++17重构：用 lambda 替代 std::bind 传递回调，提升可读性并支持 move 捕获；重新设计 send 接口，通过万能引用统一所有字符串类发送路径，Buffer跨线程发送使用swap窃取资源，从 O(n) 拷贝优化为 O(1)；采用 RAII 和智能指针管理连接的生命周期；以及sink parameter、inline变量、scoped_lock、enum class等现代C++惯用法和特性。
PRC功能：基于 Protobuf 的 Service/Stub 体系实现 RPC 闭环，完成请求的序列化与反序列化、客户端透明调用与服务端动态路由。设计 `[TotalLen][HeaderLen][Header][Body]` 四段式协议解决 TCP 粘包问题。使用 ZooKeeper 实现服务注册与发现。

## 3分钟项目介绍稿(待大幅修改)

【开头 - 15 秒】
这个项目是用 C++17 对陈硕 muduo 网络库的核心模块进行重构，去掉了 boost 库的依赖，实现了一个多 Reactor 多线程的 TCP 网络库。在此基础上还实现了基于 Protobuf 的 RPC 远程调用功能。

【架构与流程】
网络库整体是一个主从 Reactor 架构。主Reactor运行在主线程，只负责监听新连接；从Reactor即subReactor 可以有多个, 每个subReactor跑在一个子线程中，负责已建立连接的 IO 读写。每个 Reactor 本质都是一个 EventLoop，也就是一个事件循环，内部用 epoll 做非阻塞的的事件监听，不断调用epoll_wait，循环监听已注册的fd和事件，每次epoll_wait获取一个事件列表，然后依次处理事件。这里说的比较简单，这就是epoll_wait的正常逻辑，我用面向对象的话语再重复叙述一遍这个过程，一个Reactor对应一个EventLoop事件循环，里面有一个EPollPoller叫做事件监听器又叫做多路复用器（Demultiplex），它其实就是对epoll_wait的封装，这个EPollPoller循环地监听已注册的Channel，这个Channel是对fd、事件、读写回调的封装，所以每次监听得到一个活跃的Channel列表，然后依次对相关的fd和事件执行对应的回调函数。这就是这个网络库的框架了。（我就写这么详细，两套说法，面试时自由发挥嘛）。

服务器启动后(启动细节略)，当有新的连接到来，主Reactor的Acceptor接受新的Tcp连接(具体细节略)，通过轮询从线程池中选一个subReactor，并把新的连接封装成TcpConnection，然后注册到subReactor的epoll上，这个Tcp连接后续就完全在这个subReactor中处理了。之后这个连接的对端发送数据，subReactor的epoll_wait检测到可读事件，（Channel封装了fd、这个可读事件、回调函数），通过Channel调用TcpConnection的handleRead这个回调函数，从内核读数据到用户缓冲区Buffer中，然后再调用用户设置的onMessage回调函数，这个onMessage回调函数是用户写的业务逻辑，通过网络库一层层传递下来的。

整个项目充满了大量的回调函数，而网络库的意义就在于把用户的业务逻辑和底层的网络通信逻辑进行了分离，用户只需要把业务逻辑写到onMessage等这样的回调函数中，设置给网络库就ok了，就不用自己写网络通信的逻辑了。

【问起来了再回答】
one loop per thread 的含义是(这个就不主动说了,问起来了再回答)：字面上看就是一个EventLoop事件循环与一个线程唯一绑定。我说完整一点就是，每个subReactor，都对应一个EventLoop，都运行在一个线程中，都有一个事件监听器EPollPoller对epoll_wait的封装，然后每一个已建立的TCP连接的后续逻辑全在这里面执行。libevent和libev也是这个思想。muduo作者和libev的作者都认为: 高性能网络库的实现，one loop per thread 通常是一个好的设计方法。
 
main函数中的逻辑（供个人理解，这个东西面试就别说）：首先TcpServer构造函数初始化时是设置各种回调、Acceptor的构造函数是bind和设置回调但未listen、线程池的构造函数啥也没有且未启动线程，然后TcpServer.start()是开始listen且启动线程. loop.loop()是开始epoll_wait开始监听事件.

发送数据：在onMessage中可以调用conn->send，逻辑都在sendInLoop中，如果outPutBuffer_是空的就直接调用write发送了，如果write发完后有剩余或者outPutBuffer_不为空那就append到outPutBuffer_末尾并设置Channel为可写事件，让epoll下一次再继续写.

【C++17 改造】具体一些难点和改进点, 围绕std::bind和send接口来说吧.
1. bind->lambda
首先是这个网络库大量使用了std::bind，std::bind是C++11引入的,作用就是把一个函数和它的部分参数绑定在一起,生成一个新的可调用对象, 当时还挺流行的。但是它有两个问题，一是可读性差，绑定成员函数要写函数指针、占位符placeholder；二是std::bind绑定参数时只能拷贝，不支持move语义。所以我把std::bind全部替换成了lambda, 既提升了可读性, 又在某些需要右值和move操作的情况下提升了性能. 例如有worker线程池的场景, worker线程完成耗时计算后, 需要跨线程地把数据和回调投递回subReactor所在的IO线程的pending队列, 用std::bind数据只能拷贝进去, 换用lambda就能move, 实现零拷贝.

注意(供个人理解, 面试不需要说这个):onMessage业务代码就是在handleRead中调用的, 是在subReactor的IO线程中调用的. 但是onMessage里面直接conn->send, 那就不存在跨线程, 都是subReactor所在的IO线程. 如果在onMessage里面写一个worker线程池, 把conn指针传入, 在线程池里面调用conn->send, 这才跨线程, 这会通透了.
或者:这个优化在 worker 线程池模式下才体现，即 onMessage 中把耗时任务丢到 worker 线程，worker 线程完成后调 conn->send() 跨线程投递。

2. 在把std::bind改成lambda的过程中我踩了一个坑, 我一开始图方便lambda全部使用引用捕获, 同线程没有问题, 但跨线程就出bug了, lambda被拷贝进了pending队列后, 原来函数的局部变量就销毁了, 再引用就悬空了. 所以跨线程投递的lambda必须值捕获, 把数据move进去. 另外我还修复了一个原版的bug, muduo网络库的作者标记了一个FIXME, 原版std::bind绑定的是裸指针this, 跨线程把这个回调投递到pending等待队列中等待执行时, 如果这期间客户端断开了, this所对应的对象析构了, this就悬空了, 所以我再lambda捕获列表中用shared_from_this拿了一个shared_ptr, 这样就保证this对象不会提前析构.

3. 第三个是重新设计了send接口, 原版有多个重载，我想统一成一个接口, 但调用方传的类型很杂——string 左值、右值、字符串字面量、string_view 都有可能，此外还要保证内部能支持move操作。 我试过好几种组合，用string_view + string&&两个重载，看似不错，但字符串字面量又能同时匹配这两个接口. 最终我使用万能引用模板一步到位, 同线程时全部构造成string_view, 统一接口。跨线程时用 forward 完美转发，右值 move，左值拷贝。从而实现了一个漂亮的统一的send接口。
send还有一个接口是对于缓存区Buffer的，这个不能统一，单独保留了这个重载，但是muduo作者在源码位置上面标记了FIXME efficiency!!! 所以我又进行了优化. Buffer的底层是一个vector char, 这就是要拷贝或窃取的数据. 我创建一个空的Buffer, 和原Buffer做一个swap操作, 底层就是做一些指针的交换, O(1)的复杂度, 从而窃取走资源, 实现零拷贝.

【问题来了再回答】
性能测试：用 pingpong 吞吐量测试，参考陈硕博客的方案，客户端和服务端持续互发消息，统计单位时间吞吐量。结果与原版 muduo 持平。排查过程中修了几个影响性能的问题：Buffer 里 extrabuf 的 = {} 零初始化每次 read 都多一次 64KB memset；日志级别没区分 debug/release。最后发现还差一个编译优化 flag，加上后性能才一致的。做测试先是在阿里云2核2G上面测试,客户端服务器各占一个CPU核, 但不稳定,后面换到wsl上进行测试的, 16核, 还能多线程, 数据更稳定.
weak_ptr + tie机制：Channel 回调执行时，TcpConnection 可能已经析构了。Channel内部用一个weak_ptr保存TcpConnection。Channel 回调执行时用 weak_ptr::lock 判断 TcpConnection 是否还活着，防止访问已析构对象。
__thread和thread_local:线程局部存储（Thread Local Storage）——用它修饰的变量，每个线程各自拥有一份独立副本，互不干扰。例如每个线程缓存自己的线程id, 避免每次都做系统调用去获取线程id.
以及scoped_lock、enum class等语法糖。略

整个库大概十几个核心类，四千多行代码，通过回调机制把业务代码和底层网络 IO 解耦。

【RPC部分】（没想好怎么回答）
面试官问 Zookeeper 底层怎么答：诚实说你用的是它的服务注册发现能力，然后把你知道的说完，再拉回 muduo：
"Zookeeper 在我项目里主要用作服务注册中心。它底层是一个树状的层级命名空间，每个节点叫 znode。我用的是临时节点——服务上线时创建，session 断开后节点自动删除，这样就实现了服务下线感知。至于它的一致性协议是 ZAB（类似 Raft），这块我没有深入研究。我这个项目的重心主要在网络库的 Reactor 架构和 send 的优化上。

## Benchmark （pingpong测试相关🏓）

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



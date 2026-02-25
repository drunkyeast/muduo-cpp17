# 简历相关
## 2月底mark一下后续优化点.
cb传递, 直接拷贝传递, 还是move后传递.... 在Channel/EpollPoller/EventLoop三个模块中.

## 面试时怎么讲呢?
写一个3分钟项目介绍稿. 梳理流程. 博客已经讲得很清楚了. (你要自己写啊)
深入讲几个技术点, 例如one loop per thread, Reactor架构等等.
讲一下我C++17的优化点。 和一些改进点（甚至可以“人造bug”：聊一下我bind改成lambda，但用的引用捕获导致bug，编故事嘛）

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
## 很重要!! 关于One Loop Per Thread 设计模式.
One Loop Per Thread 不是说"对象只能被一个线程知道"，而是说"对象只能在它所属的线程中被操作".
例如某个socket/channel/TcpConnection要close关闭了, 不是在子Reactor中直接关闭, 而是要绕一圈到主Reactor再到子Reactor. 然后主Reactor(即TcpServer)里面又保存了所有的connection/socket/channel的信息.
AI说TcpServer 只是"持有引用"，不是"操作对象". TcpConnection 的操作权属于子线程.

## 我要用C++17进一步重构, 可以下面这些角度:
修改建议优先级
高优先级：inline变量、std::string_view、lambda替代std::bind、[[nodiscard]]
中优先级：结构化绑定、std::optional、std::scoped_lock
低优先级：其他特性按需使用

## 还有优化
学习std::function的时候, 涉及回调函数是, 有个开销, 用模板函数更好. 我说得不清楚, 直接看下面代码演示.
方案1：使用 std::function（你目前的方式）
```cpp
class EventLoop {
public:
    using Functor = std::function<void()>;
    
    void runInLoop(Functor cb) {
        if (isInLoopThread()) {
            cb();  // 有虚函数调用开销
        } else {
            queueInLoop(std::move(cb));
        }
    }
};
```
方案2：使用模板参数（性能优化版本）
```cpp
class EventLoop {
public:
    // 使用模板参数，让编译器知道具体类型
    template<typename Functor>
    void runInLoop(Functor&& cb) {
        if (isInLoopThread()) {
            cb();  // 可以内联，零开销！
        } else {
            queueInLoop(std::forward<Functor>(cb));
        }
    }
    
private:
    // 存储时仍然用 std::function
    void queueInLoop(std::function<void()> cb) {
        // ...
    }
};
```
这个知识点也可以回答面试官提问: "你的项目是否用到了模板".
答: 是的，我了解到 std::function 有一定的性能开销，主要是类型擦除导致的虚函数调用和可能的堆分配。优化方式是将回调函数作为模板函数的参数，这样编译器在编译期就知道具体类型，可以进行内联优化，避免间接调用。
这个知识点太进阶了. 
答: 你说得模板指的是泛型编程吗? 用的STL这个算用了模板吗? 网络库的业务逻辑是针对具体的网络事件和 TCP 连接，不需要泛型化.

## 关于回调函数
看起来很混乱, 以及std::function, template优化, lambda, 有点理不清楚.
AI回答: 你的代码应该保持 std::function，但可以用 lambda 替代 std::bind：
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



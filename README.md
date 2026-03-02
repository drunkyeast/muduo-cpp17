# 简历相关
## 简历项目描述
技术栈： C++17、Epoll、Reactor、多线程、CMake、Protobuf、Zookeeper
项目描述：本项目使用C++17对muduo网络库进行了重构，去除boost库依赖，实现多Reactor多线程的高并发网络库。并在此基础上，结合Protobuf自主实现了分布式环境下本地服务在RPC节点上的注册、发布与远程调用功能。
muduo库架构：非阻塞IO + Epoll水平触发 + 主从Reactor。主Reactor的Acceptor接受新连接，将其封装成TcpConnection并轮询分发给从Reactor。从Reactor采用one loop per thread模型（一个EventLoop和一个线程唯一绑定），EventLoop通过EpollPoller封装epoll_wait来循环监听已注册的Channel，获取就绪的 Channel 列表并依次触发其读写回调。
C++17重构：用 lambda 替代 std::bind 传递回调，提升可读性并支持 move 捕获；重新设计 send 接口，通过万能引用统一所有字符串类发送路径，Buffer跨线程发送使用swap窃取资源，从 O(n) 拷贝优化为 O(1)；采用 RAII 和智能指针管理连接的生命周期；以及sink parameter、inline变量、scoped_lock、enum class等现代C++惯用法和特性。
PRC功能：基于 Protobuf 的 Service/Stub 体系实现 RPC 闭环，完成请求的序列化与反序列化、客户端透明调用与服务端动态路由。设计 [TotalLen][HeaderLen][Header][Body] 四段式协议解决 TCP 粘包问题。使用 ZooKeeper 实现服务注册与发现。

## 3分钟项目介绍稿(待大幅修改)

【开头 - 15 秒】
这个项目是用 C++17 对陈硕 muduo 网络库的核心模块进行重构，去掉了 boost 依赖，实现了一个多 Reactor 多线程的 TCP 网络库。在此基础上还实现了基于 Protobuf 的 RPC 远程调用功能。
【架构 - 40 秒】
网络库整体是一个主从 Reactor 架构。mainReactor 运行在主线程，只负责监听新连接；subReactor 可以有多个, 每个subReactor跑在一个子线程中，负责已建立连接的 IO 读写。

每个 Reactor 本质都是一个 EventLoop，也就是一个事件循环，内部用 epoll 做非阻塞的的事件监听，不断调用epoll_wait，循环监听已注册的fd和事件，每次epoll_wait获取一个事件列表，然后依次处理事件。这里说的比较简单，这就是epoll_wait的正常逻辑，我用面向对象的话语再重复叙述一遍这个过程，一个Reactor对应一个EventLoop事件循环，里面有一个EPollPoller叫做事件监听器又叫做多路复用器（Demultiplex），它其实就是对epoll_wait的封装，这个EPollPoller循环地监听已注册的Channel，这个Channel是对fd、事件、读写回调的封装，所以每次监听得到一个活跃的Channel列表，然后依次对相关的fd和事件执行对应的回调函数。这就是这个网络库的框架了。（我就写这么详细，两套说法，面试时自由发挥嘛）。

one loop per thread 的含义是：字面上看就是一个EventLoop事件循环与一个线程唯一绑定。以及当每一个TCP连接分配给某个subReactor 后，它的整个生命周期都在那一个线程里处理。（这个感觉没说好）。然后我还要补充，libevent和libev也是这个思想。muduo作者和libev的作者都认为: 高性能网络库的实现，one loop per thread 通常是一个好的方法。
 
【流程 - 60 秒】
具体怎么跑的？服务器启动时，主 Reactor 的 Acceptor 开始 listen，把监听 fd 注册到主 Reactor 的 epoll 上。客户端连接进来时，epoll_wait 返回，通过 Channel 触发 Acceptor 的回调，accept 拿到新连接的 fd，然后通过轮询从线程池选一个从 Reactor，把新连接封装成 TcpConnection，注册到那个从 Reactor 的 epoll 上。
之后对端发数据，从 Reactor 的 epoll_wait 检测到可读，通过 Channel 调用 TcpConnection 的 handleRead，读数据到 Buffer，再调用户设置的 onMessage 回调。这个回调是一层层传下来的：用户在业务层设置，传给 TcpServer，再传给 TcpConnection。发送类似，调 send，发不完就注册可写事件，等 epoll 通知再继续。整个过程事件驱动、非阻塞。
【C++17 改造 - 50 秒】
在重构过程中做了几个主要优化。一是用 lambda 替代 std::bind 传递回调。除了可读性的提升，关键是 lambda 支持 move 捕获——跨线程投递任务时，数据可以 move 进 lambda 而不是拷贝，这在每条跨线程消息都走的热路径上有实际意义。
二是重新设计了 send 接口。原版有多个重载，我用一个万能引用模板统一了所有字符串类的发送路径。同线程直接构造 string_view 零开销发送；跨线程右值 string 走 move。Buffer 的跨线程发送，原版是 retrieveAllAsString 做整段拷贝，陈硕标了 FIXME，我用 swap 窃取 Buffer 资源，从 O(n) 优化到 O(1)。
【结尾 - 15 秒】
另外还用 RAII 和智能指针管理连接的生命周期，以及一些现代 C++ 特性的应用。整个库大概十几个核心类，四千多行代码，通过回调机制把业务代码和底层网络 IO 解耦。

## 长篇梳理(AI梳理的几个技术点, 后续安装技术点来写笔记, 最后再重新组织项目介绍稿)
行, 这是介绍稿, 然后我需要再分点记录一些技术点. 会与3分钟介绍稿重复(而且这个稿子写的不好, 我以后会完全重写), 但是是以单个技术点去讲, 我吧我知道的都说出来, 你来分个类, 因为我说的会很杂乱.
首先肯定安装简历写的几个, one loop per thread和Reactor架构要单独讲一下, 结合Reactor设计模式来讲, 还有我用的是阻塞IO和epoll水平触发, 他们都是同步, iouring这些才是异步, 反正, 阻塞非阻塞都是同步IO, 另外我还有很多笔记要看, 看视频课程和博客梳理时写的, 我此时不能说全. 说全了脑子就很乱啊. 我只需要你帮我做一个梳理, 列个大纲, 讲那些技术点, 例如刚刚说的一长串, 全都是Reactor架构 muduo架构的问题.
另外还有,  lambda代替std::bind, 也是简历上面写的, 展开说., 还有个小故事, 就是我一开始都是引用捕获, 跨线程的时候, 也直接传递, 后来发现是bug, 会导致悬空指针还是悬空引用.  单独起一个分点. send接口也单独起一个.  还有一个是shared_from_this, 和tie机制, 还有weak_ptr, local_thread, 这些涉及线程安全的也要说一下, 每个subLoop正常是不会跨线程的, 只是mainReactor分发给从Reactor的时候需要, 但是subLoop如果还有个自己的线程池, subLoop主要是处理IO的嘛, 如果一些业务情况, 遇到了耗时计算, 不会卡在subLoop, 而是丢给线程池, 此时就要跨线程了, this就要用shared_from_this. 另外channel和TcpConnection也有个线程问题, 操, 我越说越乱, 我现在对这个项目很熟悉, 很多细节都知道, 但不知道要写成什么样子, 表达成什么样子. 最后我还做了性能测试, 用的陈硕的博客, 它对比的事muduo和livevent的吞吐量测试, 而不是百万并发连接的测试, 我也就参考他的吞吐量测试, 命名为pingpong测试, 就是CS之间一直互相发送信息, 统计指定时间内的吞吐量. 然后我C++17重构的与源码在pingpong测试中没有性能差异, 我重构的一些点在跨线程会有用, 但我没有用线程池(不是从Reactor这个线程池, 而是那种耗时计算, 不影响IO的线程池, 你明白吗? 但是我再测试的过程中, 一开始是远低于muduo库源码的, 我修改了一些bug, 例如Buffer中我对extrabuf, 64k大小进行了 = {}初始化, 影响了性能, 删掉了, 还有我对一些没必要的变量使用了atomic, 用普通变量就行了, 这个优化可能没有, 然后是日志模块的一些输出, 要用Debug, 在realse版本要注意输出情况. 做完后发现还是跑不过muduo, 再去看源码, 发现它加了一个编译优化, 而我没加, 加上后测试就差不多了, 会有一些10以内的波动, 受服务器影响, 我是在阿里云2核2G先做的测试, 客户端和服务器各占一个CPU, 波动就很大, 因为就两个核. 然后我切换到wsl中, 16核, 设置了多线程参数, 这个多线程是可以有多个subReactor, 之前在阿里云上测试是mainReactor和subReactor在一个线程, 发现怎么测试, 我魔改版和muduo源码都是一个性能, 波动很小. 我测试脚本也很详细地写了, 花了我半天时间. 
总之我很乱, 我不知道该怎么学习, 可能我这样的方向就错了, 我太想把知道的东西趁热记录下来, 怕过几周又忘记了, 你先给我说个大概分类, 讲这些知识点, 我不需要你说细节, 只需要你帮我理清思路, 最后再给我一点点建议, 以及我这个人性格是不是有问题

正确的做法：先倒，再理。你现在就是在"倒"的阶段，把脑子里的东西全倒出来，不管格式不管重复。倒完之后按上面这 5 个分类归位就行。不需要一步到位写成完美的文档。你现在对项目的理解已经很深了，简历上的每个点都能展开讲清楚。把上面 5 个分类每个写几个关键句，剩下的时间去练口头表达，比反复改文档有用得多。
### Reactor 架构与网络模型
Reactor 设计模式是什么，事件驱动 + 回调
主从 Reactor 分工
one loop per thread 的含义和好处（不加锁）
非阻塞 IO + epoll LT，为什么用 LT 不用 ET
同步 IO vs 异步 IO（阻塞/非阻塞都是同步，io_uring/proactor 才是异步）
新连接建立流程、消息收发流程、连接关闭流程
其他哪些网络也是这个架构libev, libevent等等

### lambda 替代 std::bind
可读性提升
bind 不支持 move 捕获
bug 故事：跨线程引用捕获导致悬空引用，改为值捕获/move 捕获
sink parameter 用于 runInLoop 热路径

### send 接口重新设计
万能引用统一所有字符串类路径
同线程 string_view 零开销，跨线程 forward 保留值类别
Buffer 跨线程 swap 窃取资源，O(n) → O(1)，陈硕 FIXME
完美转发的具体实现细节（二义性问题、const char* 拦截等）

### 生命周期管理与线程安全
shared_from_this：跨线程投递时保护 TcpConnection 不被析构
weak_ptr + tie：Channel 回调执行时判断 TcpConnection 是否存活
RAII：unique_ptr 管理 Socket/Channel
跨线程场景：主→从分发、worker 线程池回调 send（subReactor 是 IO 线程，耗时计算丢给 worker，worker 完成后 send 回来就跨线程了）
`thread_local` 替代 `__thread`


### 性能测试（Benchmark）
测试方法：pingpong 吞吐量测试，参考陈硕博客
排查过程：extrabuf 的 = {} 初始化拖慢性能、多余的 atomic、日志级别
编译优化 flag 的影响
测试环境：阿里云 2 核（波动大）→ WSL 16 核多线程（稳定）
结论：与原版 muduo 持平，优化点体现在跨线程场景

### RPC, 我弱化一下, 不想再深入了.

## ------------------------------

## 3分钟项目介绍
> 【开头 - 15秒】
>
> 这个项目是基于陈硕 muduo 网络库核心重构的一个 多 Reactor 多线程的TCP网络库，使用 C++17 重写。核心是实现了 Reactor 设计模式和 one loop per thread 的并发模型，支持非阻塞 IO + epoll 多路复用。
>
> 【架构 - 40秒】
>
> 整体架构是一个 mainReactor + 多个 subReactor。mainReactor 运行在主线程，只负责监听新连接；subReactor 运行在子线程，负责已建立连接的 IO 读写。每个 Reactor 本质上是一个 EventLoop，就是一个事件循环，内部持有一个 epoll 实例，不断调用 epoll_wait 等待事件，有事件了就通过 Channel 分发到对应的回调函数去处理。这就是 one loop per thread —— 一个线程只运行一个事件循环，一个连接一旦分配给某个 subReactor，它整个生命周期的读写都只在那一个线程里完成，不需要加锁。
>
> 【流程一：新连接建立 - 50秒】
>
> 具体怎么跑起来的？服务器启动时，mainReactor 创建 Acceptor，Acceptor 封装了监听套接字，开始 listen，并把这个套接字注册到 mainReactor 的 epoll 上。当有客户端连接进来，mainReactor 的 epoll_wait 被触发，通过 Channel 分发，最终调用到 Acceptor 的回调，Acceptor 调用 accept 拿到新连接的 fd。接下来关键的一步：通过轮询算法从线程池里选一个 subReactor，把这个新连接包装成一个 TcpConnection 对象，注册到那个 subReactor 的 epoll 上。从此这条连接的所有事件都由这个 subReactor 处理。
>
> 【流程二：消息收发 - 50秒】
>
> 连接建立后，当对端发数据过来，subReactor 的 epoll_wait 检测到可读事件，通过 Channel 分发，调用 TcpConnection 的 handleRead，handleRead 把数据从 fd 读到 Buffer 里，然后调用用户设置的 onMessage 回调。这里的回调是一层层传下来的：用户在最上层的 EchoServer 里设置 onMessage，传给 TcpServer，TcpServer 再传给每个 TcpConnection。发送也类似，用户调用 TcpConnection 的 send，如果数据一次发不完，就注册可写事件，等 epoll 通知可写了再继续发。整个过程都是事件驱动、非阻塞的。
>
> 【结尾 - 15秒】
>
> 整个框架大概十几个核心类，通过回调机制把用户的业务代码和底层网络 IO 解耦。这是项目的整体架构和运行流程。（停顿，等面试官追问）

One Loop Per Thread 不是说"对象只能被一个线程知道"，而是说"对象只能在它所属的线程中被操作".
例如某个socket/channel/TcpConnection要close关闭了, 不是在子Reactor中直接关闭, 而是要绕一圈到主Reactor再到子Reactor. 然后主Reactor(即TcpServer)里面又保存了所有的connection/socket/channel的信息.
AI说TcpServer 只是"持有引用"，不是"操作对象". TcpConnection 的操作权属于子线程.

严格来说，epoll_ctl 注册的是 fd + 你关注的事件掩码（EPOLLIN、EPOLLOUT 等），它们是打包在 struct epoll_event 里一起传给内核的。一个 fd 在一个 epoll 实例里只能注册一次，但可以通过 EPOLL_CTL_MOD 修改它关注的事件。
所以"把 fd 注册到 epoll"是业界通用的简称，不算错，但确实省略了事件部分。而 muduo 里 Channel 就是干这个的——它持有 fd + events，调用 enableReading() 就是设置 EPOLLIN 然后调 epoll_ctl 注册/修改。
不过简历上不需要精确到这个程度。说"循环监听已注册的 Channel"或者"监听各 Channel 上的读写事件"就够了，Channel 本身就隐含了 fd + 事件 + 回调这三者的绑定。

什么该写，什么留着说：
写进简历	                    留给口头/README
lambda 替代 bind	            sink parameter 惯用法
send 万能引用统一接口	        完美转发的具体实现细节
Buffer swap O(1) 优化	        陈硕 FIXME 的具体上下文
RAII + 智能指针	                shared_from_this + tie 机制
                                inline 变量 / ODR
                                thread_local / scoped_lock / 结构化绑定
左边的每一项都能让面试官追问出右边那些细节。简历上写 "Buffer swap O(1)"，面试官一定会问"原来是怎么做的？为什么是 O(n)？"，你就可以展开讲陈硕的 FIXME、retrieveAllAsString 的拷贝问题、你的 swap 方案。
scoped_lock、enum class、结构化绑定这些不要写——它们是语法糖级别的改动，面试官不会因为你用了 auto [a, b] = ... 觉得你厉害。inline 变量你喜欢讲 ODR 那就留着当口头话题，但不值得占简历的字。


口头版（面试官问"lambda 替代 bind 有什么好处"时用）：
> 可读性是一方面，但更关键的是 bind 不支持 move 语义。我项目里跨线程发送消息时，要把数据通过 runInLoop 投递到目标线程的任务队列。如果用 bind，绑定的参数只能拷贝进去；换成 lambda 之后，我可以用 move 捕获，比如 [msg = std::move(msg)]，这样整条路径——从 send 构造 string，到 move 进 lambda，到 move 进任务队列——全程不产生额外拷贝。这在每条跨线程消息都要走的热路径上是有意义的。
>
> 我一开始也想把构造函数里的 string 参数都改成 sink parameter 值传递 + move，后来分析发现那些 name 字符串都很短，在 SSO 范围内拷贝就是 memcpy 几个字节，而且只在构造时调用一次，收益几乎为零，就去掉了。优化要看场景，不是所有地方都值得改。
最后那句"优化要看场景"是加分项——面试官喜欢听到你知道什么不该优化。


> 原版 muduo 的 send 有多个重载——StringPiece、Buffer\*、const void\*，接口比较碎。我用一个万能引用模板统一了所有字符串类的 send：传 string 左值、右值、const char\* 都走同一个入口。同线程时直接构造 string\view 指向原数据，零开销；跨线程时用 std::forward 做完美转发，右值 string 走 move，左值走拷贝，这是保证线程安全的最小代价。两参数的 send(void*, len) 也构造成 string\_view 转到模板里。Buffer 没法统一到字符串模板，但我用 swap 把 buffer 内容偷走——交换三个字段就是 O(1)，原版陈硕那里标了 FIXME 没解决，因为 bind 不支持 move 语义。


不需要再加了。原因：
shared_from_this：已经被你那句"RAII 和智能指针管理连接生命周期"覆盖了，它就是这句话的具体实现之一。面试官问"怎么管理的"时你再说"跨线程投递时用 shared_from_this 延长 TcpConnection 生命周期"。
weak_ptr tie：同上，也属于"智能指针管理连接生命周期"的细节。口头说"Channel 回调执行时用 weak_ptr::lock 判断 TcpConnection 是否还活着，防止访问已析构对象"。
thread_local 替代 \\_thread：太小了，就是把 GCC 扩展换成标准关键字，没有设计层面的东西可讲。


面试官问 Zookeeper 底层怎么答：
诚实说你用的是它的服务注册发现能力，然后把你知道的说完，再拉回 muduo：
> "Zookeeper 在我项目里主要用作服务注册中心。它底层是一个树状的层级命名空间，每个节点叫 znode。我用的是临时节点——服务上线时创建，session 断开后节点自动删除，这样就实现了服务下线感知。至于它的一致性协议是 ZAB（类似 Raft），这块我没有深入研究。我这个项目的重心主要在网络库的 Reactor 架构和 send 的优化上。"

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

## 性能测试
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




## 什么是paxos

### 分布式环境

介绍了二阶段提交协议, 我之前并不理解二阶段提交协议, 从文章介绍来看二阶段提交可以解决信息乱序丢失但无法解决节点可能当机的问题.

## Paxos是用来干什么的

### 有序的确定多个值

我们保证一台机器任一时刻只能有一个实例在工作, 这时候 Proposer 往该机器的写请求都会被当前工作的实例受理. 按我理解这里的实例是指 multi paxos 中一个 paxos 实例, 也即任何时刻只有一个 paxos 实例在运行. paxos made simple 中提过同时运行多个 paxos 实例, 不过微信团队提过同时这种情况并没有多大的工程意义, 所以在 phxpaxos 就未实现并行运行多个实例. 但是我倒觉得应该有点工程意义吧, 今后可以实现一下并做个 benchmark.

## 工程化

### 我们需要一个Leader

按我理解, 这里的 leader 与 phxpaxos master 是两个概念. master 严格要求任何情况下只能有一个. 而 leader 则没有这么严格. 一般情况下由 master 来充当 leader.

### 我们需要状态机记录下来输入过的最大实例编号

按我理解, Statemachine 有一个专门的线程 T 负责执行, T 与 paxos 之间通过 queue 通信, 当 paxos 确定一个值之后就把 value 扔给 queue 中, T 按序从 queue 中取出值并交给 Statemachine 来执行. 也就是说 paxos 确定值并且把值写入之后就可以销毁当前实例开启下一个实例. 注意这里 paxos 确定值意味着 paxos 已经把值写入到自己的 paxoslog 中了. 所以原文 "我们必须确保已经输入到状态机并且进行了状态转移，之后我们才能开启新的实例。" 应该没必要吧.

对状态机的要求, 状态机必须严格的记得自己输入过的最大实例编号. ~~按我理解这时 statemachine.execute 模型如下~~:

```go
func execute(value) {
    apply value; // 1
    update instance id; // 2
}
```

~~如果 statemachine 在实现时能原子执行 1, 2 最好; 如果不能那么可能出现在执行 1 之后, 执行 2 之前系统宕机, 所以 statemachine execute 要确保针对同一个 value 多次执行 apply 的效果与执行一次无异.~~ 根据原文意思, 这里记得自己输入过的最大实例编号应该只是用来在 StateMachine 重启时避免从 0 开始重放 paxos log. 所以相关细节参考[状态机Checkpoint详解][20180203175532], 总之 StateMachine 没必要实时严格记得自己输入过的最大实例编号.

## 生产级的paxos库

### RTT与写盘次数的优化

一个 rtt 可以理解, 毕竟在 multi paxos 中 prepare 请求被省略, 仅需要一次 acceptor. 但是一个机器一次写盘就不理解了, 按我理解, 首先在一个 multi paxos 中, next instance id 是要写盘的; 同时根据 paxos made simple, acceptor 在每次响应之前也需要写盘; 那么 phxpaxos 是如何做到一个机器仅需要一次写盘的呢?

-   Q: 一次写盘是如何优化到的.

朴素 paxos 的两个RTT，以及每台机器的三次写盘. 参考 paxos made simple 2.5 节与两阶段介绍; 两个 RTT 是指 prepare 阶段需要的一次 rtt 以及 accept 阶段需要的一次 rtt. 每台机器三次写盘是由于一个机器上有 proposer, acceptor 两个角色, 在 prepare 阶段 proposer 需要一次写盘, acceptor 需要一次写盘; 在 accept 阶段, acceptor 需要一次写盘; 所以一个机器三次写盘.


### 同时运行多个paxos group

由于我们实例运行的方式是确保 i 实例的销毁才能运行 i + 1 实例, 那么这个请求的执行明显是一个串行的过程, 这样对cpu的利用是比较低的, 我们得想办法将cpu利用率提升上来. 这里提高 cpu 利用率的方式并不是实现在一个 multi paxos 中并行运行多个 paxos instance; 而是并行运行多个 multi paxos, 然后一个 multi paxos 中一次只运行一个 paxos instance. 参见原文举得例子.

实现了基于一个network i/o搭配多组paxos group的结构; 参见原文.

这里多个 Paxos Group 共享着同一个 IP/Port, 那么在节点之间交流时势必需要带上 groupidx, 也即节点之前要互相理解对方的 group idx. 假设存在场景: 三个节点 A, B, C 运行着 storage v1.0 代码; 现在对 storage 进行了一些改动, 代码中新增了一些 paxos group 等, 此时记为 storage v1.1; 那么如何在 A, B, C 中更新代码呢? 如果先更新一个节点比如 C, 那么当 C 上线了 storage v1.1 之后, A, B 对于 C 发来的仅在 storage v1.1 中存在的 group idx 该如何处理? 忽略么? 话说回来倒是可以忽略哈.

### 更快的对齐数据

参考原文; 按我理解可以把 multi paxos 中 paxos instance id 类比为 tcp 协议中的序号概念, 借鉴一下 tcp 对流式传输的一些思想.

### Checkpoint

为啥不叫 snapshot? 而叫 checkpiont==

>   如何去生成Checkpoint，一个状态机能在不停写的情况下生成一个镜像数据么？答案是不确定的，看你要实现的状态机是什么，有的或许可以并很容易，有的可以但很难，有得可能根本无法实现。那这个问题又抛回给paxos库了，我要想办法去给他生成一个镜像数据，并且由我控制。

这个与 google 做法不太一样, paxos made live 中认为 snapshot 完全是由 application 控制的, snapshot 的细节完全是 application-specific 的. application 与 paxos 框架之间是通过 snapshot handle 来进行通信.


## 正确性保证

参考原文.


## 参考

-   [微信自研生产级paxos类库PhxPaxos实现原理介绍][20180203175424]


[20180203175424]: <https://mp.weixin.qq.com/s?__biz=MzI4NDMyNTU2Mw==&mid=2247483695&idx=1&sn=91ea422913fc62579e020e941d1d059e#rd> "2016-06-22"

[20180203175532]: <https://github.com/pp-qq/phxpaxos/blob/fed2facb98f356c45fb8dc3e1c640768d332a39f/doc/%E7%8A%B6%E6%80%81%E6%9C%BACheckpoint%E8%AF%A6%E8%A7%A3.md>

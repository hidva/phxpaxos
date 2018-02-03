
暂定对 PhxPaxos 的学习顺序如下:

1.  学习 README.zh_CN.md.
2.  学习 include 目录下内容, 学习顺序参考公共头文件介绍. 同时参考 github wiki, doc/ 等文档.
3.  学习 sample 下的例子. 可以参考 README.zh_CN.md
4.  学习源码. 顺序暂定. 参考 `code_reading_note.pdf`, 这里面存放着一些源码实现细节.


## README.zh_CN.md

参见原文.

-   QA: 基于Paxos算法的集群签名保护，隔离非法签名的错误机器。
-   A: 参见 [如何进行成员变更.md][20180203171101] 介绍.

-   QA: 自适应的过载保护是指啥?
-   A: 参见 `Node::SetProposeWaitTimeThresholdMS()` 等类似 API.

-   Q: 一个PhxPaxos实例任一时刻只允许运行在单一进程（容许多线程）。不懂啥意思. 本来我以为是说一个机器上只能部署一个使用 phxpaxos 的进程; 但是从下面的例子中看到并不是这样的.

### 性能

按我理解这里的测试模型是在 phxpaxos 收到客户端发来的请求之后会随机选择一个 group 来进行处理. 从测试结果中可以看出随着 group 数目提升 qps 也随之提升.


## 参考

-   PhxPaxos, 对应 git commit: 7b07b5a.


[20180203171101]: <https://github.com/pp-qq/phxpaxos/blob/211745c82a91f795fb394cac74c042db21fddcdc/doc/%E5%A6%82%E4%BD%95%E8%BF%9B%E8%A1%8C%E6%88%90%E5%91%98%E5%8F%98%E6%9B%B4.md>




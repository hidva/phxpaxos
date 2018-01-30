
参见原文. 与 Paxos Made Live 中 snapshot 节很是相似; 这里有几个问题:

-   QA: StateMachine 何时生成 checkpoint?
-   A: 与 Paxos Made Live 中介绍地一致, StateMachine 可以在任何时刻生成 checkpoint.

-   Q: 按我理解, '以状态机的Checkpoint来启动PhxPaxos' 流程如下:

    ```
    statemachine.RecoveryFromCheckpoint(checkpoint); // 1, 此时也会移除 sm 上一次启动时遗留的老状态.
    startid = statemachine.GetCheckpointInstanceID(); // 2
    statemachine.Execute(paxoslog[startid, ...));
    ```

    但是原文并没有介绍第 1 步, 而是直接从第 2 步开始了.


-   QA: `Node::SetHoldPaxosLogCount()` 接口存在的意义
-   A: 参见 '自动传输Checkpoint到其他节点' 节; 其实我觉得这个接口没啥意义, Paxos Made Live 中好像也没用.

-   Q: `StateMachine::LoadCheckpointState()` 为啥会自杀? 按我理解难道不是接着 learn paxos log 么.


## 参考

1.  [状态机Checkpoint详解][20180127152258]

[20180127152258]: <https://github.com/Tencent/phxpaxos/wiki/%E7%8A%B6%E6%80%81%E6%9C%BACheckpoint%E8%AF%A6%E8%A7%A3> "version: 265549b"

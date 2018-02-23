
参见原文. 与 Paxos Made Live 中 snapshot 节很是相似; 这里有几个问题:

-   QA: StateMachine 何时生成 checkpoint?
-   A: 与 Paxos Made Live 中介绍地一致, StateMachine 可以在任何时刻生成 checkpoint.

-   QA: 按我理解, '以状态机的Checkpoint来启动PhxPaxos' 流程如下:

    ```
    statemachine.RecoveryFromCheckpoint(checkpoint); // 1, 此时也会移除 sm 上一次启动时遗留的老状态.
    startid = statemachine.GetCheckpointInstanceID(); // 2
    statemachine.Execute(paxoslog[startid, ...));
    ```

    但是原文并没有介绍第 1 步, 而是直接从第 2 步开始了.
-   A: RecoveryFromCheckpoint() 的调用是 State machine 编写者负责的. 如在 phxkv 中, PhxKVSM 的 RecoveryFromCheckpoint() 逻辑是在 PhxKVSM::Init() 中实现的, PhxKVSM::Init() 会在 PhxKV::RunPaxos() 一开始就被调用. 而且现在意识到 RecoveryFromCheckpoint 确确实实应该由 StateMachine 自身来负责, 不应该由 paxos 框架负责, 就像 paxos made live 中所说, StateMachine snapshot 的生成与组织的细节只有 StateMachine 知道, 也即只有 StateMachine 自身知道最新的 snapshot 在哪以及如何恢复.

-   QA: `Node::SetHoldPaxosLogCount()` 接口存在的意义
-   A: 参见 '自动传输Checkpoint到其他节点' 节; 其实我觉得这个接口没啥意义, Paxos Made Live 中好像也没用.

-   QA: 话说回来了, `StateMachine::LoadCheckpointState()` 为啥要设计到自杀啊!
-   A: 首先 phxpaxos 程序的一般模式如下:

    ```go
    func main() {
        // 从本地保存的 checkpoint 中恢复 StateMachine 的状态.
        RecoveryStateMachine()
        RunPaxosAndServe()
    }
    ```

    StateMachine::LoadCheckpointState() 的一般实现是按照自身对 checkpoint 的组织将从其他节点接受的 checkpoint 存放在合适的位置. 在 LoadCheckpointState() 返回之后自杀重启之后就会执行 `func main` 中 RecoveryStateMachine() 的逻辑, 即根据从其他节点接收到的 checkpoint 来恢复自身状态, 合情合理. 只不过自杀这个确实有些粗暴了.

## 参考

1.  [状态机Checkpoint详解][20180127152258]

[20180127152258]: <https://github.com/Tencent/phxpaxos/wiki/%E7%8A%B6%E6%80%81%E6%9C%BACheckpoint%E8%AF%A6%E8%A7%A3> "version: 265549b"


## 关于磁盘的选择

参见原文.

-   Q: PhxPaxos 不是使用了 leveldb 作为存储么, 怎么还会有随机写的问题?

## fdatasync带来的写放大问题

尽量使用 `BatchPropose()` 等批量提交接口来减缓 fdatasync() 写放大带来的问题.

## 关于PhxPaxos在LoadCheckpointState后会进行自杀

参见原文.

## 关于SetHoldPaxosLogCount的设置

状态机Checkpoint详解提到，PaxosLog保留越多，就越少几率触发Checkpoint的同步，在有条件的情况下必然使保留越多越好。其实按我理解, N 条 paxos log 形成 checkpoint(也就是 snapshot) 应该比 N 条 paxos log 的体积更小, 更精简; 比如以一个 KV 存储举例, 三条 paxos log 分别是 SET(key1, value1), DEL(key1), SET(key1, value2), 生成的 checkpoint 可以只包含 SET(key1, value2). 所以使用 checkpoint 来同步效率应该更高. 等等我想到了 checkpoint 表示某一时刻下存储的全量数据, 所以其体积应该远大于几条 paxos log 的, 所以还是使用 paxos log 同步效率更高一点. 但是在 phxpaxos 实现中, 一旦涉及到 checkpoint 同步, 就会涉及到进程自杀, 头疼==.

SetHoldPaxosLogCount() 的实际设置应该根据业务请求数据大小情况，算一下这个值对应的数据有多少，并根据自身的磁盘情况来决定。 按我理解根据业务请求数据估算一条 paxos log 大小, 再根据磁盘情况估算最多可以存放多少 paxos log.

SetHoldPaxosLogCount() 的值对应的产生这么多请求需要的时间，实际就是你能容忍机器离线落后并不进行Checkpoint拉取的时间。按我理解假设每秒生成一条 Paxos Log, 每秒进行一次 checkpoint, HoldPaxosLogCount 的值为 N, nodeB 在时刻 t crash, 在时刻 t1 重启. 那么当 t1 - t < N 时, 重启后的 nodeB 的同步仅依赖 paxos log 即可完成; 当 t1 - t >= N 时, 重启后的 nodeB 的同步就不得不需要使用 checkpoint 了.


## 使用BatchPropose带来的checkpoint更新问题

一般Checkpoint InstanceID(后面简称CPID)的更新，可在状态机Execute执行成功后，则更新到当前执行的InstanceID。按我理解不是仅当生成 checkpoint 时才会更新 CPID 么?! 所以这里的场景应该是每次 Execute() 时都会生成 checkpoint, checkpoint 中存放着 CPID; 在节点重启时, 其会利用其最近一次 checkpoint CP 来恢复, 然后再从 CP.CPID + 1 开始重新应用 paxos log. 此时 Execute() 实现如下:

```go
// 使用编号处于 [start, end] 内的 paxos log 生成 checkpoint, 并将 end 保存在 checkpoint 中作为 CPID.
func GenerateCheckpoint(start, end int) {
}

func Execute(paxlog PaxosLog) bool {
    rst := doExecute(paxlog)
    GenerateCheckpoint(0, paxlog.no)
    return rst;
}
```

但由于BatchPropose使得多个Propose请求获得相同的InstanceID，参见原文描述.

如何解决这个问题，比较简易的做法是，始终更新CPID为当前Execute成功的InstanceID - 1. 按我理解, 这里 Execute() 实现如下:

```go
func Execute(paxlog PaxosLog) bool {
    GenerateCheckpoint(0, paxlog.no - 1)
    return doExecute(paxlog)
}
```

这样可以解决上面提到的 BatchPropose() 问题.

## 更严格的使用Master功能

介绍了一种场景, 写入未在 master 上生效, 导致 master 上的读请求读取到的是老数据. 参见原文. 按我理解假设有三台机器: A, B, C, 首先 A 是 Master, 其收到了客户端发来的写请求, 然后 A 执行原文提出的命令序列, A, B 批准了提议 P, 所以接下来会在 A, B 上执行 StateMachine 的 Execute(P.value) 方法; 只不过在 A 将要执行 Execute() 时, A master 租约到期而且由于某些原因 A 未能及时续约导致 Master 被 C 节点抢去; A(与 B) 节点开始执行 Execute(), 成功写入客户端的写请求并给客户端返回写入成功的响应; 然后客户端开始向 master 节点发起读请求读取其刚写入的数据, 这时候读请求会被 C 处理, 此时 C 可能还未执行 Execute(P.value), 导致 C 将返回老数据.

本来我以为这种情况是因为 master 选举所使用 Group 与处理客户端请求的 Group 不是一个 group 导致. 但即使是同一个 group 也可能会出现这么一个场景: A, B, C, 首先 A 是 Master, 其收到了客户端发来的写请求, 然后 A 执行原文提出的命令序列; 然后在第 i 步之后第 ii 步之前, master 租约到期, A 由于某些原因未能成功续约, 此时 C 节点发起选择自己为 master 的提议, 并且在 paxos instance i 上得到了 A, B, C 批准选择; A 执行第 ii 步发起提议 P, 并在 paxos instance i + 1 上得到 A, B 批准选择, 然后 A, B 上执行 StateMachine 的 Execute(P.value) 方法, A(与 B)成功写入客户端的写请求并给客户端返回写入成功的响应; 然后客户端开始向 master 节点发起读请求读取其刚写入的数据, 这时候读请求会被 C 处理, 此时 C 可能还未执行 Execute(P.value), 导致 C 将返回老数据.

针对这种情况的解决方案, 参见原文. 但是按我理解, 当 master 选举所使用 Group 与处理客户端请求的 Group 不是一个时, 这种方案并不能解决问题吧. 可以构造这么一个场景: 三个节点 A, B, C; 首先 A 是 master; 两个 PhxPaxos Group: grp1, grp2, grp1 负责 master 选举, grp2 负责处理客户端请求; 某一时刻 t, A 收到客户端写请求, 其在 grp2 上发起提议 P, 然后被节点 A, B 批准选择, 此时开始在 A, B 上执行 StateMachine 的 Execute(P.value) 方法; 此时 Execute() 实现大致如下:

```go
func Execute(p P, ctx *SMCtx) bool {
    if p.master_version != getCurrentMasterVersion() {  // #1
        if ctx != nil {
            ctx.err = fmt.Errorf("Master 版本号冲突, 写入失败");
        }
        return true;
    }

    // #2
    return doExecute(p)
}
```

然后 A(与 B) 在 `#1` 通过开始执行 `#2` 时, master 租约到期, 然后由于某些原因, A 未能成功续约, 导致 C 节点在 grp1 上发起自己为 master 的提议并得到 A, B, C 批准选择, C 成为了 master; A(与 B) 成功执行了 `#2`, A 给客户端返回写入成功的响应; 然后客户端开始向 master 节点发起读请求读取其刚写入的数据, 这时候读请求会被 C 处理, 此时 C 可能还未执行 Execute(P.value), 导致 C 将返回老数据. 更极端的情况, 当 master 变更发生在 `#1` 阶段时, 可能会出现 A 节点在执行 `#1` 返回 `false`, 但是 B 节点在执行 `#1` 时返回 `true`.

当 master 选举所使用 Group 与处理客户端请求的 Group 是同一个时, 目测原文提出的解决方案是可以生效的.

-   Q: Execute() 的行为依赖于 getCurrentMasterVersion() 的返回值, 而 getCurrentMasterVersion() 的返回值随着时间的推移可能返回变化. 那么可能存在场景: 节点 A, B, C 都开始执行 Execute(P), 然后 A 在 `#1` 返回 `false` 之后开始执行 `#2` 之前 crash 掉了, B, C 正常执行结束; C 是 master, C 给客户端返回写入成功的响应; 一段时间之后, A 重启并进行 Recovery, 然后 A 又一次会执行 Execute(P), 但此时 `getCurrentMasterVersion() > p.master_version`, 导致 A 执行 Execute() 未写入有效数据. 这可咋整?

## 关于PhxPaxos会产生巨量的日志

参见原文. 有一个 phxpaxos 会产生巨量日志的意识.

## 参考

1.  [PhxPaxos使用经验以及可能遇到的问题][20180201113133]

[20180201113133]: <https://github.com/Tencent/phxpaxos/wiki/PhxPaxos%E4%BD%BF%E7%94%A8%E7%BB%8F%E9%AA%8C%E4%BB%A5%E5%8F%8A%E5%8F%AF%E8%83%BD%E9%81%87%E5%88%B0%E7%9A%84%E9%97%AE%E9%A2%98> "21338cf"



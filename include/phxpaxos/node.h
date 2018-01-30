/*
Tencent is pleased to support the open source community by making
PhxPaxos available.
Copyright (C) 2016 THL A29 Limited, a Tencent company.
All rights reserved.

Licensed under the BSD 3-Clause License (the "License"); you may
not use this file except in compliance with the License. You may
obtain a copy of the License at

https://opensource.org/licenses/BSD-3-Clause

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" basis,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
implied. See the License for the specific language governing
permissions and limitations under the License.

See the AUTHORS file for names of contributors.
*/

#pragma once

#include "phxpaxos/sm.h"
#include "phxpaxos/options.h"
#include <typeinfo>
#include <inttypes.h>
#include <map>
#include <vector>

namespace phxpaxos
{

class NetWork;

/* 所有API无特别说明，均视返回值0为成功，否则异常。更多的返回值定义在def.h.
 *
 * 所有API参数之一iGroupIdx含义均为Paxos Group的Idx。
 *
 * 所有API无特别说明，返回值为Paxos_GroupIdxWrong均为参数iGroupIdx错误。
 *
 * 所有API无特别说明，llInstanceID均指PhxPaxos所确定的全局有序系列中各个提议对应的ID，这个ID为一个从0开始严格有序
 * 连续向上增长的整数。
 */

//All the funciton in class Node is thread safe!

/* phxpaxos group 日常工作流程
 *
 * 通过 BatchPropose(), Propose() 对同一个 group 提交的值会被放入一个有序序列中. 若在 propose() 时指定了
 * SMCtx, 则值在放入序列之后会传递给 group 下与 SMCtx 具有相同 SMID 的状态机并调用状态机的 Execute() 方法; 若
 * propose() 时未指定 SMCtx, 则值不会传递给任何状态机, 也不会执行任何状态机的 Execute() 方法.
 */

class Node
{
public:
    Node() { }
    virtual ~Node() { }

    //If you want to end paxos, just delete poNode.
    //But if you use your own network, poNode can be deleted after your own network stop recieving messages.
    // 按我理解, 这里应该是 poNode **should be** deleted after xxx; 如果在 own network 停止收信息之前
    // delete poNode, 会不会导致 own network 在后续调用 OnReceiveMessage 往 poNode 塞取消息时直接 SIGSEGV?
    static int RunNode(const Options & oOptions, Node *& poNode);

    //Base function.
    virtual int Propose(const int iGroupIdx, const std::string & sValue, uint64_t & llInstanceID) = 0;

    virtual int Propose(const int iGroupIdx, const std::string & sValue, uint64_t & llInstanceID, SMCtx * poSMCtx) = 0;

    /* phxpaxos wiki 原文: 获取当前PhxPaxos正在进行的提议所对应的ID, 也可认作当前节点所见的最大的ID.
     * 但实践表明, 这里是返回在 iGroupIdx 指定的 multi paxos 中, 当前节点所见的最大的 ID 的下一个值.
     */
    virtual const uint64_t GetNowInstanceID(const int iGroupIdx) = 0;

    // 获取当前节点所保留的 Paxos log 的最小 InstanceID, 也就是当前节点可通过 GetInstanceValue 接口读出的
    // Instance 范围的最小值.
    virtual const uint64_t GetMinChosenInstanceID(const int iGroupIdx) = 0;

    virtual const nodeid_t GetMyNodeID() const = 0;

    //Batch propose.

    //Only set options::bUserBatchPropose as true can use this batch API.
    //Warning: BatchProposal will have same llInstanceID returned but different iBatchIndex.
    //Batch values's execute order in StateMachine is certain, the return value iBatchIndex
    //means the execute order index, start from 0.
    virtual int BatchPropose(const int iGroupIdx, const std::string & sValue,
            uint64_t & llInstanceID, uint32_t & iBatchIndex) = 0;

    // 按我理解, 当 BatchPropose() 返回时, sValue 已被 choosen, 所以要并行调用 BatchPropose() 才能起到
    // batch 的效果. 通过 BatchPropose() 提交的值与通过 Propose() 提交的值具有相同的处理方式.
    virtual int BatchPropose(const int iGroupIdx, const std::string & sValue, uint64_t & llInstanceID,
            uint32_t & iBatchIndex, SMCtx * poSMCtx) = 0;

    //PhxPaxos will batch proposal while waiting proposals count reach to BatchCount,
    //or wait time reach to BatchDelayTimeMs.
    virtual void SetBatchCount(const int iGroupIdx, const int iBatchCount) = 0;

    virtual void SetBatchDelayTimeMs(const int iGroupIdx, const int iBatchDelayTimeMs) = 0;

    //State machine.

    //This function will add state machine to all group. 通过 Node::AddStateMachine() 添加的 state
    // machine 与通过配置 Options::vecGroupSMInfoList() 配置的 state machine 有啥区别?
    virtual void AddStateMachine(StateMachine * poSM) = 0;

    virtual void AddStateMachine(const int iGroupIdx, StateMachine * poSM) = 0;

    /* 设置 Propose API的超时时间（注意超时并不意味着该次Propose失败）。
     *
     * QA: 可能存在场景: Propose() 由于这里设置的超时而返回 PaxosTryCommitRet_Timeout, 但是 propose 提议的值
     * 已经被其他 acceptor 批准并且被大多数 acceptor 选择而且已经在其他节点上执行了 SM::Execute().
     * A: 就像上面说的: 超时并不意味着该次Propose失败.
     */
    virtual void SetTimeoutMs(const int iTimeoutMs) = 0;

    //Checkpoint

    //Set the number you want to keep paxoslog's count.
    //We will only delete paxoslog before checkpoint instanceid.
    //If llHoldCount < 300, we will set it to 300. Not suggest too small holdcount.
    virtual void SetHoldPaxosLogCount(const uint64_t llHoldCount) = 0;

    // QA: 不了解 Replayer 这里是干嘛用的, 按我理解 StateMachine 完全可以在 Execute() 中 根据需要来生成
    // checkpoint, 没必要再整个 ExecuteForCheckpoint() 以及 Replayer 了啊==!
    // A: 参见 'Replayer 是什么?' 介绍.
    //Replayer is to help sm make checkpoint.
    //Checkpoint replayer default is paused, if you not use this, ignord this function.
    //If sm use ExecuteForCheckpoint to make checkpoint, you need to run replayer(you can run in any time).

    //Pause checkpoint replayer.
    virtual void PauseCheckpointReplayer() = 0;

    //Continue to run replayer
    virtual void ContinueCheckpointReplayer() = 0;

    //Paxos log cleaner working for deleting paxoslog before checkpoint instanceid.
    //Paxos log cleaner default is pausing.

    //pause paxos log cleaner.
    virtual void PausePaxosLogCleaner() = 0;

    //Continue to run paxos log cleaner.
    virtual void ContinuePaxosLogCleaner() = 0;

    //Membership

    //Show now membership.
    virtual int ShowMembership(const int iGroupIdx, NodeInfoList & vecNodeInfoList) = 0;

    //Add a paxos node to membership.
    virtual int AddMember(const int iGroupIdx, const NodeInfo & oNode) = 0;

    //Remove a paxos node from membership.
    virtual int RemoveMember(const int iGroupIdx, const NodeInfo & oNode) = 0;

    //Change membership by one node to another node.
    // QA: 不明白 ChangeMember() 干嘛用的
    // A: 目测是原子地执行 RemoveMember(), AddMember().
    virtual int ChangeMember(const int iGroupIdx, const NodeInfo & oFromNode, const NodeInfo & oToNode) = 0;

    //Master

    //Check who is master.
    virtual const NodeInfo GetMaster(const int iGroupIdx) = 0;

    //Check who is master and get version.
    virtual const NodeInfo GetMasterWithVersion(const int iGroupIdx, uint64_t & llVersion) = 0;

    //Check is i'm master.
    virtual const bool IsIMMaster(const int iGroupIdx) = 0;

    virtual int SetMasterLease(const int iGroupIdx, const int iLeaseTimeMs) = 0;

    virtual int DropMaster(const int iGroupIdx) = 0;

    //Qos

    //If many threads propose same group, that some threads will be on waiting status.
    //Set max hold threads, and we will reject some propose request to avoid to many threads be holded.
    //Reject propose request will get retcode(PaxosTryCommitRet_TooManyThreadWaiting_Reject), check on def.h.
    /* 设置ProposeAPI最多挂起的线程。对于每个Group的Propose在内部是通过线程锁来进行串行化的，通过设置这个值，使得
     * 在过载的情况下不会挂死太多线程。
     */
    virtual void SetMaxHoldThreads(const int iGroupIdx, const int iMaxHoldThreads) = 0;

    /* 设置一个线程挂起等待时间的阈值，系统会根据这个阈值进行ProposeAPI的自适应过载保护，当被系统判定需要进行过载保
     * 护时，会随机的进行一些Propose请求的直接快速拒绝，使得这些请求不会进入线程等待。
     * 按我理解假设设置处于未被 choosen 的 value 的数量最多为 7. 那么当当前未被 choosen 的 value 的数量已经为
     * 7 时, 后续的 Propose() 可能会直接 Reject.
     *
     * QA: 假设我这里配置为 3 秒, 那么 phxpaxos 是如何判断是否需要进行过载保护的呢?
     * A: 按我理解, 这里配置为 3 秒, 表明 Propose() 阻塞 3 秒是可被接受的; 那么在 Propose() 时, 当 phxpaxos
     * 觉得如果允许 value 提交可能会使 Propose() 超过 3 秒才能返回时, phxpaxos 可能就会直接拒绝 value. 再细节
     * 的话, phxpaxos 可以根据历史数据计算出来一个 value 从提交到被选择所花费的平均时间 t ms, 进而得到未被
     * choosen 的 value 的最大数目: iWaitTimeThresholdMS / t, 进而按照上面我所理解的做法来处理. t 可以时时/
     * 采样周期性更新.
     */
    //To avoid threads be holded too long time, we use this threshold to reject some propose to control thread's wait time.
    virtual void SetProposeWaitTimeThresholdMS(const int iGroupIdx, const int iWaitTimeThresholdMS) = 0;

    //write disk
    /* 这个和 Options::bSync 有啥区别么? **动态**设置PhxPaxos写磁盘是否要附加fdatasync，非常有风险的接口，建议
     * 由高端开发者谨慎使用。
     */
    virtual void SetLogSync(const int iGroupIdx, const bool bLogSync) = 0;

    //Not suggest to use this function
    //pair: value,smid.
    //Because of BatchPropose, a InstanceID maybe include multi-value.
    // 从 GetMinChosenInstanceID() 可知, 当 instance id 对应的 paxos log 被删除时, 将无法
    // GetInstanceValue().
    // QA: smid 是从哪里获取到的?
    // A: 参见 'phxpaxos group 日常工作流程' 节, 这里 smid 是 value 在 propose() 时 SMCtx 中存放的 smid;
    // 若在 propose() 时未指定 SMCtx, 则其值不确定, 实践发现值为 0, 同时 StateMachine::SMID() 也允许返回 0.
    // 怪哉.
    /* 通过llInstanceID获取对应的提议的值。仅当将PhxPaxos当作纯粹的有序队列(参见不带 SMCtx 参数的 Propose)来使
     * 用才会调用这个API.
     */
    virtual int GetInstanceValue(const int iGroupIdx, const uint64_t llInstanceID,
            std::vector<std::pair<std::string, int> > & vecValues) = 0;

protected:
    friend class NetWork;

    virtual int OnReceiveMessage(const char * pcMessage, const int iMessageLen) = 0;
};

}

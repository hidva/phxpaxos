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

#include <string>
#include <vector>
#include <typeinfo>
#include <stdint.h>
#include <inttypes.h>

namespace phxpaxos
{


/* Paxos 如何返回响应?
 *
 * 在之前学习 paxos made simple/live 时就有一个疑问就是 Paxos 如何返回响应? 根据 paxos made live 可知,
 * 无论是写请求还是读请求, 都需要提交给 multi paxos 来确定请求在请求序列中的顺序(当然 paxos made live 也指
 * 明当引入 master 节点之后, 读写请求都由 master 来处理, 这样对于读请求而言, 就可以不经过一次 paxos 来确定其
 * 在请求序列中的顺序了). 对于写请求而言, 如果待写入内容被 paxos 选择了, 那么就可以给 client 返回写成功响应;
 * 如果待写入内容未被 paxos 选择, 那么可以给 client 返回写失败响应. 对于读请求而言, 如何给 client 返回响应呢?
 *
 * 根据 paxos made simple/live 可知, statemachine 的 executor 方法是在 learner 中调用的. 而 client 的
 * 请求则是提交给 proposer 的. 所以 learner 可以把 statemachine exector 结果通过某种方式告知 proposer,
 * proposer 就可以将其作为响应内容返回给 client 了.
 *
 * phxpaxos 使用了 SMCtx 作为 learner 与 proposer 之间的通信介质. 对于写请求而言, 可以通过不带 poSMCtx
 * 参数的 Propose() 方法提交待写入内容, 完事之后通过 Propose() 返回结果来获取待写入内容有没有被选择, 并依次
 * 作为响应返回给 client. 此时在调用 StateMachine::Executor() 时, poSMCtx 取值为 nullptr, Executor()
 * 此时只需要写入内容即可. 对于读请求而言, 其应该先构造一个 SMCtx 实例 p, 然后调用带 poSMCtx 参数的
 * Propose() 来提交; 此时在调用 StateMachine::Executor() 时, poSMCtx 取值为 &p, Executor() 应该负责
 * 填充 p; 当 Propose() 返回时, p 已经被填充, 此时可以提取出 p 的内容并将其作为响应返回给 client. 可以参考
 * sample/phxecho 来加深理解.
 */
class SMCtx
{
public:
    SMCtx();
    SMCtx(const int iSMID, void * pCtx);

    int m_iSMID;
    void * m_pCtx;
};

//////////////////////////////////////////////

class CheckpointFileInfo
{
public:
    std::string m_sFilePath;
    size_t m_llFileSize;
};

typedef std::vector<CheckpointFileInfo> CheckpointFileInfoList;

/////////////////////////////////////////////

const uint64_t NoCheckpoint = (uint64_t)-1;

/* StateMachine, 参见 paxos made live 中介绍的 paxos 工作模式.
 */
class StateMachine
{
public:
    virtual ~StateMachine() {}

    //Different state machine return different SMID().
    /* 参见 'Paxos 如何返回响应?', SMCtx 仅会交付给具有相同 SMID 的 StateMachine 的 Execute() 方法.
     * 理论上挂载到同一个 Group 下的多个 SM 应该具有互不相同的 smid.
     */
    virtual const int SMID() const = 0;

    //Return true means execute success.
    //This 'success' means this execute don't need to retry.
    //Sometimes you will have some logical failure in your execute logic,
    //and this failure will definite occur on all node, that means this failure is acceptable,
    //for this case, return true is the best choice.
    //Some system failure will let different node's execute result inconsistent,
    //for this case, you must return false to retry this execute to avoid this system failure.
    //
    // Q: 如果因为磁盘满等一下原因返回了 false, 这时重试有用吗?
    virtual bool Execute(const int iGroupIdx, const uint64_t llInstanceID,
            const std::string & sPaxosValue, SMCtx * poSMCtx) = 0;

    // QA: 不知道其调用场景.
    // A: 参见 'Replayer 是什么?' 节.
    virtual bool ExecuteForCheckpoint(const int iGroupIdx, const uint64_t llInstanceID,
            const std::string & sPaxosValue);

    // 按我理解, phxpaxos 在调用 LockCheckpointState() 之后, 会调用 GetCheckpointInstanceID() 以及
    // GetCheckpointState(), 并把拿到的信息交给其他节点, 所以 LoadCheckpointState() 中的
    // llCheckpointInstanceID 就对应着 GetCheckpointInstanceID(), vecFileList 对应着
    // GetCheckpointState().

    //Only need to implement this function while you have checkpoint.
    //Return your checkpoint's max executed instanceid.
    //Notice PhxPaxos will call this function very frequently.
    // 目测是可以返回 NoCheckpoint.
    virtual const uint64_t GetCheckpointInstanceID(const int iGroupIdx) const;

    //After called this function, the vecFileList that GetCheckpointState return's, can't be delelted, moved and modifyed.
    virtual int LockCheckpointState();

    //sDirpath is checkpoint data root dir path.
    //vecFileList is the relative path of the sDirPath.
    virtual int GetCheckpointState(const int iGroupIdx, std::string & sDirPath,
            std::vector<std::string> & vecFileList);

    virtual void UnLockCheckpointState();

    //Checkpoint file was on dir(sCheckpointTmpFileDirPath).
    //vecFileList is all the file in dir(sCheckpointTmpFileDirPath).
    //vecFileList filepath is absolute path.
    //After called this fuction, paxoslib will kill the processor.
    //State machine need to understand this when restart.
    virtual int LoadCheckpointState(const int iGroupIdx, const std::string & sCheckpointTmpFileDirPath,
            const std::vector<std::string> & vecFileList, const uint64_t llCheckpointInstanceID);

    // Q: 不懂 BeforePropose(), NeedCallBeforePropose() 的使用场景在哪? 所以语义也不是能看懂.
    //You can modify your request at this moment.
    //At this moment, the state machine data will be up to date.
    //If request is batch, propose requests for multiple identical state machines will only call this function once.
    //Ensure that the execute function correctly recognizes the modified request.
    //Since this function is not always called, the execute function must handle the unmodified request correctly.
    virtual void BeforePropose(const int iGroupIdx, std::string & sValue);

    //Because function BeforePropose much waste cpu,
    //Only NeedCallBeforePropose return true then will call function BeforePropose.
    //You can use this function to control call frequency.
    //Default is false.
    virtual const bool NeedCallBeforePropose();
};

}

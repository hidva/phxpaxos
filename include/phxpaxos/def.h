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

namespace phxpaxos
{

#define SYSTEM_V_SMID 100000000
#define MASTER_V_SMID 100000001
#define BATCH_PROPOSE_SMID 100000002

enum PaxosTryCommitRet
{
    PaxosTryCommitRet_OK = 0,
    PaxosTryCommitRet_Reject = -2,
    // 提议与其他节点同时发起的提议冲突，导致没有被chosen，建议对提议重新发起Propose.
    PaxosTryCommitRet_Conflict = 14,
    // 状态机转移函数失败。
    PaxosTryCommitRet_ExecuteFail = 15,
    // 该节点被设置为follower模式，不允许Propose.
    PaxosTryCommitRet_Follower_Cannot_Commit = 16,
    // 该节点已被集群剔除，不允许Propose.
    PaxosTryCommitRet_Im_Not_In_Membership  = 17,
    // 提议的值超过大小限制。
    PaxosTryCommitRet_Value_Size_TooLarge = 18,
    // 超时。
    PaxosTryCommitRet_Timeout = 404,
    // 超过设置的最大等待线程数，直接拒绝Propose.
    PaxosTryCommitRet_TooManyThreadWaiting_Reject = 405,
};

enum PaxosNodeFunctionRet
{
    Paxos_SystemError = -1,
    Paxos_GroupIdxWrong = -5,
    // 修改成员的过程中节点GID改变了(GID为集群初始化随机生成的唯一证书)。这错误很罕见，一般是由于数据被人为篡改或其
    // 他拜占庭问题导致。由于不是很了解 membership 实现, 所以这个场景不是很懂.
    Paxos_MembershipOp_GidNotSame = -501,
    // 修改冲突，一般由于修改的过程中成员出现变化了，比如其他节点也同时在发起修改成员的操作。针对此错误码建议开发者重
    // 新获取当前成员列表以评估是否要再进行重试修改。
    Paxos_MembershipOp_VersionConflit = -502,
    // 没开启成员管理的集群禁止进行成员管理相关API的调用。集群初始化默认不开启成员管理，在Options里面设置
    // bUseMembership为true则会开启成员管理功能。
    Paxos_MembershipOp_NoGid = 1001,
    // 尝试添加的成员已存在。
    Paxos_MembershipOp_Add_NodeExist = 1002,
    // 尝试剔除的成员不存在。
    Paxos_MembershipOp_Remove_NodeNotExist = 1003,
    // 该替换操作对集群成员列表并未产生实质变化(被替换节点不存在且替换节点已存在)。
    Paxos_MembershipOp_Change_NoChange = 1004,
    Paxos_GetInstanceValue_Value_NotExist = 1005,
    Paxos_GetInstanceValue_Value_Not_Chosen_Yet = 1006,
};

}


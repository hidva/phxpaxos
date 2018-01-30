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

#include "phxpaxos/def.h"
#include "phxpaxos/sm.h"
#include "phxpaxos/network.h"
#include "phxpaxos/storage.h"
#include "phxpaxos/log.h"
#include <functional>
#include <vector>
#include <typeinfo>
#include <inttypes.h>
#include "breakpoint.h"

namespace phxpaxos
{

class LogStorage;
class NetWork;
class StateMachine;

typedef uint64_t nodeid_t;
static const nodeid_t nullnode = 0;

///////////////////////////////////////////////

/* 按我理解, 本程序中的一些重要概念
 *
 * phxpaoxs, paxos group, group 标识着 paxos made live 中所说的 multi paxos. 一个进程中可以包含多个
 * phxpaxos, 使用 iGroupIdx 来区分.
 *
 * llInstanceID; 既然 phxpaxos 是 multi paxos, 所以其内包含着多个 paxos instance, 每一个 paxos instance
 * 使用 llInstanceID 来区分.
 *
 * StateMachine; 就是 paxos made live 中所说的状态机, phxpaxos 会以同样的顺序将 paxos instance 选择出来的
 * 值传递给 StateMachine.
 *
 * GroupSMInfo; PhxPaxos 库实现上支持一个 paxos group 挂载多个 StateMachine. QA: 此时在提交一个 proposer
 * 之后如何得知该调用哪个 StateMachine?
 * A: 参见 'phxpaxos group 日常工作流程' 节.
 *
 * Checkpoint, 应该是 paxos made live 中 snapshot 的概念.
 */

/* Replayer 是什么?
 *
 * 按我理解, Replayer 就是一个后台线程, 该线程会周期性调用 StateMachine::ExecuteForCheckpoint() 来生成镜像.
 * 也就是在 PhxPaxos 看来, 开发者可以在 ExecuteForCheckpoint() 中实现镜像的生成逻辑. 当开发者在
 * StateMachine::Execute() 中就可以生成 checkpoint 时, 此时其可以不实现 ExecuteForCheckpoint(), 同时可以不
 * 开启 bUseCheckpointReplayer.
 */

/* QA: NodeInfo; 按我理解, node 具有 nodeid, port, ip 三个属性; 那么 NodeInfo(nodeid) 以及
 * NodeInfo(ip, port) 这些函数中如何确定缺少的属性的呢? 本来我想的是 nodeid 8bytes, 前 4bytes 存放着 ip, 后
 * 4bytes 存放着 port; 还有另外一种可能, NodeInfo 就是纯粹的物料类, 属性的确定全靠 set() 调用.
 * A: 实现表明, nodeid 前 4 byte 存放 Ip, 后 4 byte 存放 port. 但是这样就不能支持最近被中央大力推崇的 ipv6 了
 * 啊.
 *
 * Q: nodeid 依赖于 node 的 ip, port. 如果 node 分散在不同的数据中心, 那么会不会导致 node 具有相同的私有 ip
 * 地址, 从而导致两个不同的 node 具有相同的 nodeid.
 *
 * 按我理解, NodeInfo 更应该设计成一经创建便不再变更的只读对象. 这样在之后的 FollowerNodeInfo, NodeInfoList 中
 * 都可以采用指针结构应该可以避免不少的数据拷贝.
 */
class NodeInfo
{
public:
    NodeInfo();
    NodeInfo(const nodeid_t iNodeID);
    NodeInfo(const std::string & sIP, const int iPort);
    virtual ~NodeInfo() { }

    const nodeid_t GetNodeID() const;
    const std::string & GetIP() const;
    const int GetPort() const;

    void SetIPPort(const std::string & sIP, const int iPort);
    void SetNodeID(const nodeid_t iNodeID);

private:
    void MakeNodeID();
    void ParseNodeID();

    nodeid_t m_iNodeID;
    std::string m_sIP;
    int m_iPort;
};

class FollowerNodeInfo
{
public:
    NodeInfo oMyNode;
    NodeInfo oFollowNode;
};

typedef std::vector<NodeInfo> NodeInfoList;
typedef std::vector<FollowerNodeInfo> FollowerNodeInfoList;

/////////////////////////////////////////////////

class GroupSMInfo
{
public:
    GroupSMInfo();

    //required
    //GroupIdx interval is [0, iGroupCount)
    int iGroupIdx;

    //optional
    //One paxos group can mounting multi state machines.
    std::vector<StateMachine *> vecSMList;

    //optional
    //Master election is a internal state machine.
    //Set bIsUseMaster as true to open master election feature.
    //Default is false.
    /* 按我理解, 当 bIsUseMaster 为 true 时表明当前 group 将仅用来 Master 节点的选择. phxpaxos 会将内置实现
     * 的选主状态机追加 vecSMList 中; 选主状态机与 Node::GetMaster() 等接口应该是通过共享内存来通信.
     *
     * Q: bIsUseMaster 为 true 的 group.vecSMList 还能添加其他 sm 么? group 还能用来 proposer 值么?
     *
     * QA: bIsUseMaster 为 true 时, 假设代码部署在三个节点 A, B, C 上, 那么何时会由谁触发第一次选主呢?
     * A: 在 Node.RunNode 时会对每一个 group 有一个逻辑初始化的操作, 对于 bIsUseMaster 为 true 的 group 其
     * 初始化逻辑是触发一次选主操作. 所以这里是 A, B, C 都会竞争 Master 节点, 但最终只有一个节点会被选为 Master.
     * 话说回来, 我觉得 GroupSMInfo 应该新增一个 Init() 函数用来配置该 group 的初始化逻辑.
     *
     * 貌似当 bIsUseMaster 为 true 时, options bOpenChangeValueBeforePropose 必须为 true.
     */
    bool bIsUseMaster;

};

typedef std::vector<GroupSMInfo> GroupSMInfoList;

/////////////////////////////////////////////////

typedef std::function< void(const int, NodeInfoList &) > MembershipChangeCallback;
typedef std::function< void(const int, const NodeInfo &, const uint64_t) > MasterChangeCallback;

/////////////////////////////////////////////////

class Options
{
public:
    Options();

    //optional
    //User-specified paxoslog storage.
    //Default is nullptr.
    LogStorage * poLogStorage;

    //optional
    //If poLogStorage == nullptr, sLogStoragePath is required.
    std::string sLogStoragePath;

    //optional
    //If true, the write will be flushed from the operating system
    //buffer cache before the write is considered complete.
    //If this flag is true, writes will be slower.
    //
    //If this flag is false, and the machine crashes, some recent
    //writes may be lost. Note that if it is just the process that
    //crashes (i.e., the machine does not reboot), no writes will be
    //lost even if sync==false. Because of the data lost, we not guarantee consistence.
    //
    //Default is true.
    bool bSync;

    //optional
    //Default is 0.
    //This means the write will skip flush at most iSyncInterval times.
    //That also means you will lost at most iSyncInterval count's paxos log.
    int iSyncInterval;

    //optional
    //User-specified network.
    NetWork * poNetWork;

    //optional
    //Our default network use udp and tcp combination, a message we use udp or tcp to send decide by a threshold.
    //Message size under iUDPMaxSize we use udp to send.
    //Default is 4096.
    size_t iUDPMaxSize;

    //optional
    //Our default network io thread count.
    //Default is 1.
    int iIOThreadCount;

    //optional
    //We support to run multi phxpaxos on one process.
    //One paxos group here means one independent phxpaxos. Any two phxpaxos(paxos group) only share network, no other.
    //There is no communication between any two paxos group.
    //Default is 1.
    int iGroupCount;

    //required
    //Self node's ip/port.
    NodeInfo oMyNode;

    //required
    //All nodes's ip/port with a paxos set(usually three or five nodes).
    NodeInfoList vecNodeInfoList;

    //optional
    //Only bUseMembership == true, we use option's nodeinfolist to init paxos membership,
    //after that, paxos will remember all nodeinfos, so second time you can run paxos without vecNodeList,
    //and you can only change membership by use function in node.h.
    //
    // 按我理解, 当启用 bUseMembership 时, nodeinfolist 将作为 choosen value 记录在 paxos log 中. 当一个
    // paxos 集群第一次运行时, vecNodeInfoList 将作为初始值被 propose, 被 choosen 然后记录在 paxoslog 中.
    // 后续 paxos 集群中节点的运行将从 paxoslog 中读取 nodeinfolist.
    //
    //Default is false.
    //if bUseMembership == false, that means every time you run paxos will use vecNodeList to build a new membership.
    //when you change membership by a new vecNodeList, we don't guarantee consistence.
    //
    // 按我理解 bUseMembership 为 false 的场景应该是想 zookeeper 那样作为一个集群固定的全局锁之类的服务时.
    //
    //For test, you can set false.
    //But when you use it to real services, remember to set true.
    bool bUseMembership;

    // 按我理解对于一个 multi paxos 而言, membership change, master change, choosen value 之间存在一条严格
    // 的顺序, 所以把 membership change callback, master change callback 作为 StateMachine 的方法, 与
    // StateMachine 在同一个线程中调用是不是更合理点.
    //While membership change, phxpaxos will call this function.
    //Default is nullptr.
    MembershipChangeCallback pMembershipChangeCallback;

    //While master change, phxpaxos will call this function.
    //Default is nullptr.
    MasterChangeCallback pMasterChangeCallback;

    //optional
    //One phxpaxos can mounting multi state machines.
    //This vector include different phxpaxos's state machines list.
    GroupSMInfoList vecGroupSMInfoList;

    //optional
    Breakpoint * poBreakpoint;

    //optional
    //If use this mode, that means you propose large value(maybe large than 5M means large) much more.
    //Large value means long latency, long timeout, this mode will fit it.
    //Default is false
    // 按我理解, phxpaxos 会根据该配置来调整 prepare timeout 等配置项的时间. 那么为啥不进一步的根据 propose
    // value size 来动态调整呢? 还要用户手动配置.
    bool bIsLargeValueMode;

    //optional
    //All followers's ip/port, and follow to node's ip/port.
    //Follower only learn but not participation paxos algorithmic process.
    //Default is empty.
    // 不清楚 Follower 的使用场景?
    FollowerNodeInfoList vecFollowerNodeInfoList;

    //optional
    //Notice, this function must be thread safe!
    //if pLogFunc == nullptr, we will print log to standard ouput.
    LogFunc pLogFunc;

    //optional
    //If you use your own log function, then you control loglevel yourself, ignore this.
    //Check log.h to find 5 level.
    //Default is LogLevel::LogLevel_None, that means print no log.
    LogLevel eLogLevel;

    //optional
    //If you use checkpoint replayer feature, set as true.
    //Default is false;
    // QA: checkpoint replayer 是啥特性?
    // A: 参见 'Replayer 是什么?'.
    bool bUseCheckpointReplayer;

    //optional
    //Only bUseBatchPropose is true can use API BatchPropose in node.h
    //Default is false;
    bool bUseBatchPropose;

    //optional
    //Only bOpenChangeValueBeforePropose is true, that will callback sm's function(BeforePropose).
    //Default is false;
    bool bOpenChangeValueBeforePropose;
};

}

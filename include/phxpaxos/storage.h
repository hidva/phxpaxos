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
#include <typeinfo>
#include <inttypes.h>

namespace phxpaxos
{

/* 所有函数，成功返回0，不成功返回其他。所有Get相关函数，数据不存在返回1。
 */

//Paxoslib need to storage many datas, if you want to storage datas yourself,
//you must implememt all function in class LogStorage, and make sure that observe the writeoptions.

/* WriteOptions; 对于每个写操作函数，WriteOptions传入了写入的要求选项，其中当bSync设置为true的时候，存储必须要保
 * 证这次写操作严格落到磁盘后才能返回。也就是说，当这次写操作成功后，即使发生了机器重启，一样要能读出这个数据，可采用
 * fsync等方法保证此类操作。
 */
class WriteOptions
{
public:
    WriteOptions() : bSync(true) { }
    bool bSync;
};

class LogStorage
{
public:
    virtual ~LogStorage() {}

    virtual const std::string GetLogStorageDirPath(const int iGroupIdx) = 0;

    // 按我理解 Get(), Put() 中的 value 是指在 iGroupIdx 指定的 paxos group 中, llInstanceID 指定的
    // paxos instance 选择的值, 即 paxos log. Del() 则是移除 paxoslog 以此来节省空间.
    virtual int Get(const int iGroupIdx, const uint64_t llInstanceID, std::string & sValue) = 0;

    virtual int Put(const WriteOptions & oWriteOptions, const int iGroupIdx, const uint64_t llInstanceID, const std::string & sValue) = 0;

    virtual int Del(const WriteOptions & oWriteOptions, int iGroupIdx, const uint64_t llInstanceID) = 0;

    // 按我理解是指移除 iGroupIdx 指定的 paxos group 中所有的 paxoslog.
    // wiki 原文: ClearAllLog函数是清除掉所有通过Put写入过的数据，但不包括SystemVariables与MasterVariables。
    // 应该也不会清除 MinChosenInstanceID 吧.
    virtual int ClearAllLog(const int iGroupIdx) = 0;

    // 按我理解, 下一次在 iGroupIdx 指定的 paxos group 进行 proposer 时使用的 instanceid = llInstanceID+1.
    // QA: 为啥没有 SetMaxInstanceID() 呢? 是不是每次 GetMaxInstanceID() 时都会更新存放在磁盘上的 max
    // instance id? 那是不是意味着每一次调用 Node::GetNowInstanceID() 都会返回不同的值?
    // A: 目测这个数据是根据 paxos log 来计算出来的; 即 iGroupIdx 指定的 multi paxos 中, 当前存储的 paxos
    // log 中最大的 Instance id 值.
    virtual int GetMaxInstanceID(const int iGroupIdx, uint64_t & llInstanceID) = 0;

    // QA: MinChosenInstanceID 是个什么东西?
    // A: 参见 node GetMinChosenInstanceID().
    virtual int SetMinChosenInstanceID(const WriteOptions & oWriteOptions, const int iGroupIdx, const uint64_t llMinInstanceID) = 0;

    virtual int GetMinChosenInstanceID(const int iGroupIdx, uint64_t & llMinInstanceID) = 0;

    // QA: SystemVariables, MasterVariables 不懂是个什么东西
    // A: SystemVariables 存放着 SystemVSM 的 checkpoint. SystemVSM, phxpaxos 使用 SystemVSM 用来在节
    // 点之间同步 system info, 如: membership 等信息; SystemVSM 每次 Execute() 时都会更新
    // SystemVariables.
    //
    // MasterVariables 是 MasterStateMachine(下简称 MasterSM) 的 checkpoint. MasterSM 用来选主的 sm.
    // MasterSM 会在每次 Execute() 时更新 MasterVariables.
    virtual int SetSystemVariables(const WriteOptions & oWriteOptions, const int iGroupIdx, const std::string & sBuffer) = 0;

    virtual int GetSystemVariables(const int iGroupIdx, std::string & sBuffer) = 0;

    virtual int SetMasterVariables(const WriteOptions & oWriteOptions, const int iGroupIdx, const std::string & sBuffer) = 0;

    virtual int GetMasterVariables(const int iGroupIdx, std::string & sBuffer) = 0;
};

}

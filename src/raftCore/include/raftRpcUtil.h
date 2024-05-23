/*
 * @Author: rubo
 * @Date: 2024-05-08 14:38:59
 * @LastEditors: HUAWEI-Ubuntu ruluy0205@163.com
 * @LastEditTime: 2024-05-16 10:28:10
 * @FilePath: /myRaft/src/raftCore/include/raftRpcUtil.h
 * @Description: 
 */

#ifndef RAFTRPC_H
#define RAFTRPC_H

#include "raftRPC.pb.h"

/// @brief 维护当前节点对其他某一个结点的所有rpc发送通信的功能
// 对于一个raft节点来说，与任意其他的节点都要维护一个rpc连接，即 MprpcChannel
class RaftRpcUtil
{
private:
    raftRpcProctoc::raftRpc_Stub *stub_;

public:
    // 主动调用其他节点的三个方法,可以按照mit6824来调用，但是别的节点调用自己的好像就不行了，要继承protoc提供的service类才行
    bool AppendEntries(raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *response);
    bool InstallSnapshot(raftRpcProctoc::InstallSnapshotRequest *args, raftRpcProctoc::InstallSnapshotResponse *response);
    bool RequestVote(raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *response);
    // 响应其他节点的方法
    /**
     * 构造函数
     * @param ip  远端ip
     * @param port  远端端口
     */
    RaftRpcUtil(std::string ip, short port);
    ~RaftRpcUtil();
};

#endif // RAFTRPC_H

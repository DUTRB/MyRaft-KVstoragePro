/*
 * @Author: rubo
 * @Date: 2024-05-08 14:38:59
 * @LastEditors: HUAWEI-Ubuntu ruluy0205@163.com
 * @LastEditTime: 2024-05-09 10:34:21
 * @FilePath: /myRaft/src/raftCore/raftRpcUtil.cpp
 * @Description: 实现 raftrpcPro/raftRPC中定义的方法
 */

#include "raftRpcUtil.h"
#include <mprpcchannel.h>
#include <mprpccontroller.h>

bool RaftRpcUtil::AppendEntries(raftRpcProctoc::AppendEntriesArgs *args,
                                raftRpcProctoc::AppendEntriesReply *response) {
  MprpcController controller;
  // 通过RPC存根与服务器进行通信
  stub_->AppendEntries(&controller, args, response, nullptr);
  return !controller.Failed();
}

bool RaftRpcUtil::InstallSnapshot(
    raftRpcProctoc::InstallSnapshotRequest *args,
    raftRpcProctoc::InstallSnapshotResponse *response) {
  MprpcController controller;
  // 通过RPC存根与服务器进行通信
  stub_->InstallSnapshot(&controller, args, response, nullptr);
  return !controller.Failed();
}

bool RaftRpcUtil::RequestVote(raftRpcProctoc::RequestVoteArgs *args,
                              raftRpcProctoc::RequestVoteReply *response) {
  MprpcController controller;
  // 通过RPC存根与服务器进行通信
  stub_->RequestVote(&controller, args, response, nullptr);
  return !controller.Failed();
}

// 先开启服务器，再尝试连接其他的节点，中间给一个间隔时间，等待其他的rpc服务器节点启动

RaftRpcUtil::RaftRpcUtil(std::string ip, short port) {
  // 指向raftRpcProctoc::raftRpc_Stub的指针，它是一个RPC存根，用于处理RPC请求和响应。
  // RPC存根是客户端用来与服务器端进行通信的本地代理对象
  stub_ = new raftRpcProctoc::raftRpc_Stub(new MprpcChannel(ip, port, true));
}

RaftRpcUtil::~RaftRpcUtil() { delete stub_; }

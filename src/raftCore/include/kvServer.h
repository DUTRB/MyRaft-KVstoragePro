#ifndef SKIP_LIST_ON_RAFT_KVSERVER_H
#define SKIP_LIST_ON_RAFT_KVSERVER_H

#include "kvServerRPC.pb.h"
#include "raft.h"
#include "skipList.h"
#include <boost/any.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/foreach.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>
#include <iostream>
#include <mutex>
#include <unordered_map>

class KvServer : raftKVRpcProctoc::kvServerRpc {
private:
  std::mutex m_mtx;
  int m_me;
  std::shared_ptr<Raft> m_raftNode;
  std::shared_ptr<LockQueue<ApplyMsg>> applyChan;     // kvServer和raft节点的通信管道
  int m_maxRaftState; // snapshot if log grows this big

  // todo ：序列化后的kv数据，理论上可以不用，但是目前没有找到特别好的替代方法
  std::string m_serializedKVData;
  // 跳表
  SkipList<std::string, std::string> m_skipList;
  std::unordered_map<std::string, std::string> m_kvDB;

  // waitApplyCh是一个map，键是int，值是Op类型的管道
  std::unordered_map<int, LockQueue<Op> *> waitApplyCh;
  // clientid -> requestID  一个kV服务器可能连接多个client
  std::unordered_map<std::string, int>m_lastRequestId;

  // last SnapShot point , raftIndex
  int m_lastSnapShotRaftLogIndex;

public:
  KvServer() = delete;

  KvServer(int me, int maxraftstate, std::string nodeInforFileName, short port);

  void StartKVServer();

  void DprintfKVDB();

  void ExecuteAppendOpOnKVDB(Op op);

  void ExecuteGetOpOnKVDB(Op op, std::string *value, bool *exist);

  void ExecutePutOpOnKVDB(Op op);

  void Get(const raftKVRpcProctoc::GetArgs *args,
      raftKVRpcProctoc::GetReply *reply);
  // 将 GetArgs 改为rpc调用，因为是远程客户端，即服务器宕机对客户端来说是无感的

  //从raft节点中获取消息  （不要误以为是执行GET命令）
  void GetCommandFromRaft(ApplyMsg message);

  bool ifRequestDuplicate(std::string ClientId, int RequestId);

  // clerk 使用RPC远程调用
  void PutAppend(const raftKVRpcProctoc::PutAppendArgs *args,
                 raftKVRpcProctoc::PutAppendReply *reply);

  // 一直等待raft传来的 applyCh
  void ReadRaftApplyCommandLoop();

  void ReadSnapShotToInstall(std::string snapshot);

  bool SendMessageToWaitChan(const Op &op, int raftIndex);

  // 检查是否需要制作快照，需要的话就向 raft 之下制作快照
  void IfNeedToSendSnapShotCommand(int raftIndex, int proportion);

  void GetSnapShotFromRaft(ApplyMsg message);

  std::string MakeSnapShot();

public: // 使用 kvServerRPC proto文件定义的服务和方法
  void PutAppend(google::protobuf::RpcController *controller,
                 const ::raftKVRpcProctoc::PutAppendArgs *request,
                 ::raftKVRpcProctoc::PutAppendReply *response,
                 ::google::protobuf::Closure *done) override;

  void Get(google::protobuf::RpcController *controller,
           const ::raftKVRpcProctoc::GetArgs *request,
           ::raftKVRpcProctoc::GetReply *response,
           ::google::protobuf::Closure *done) override;

  // 序列化开始
  // notice ： func serialize
private:
  friend class boost::serialization::access;

  // When the class Archive corresponds to an output archive, the
  // & operator is defined similar to <<.  Likewise, when the class Archive
  // is a type of input archive the & operator is defined similar to >>.
  template <class Archive>
  void serialize(Archive &ar, const unsigned int version)
  {
    // 这里面写需要序列话和反序列化的字段
    ar & m_serializedKVData;
    // ar & m_kvDB;
    ar & m_lastRequestId;
  }

  std::string getSnapshotData() {
    m_serializedKVData = m_skipList.dumpFile();
    std::stringstream ss;
    boost::archive::text_oarchive oa(ss);
    oa << *this;
    m_serializedKVData.clear();
    return ss.str();
  }

  void parseFromString(const std::string &str) {
    std::stringstream ss(str);
    boost::archive::text_iarchive ia(ss);
    ia >> *this;
    m_skipList.loadFile(m_serializedKVData);
    m_serializedKVData.clear();
  }
};

#endif // SKIP_LIST_ON_RAFT_KVSERVER_H

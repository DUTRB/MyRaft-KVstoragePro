/*
 * @Author: rubo
 * @Date: 2024-05-08 14:38:59
 * @LastEditors: HUAWEI-Ubuntu ruluy0205@163.com
 * @LastEditTime: 2024-05-08 14:46:13
 * @FilePath: /myRaft/src/raftCore/include/Persister.h
 * @Description: 
 */

#ifndef SKIP_LIST_ON_RAFT_PERSISTER_H
#define SKIP_LIST_ON_RAFT_PERSISTER_H
#include <fstream>
#include <mutex>

/* ---------------------------------- 持久化类 ---------------------------------- */
class Persister
{
private:
    std::mutex m_mtx;
    std::string m_raftState;  // 存储Raft协议的当前状态
    std::string m_snapshot;
    /**
     * m_raftStateFileName: raftState文件名
     */
    const std::string m_raftStateFileName;
    /**
     * m_snapshotFileName: snapshot文件名
     */
    const std::string m_snapshotFileName;
    /**
     * 保存raftState的输出流
     */
    std::ofstream m_raftStateOutStream;
    /**
     * 保存snapshot的输出流
     */
    std::ofstream m_snapshotOutStream;
    /**
     * 保存raftStateSize的大小
     * 避免每次都读取文件来获取具体的大小
     */
    long long m_raftStateSize;

public:
    void Save(std::string raftstate, std::string snapshot);
    std::string ReadSnapshot();
    void SaveRaftState(const std::string &data);
    long long RaftStateSize();
    std::string ReadRaftState();
    explicit Persister(int me);
    ~Persister();

private:
    void clearRaftState();
    void clearSnapshot();
    void clearRaftStateAndSnapshot();
};

#endif // SKIP_LIST_ON_RAFT_PERSISTER_H

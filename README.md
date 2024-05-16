# 基于Raft共识算法的KV存储数据库


## 项目简介
传统的集中式数据库在面对大规模数据库和高并发访问时可能面临单点故障和性能瓶颈的问题。Raft
共识算法实现了对Paxos算法的简化设计和优化。

本项目是基于Raft一致性算法的分布式键值存储数据库，在多个系统上运行时可以确保数据的一致性、可用性和分区容错性。

> 此项目来源于[KVstorageBaseRaft-cpp](https://github.com/youngyangyang04/KVstorageBaseRaft-cpp)

## 项目大纲

![存储结构](./docs/Raft存储系统结构.drawio)
## 项目流程
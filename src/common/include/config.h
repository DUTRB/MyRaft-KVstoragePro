/*
 * @Author: rubo
 * @Date: 2024-05-06 09:39:56
 * @LastEditors: HUAWEI-Ubuntu ruluy0205@163.com
 * @LastEditTime: 2024-05-06 10:44:13
 * @FilePath: /myRaft/src/common/include/config.h
 * @Description: 
 */

#ifndef CONFIG_H
#define CONFIG_H

const int Debug = true;

const int debugMul = 1;     // 时间单位：time.Millisecond，不同网络环境rpc速度不同，因此需要乘以一个系数
const int HeartBeatTimeout = 25 * debugMul; // 心跳时间一般要比选举超时小一个数量级
const int ApplyInterval = 10 * debugMul;

const int minRandomizedElectionTime = 300 * debugMul; // 最小随机选举超时时间
const int maxRandomizedElectionTime = 500 * debugMul;

const int CONSENSUS_TIMEOUT = 500 * debugMul;

// 协程相关设置
const int FIBER_THREAD_NUM = 1;     // 协程库中线程池大小
const int FIBER_USE_CALLER_THREAD = false;  // 是否使用 caller_thread 执行调度任务


#endif
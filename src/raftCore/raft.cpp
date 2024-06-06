#include "raft.h"
#include "config.h"
#include "util.h"
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <memory>
#include <utility>

/**
 * @description:
 * 接收leader发来的日志请求，主要用于检查当前日志是否匹配并同步leader的日志到本机
 *                 实现了Raft协议中Follower节点处理来自Leader节点的AppendEntries
 * RPC请求的逻辑
 * @param {AppendEntriesArgs*} args
 * @param {AppendEntriesReply*} reply
 * @return {*}
 */
void Raft::AppendEntries1(const raftRpcProctoc::AppendEntriesArgs *args,
                          raftRpcProctoc::AppendEntriesReply *reply) {
  std::lock_guard<std::mutex> locker(m_mtx);
  reply->set_appstate(AppNormal); // 能接收到代表网络是正常的

  // 不同的人收到AppendEntries的反应是不同的
  // 此时拒绝请求并返回
  if (args->term() < m_currentTerm) {
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(-100); // 论文中：让领导人可以及时更新自己
    DPrintf("[func-AppendEntries-rf{%d}] 拒绝了 因为Leader{%d}的term{%v}< "
            "rf{%d}.term{%d}\n",
            m_me, args->leaderid(), args->term(), m_me, m_currentTerm);
    return; // 注意从过期的领导人收到消息不要重设定时器
  }
  DEFER {
    persist();
  }; // 由于这个局部变量创建在锁之后，因此执行persist的时候应该也是拿到锁的.

  if (args->term() > m_currentTerm) {
    // 改变节点状态为 follower
    // DPrintf("[func-AppendEntries-rf{%v} ] 变成follower且更新term
    // 因为Leader{%v}的term{%v}> rf{%v}.term{%v}\n", rf.me, args.LeaderId,
    // args.Term, rf.me, rf.currentTerm)
    m_status = Follower;
    m_currentTerm = args->term();
    m_votedFor =
        -1; // 这里设置成-1有意义，如果突然宕机然后上线理论上是可以投票的
            // 这里可不返回，应该改成让改节点尝试接收日志
            // 如果是领导人和candidate突然转到Follower好像也不用其他操作
            // 如果本来就是Follower，那么其term变化，相当于“不言自明”的换了追随的对象，因为原来的leader的term更小，是不会再接收其消息了
  }
  myAssert(args->term() == m_currentTerm,
           format("assert {args.Term == rf.currentTerm} fail"));
  // 如果发生网络分区，那么candidate可能会收到同一个term的leader的消息，要转变为Follower，为了和上面，因此直接写
  m_status =
      Follower; // 这里是有必要的，因为如果candidate收到同一个term的leader的AE，需要变成follower
  // term相等
  m_lastResetElectionTime = now();
  //  DPrintf("[	AppendEntries-func-rf(%v)		]
  //  重置了选举超时定时器\n", rf.me);

  // 不能直接从prevlogIndex开始复制日志，因为rpc可能会延迟，导致发过来的log是很久之前的

  // 比较日志，日志有3种情况
  // 发送了未来的日志，拒绝请求
  if (args->prevlogindex() > getLastLogIndex()) {
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(getLastLogIndex() + 1);
    //  DPrintf("[func-AppendEntries-rf{%v}]
    //  拒绝了节点{%v}，因为日志太新,args.PrevLogIndex{%v} >
    //  lastLogIndex{%v}，返回值：{%v}\n", rf.me, args.LeaderId,
    //  args.PrevLogIndex, rf.getLastLogIndex(), reply)
    return;
  }
  // 如果prevLogIndex小于快照包含的最后索引，也拒绝请求
  else if (args->prevlogindex() < m_lastSnapshotIncludeIndex) {
    // 如果prevlogIndex还没有更上快照
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(m_lastSnapshotIncludeIndex + 1);
    // todo
    // 如果想直接弄到最新好像不对，因为是从后慢慢往前匹配的，这里不匹配说明后面的都不匹配
    //  DPrintf("[func-AppendEntries-rf{%v}]
    //  拒绝了节点{%v}，因为log太老，返回值：{%v}\n", rf.me, args.LeaderId,
    //  reply) return
  }
  // 如果日志匹配
  // 注意：这里目前当args.PrevLogIndex ==
  // rf.lastSnapshotIncludeIndex与不等的时候要分开考虑，可以看看能不能优化这块
  if (matchLog(args->prevlogindex(), args->prevlogterm())) {
    //	todo：	整理logs
    // ，不能直接截断，必须一个一个检查，因为发送来的log可能是之前的，直接截断可能导致“取回”已经在follower日志中的条目
    // 那意思是不是可能会有一段发来的AE中的logs中前半是匹配的，后半是不匹配的，这种应该：1.follower如何处理？
    // 2.如何给leader回复
    // 3. leader如何处理

    for (int i = 0; i < args->entries_size(); i++) {
      auto log = args->entries(i);
      if (log.logindex() > getLastLogIndex()) {
        // 超过就直接添加日志
        m_logs.push_back(log);
      } else {
        // 没超过就比较是否匹配，不匹配再更新，而不是直接截断
        //  todo ：
        //  这里可以改进为比较对应logIndex位置的term是否相等，term相等就代表匹配
        //   todo：这个地方放出来会出问题,按理说index相同，term相同，log也应该相同才对
        //  rf.logs[entry.Index-firstIndex].Term ?= entry.Term

        if (m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm() ==
                log.logterm() &&
            m_logs[getSlicesIndexFromLogIndex(log.logindex())].command() !=
                log.command()) {
          // 相同位置的log
          // ，其logTerm相等，但是命令却不相同，不符合raft的前向匹配，异常了！
          myAssert(
              false,
              format(
                  "[func-AppendEntries-rf{%d}] "
                  "两节点logIndex{%d}和term{%d}相同，但是其command{%d:%d}   "
                  " {%d:%d}却不同！！\n",
                  m_me, log.logindex(), log.logterm(), m_me,
                  m_logs[getSlicesIndexFromLogIndex(log.logindex())].command(),
                  args->leaderid(), log.command()));
        }
        if (m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm() !=
            log.logterm()) {
          // 不匹配就更新
          m_logs[getSlicesIndexFromLogIndex(log.logindex())] = log;
        }
      }
    }

    // 错误写法like：  rf.shrinkLogsToIndex(args.PrevLogIndex)
    // rf.logs = append(rf.logs, args.Entries...)
    // 因为可能会收到过期的log！！！ 因此这里是大于等于
    myAssert(getLastLogIndex() >= args->prevlogindex() + args->entries_size(),
             format("[func-AppendEntries1-rf{%d}]rf.getLastLogIndex(){%d} != "
                    "args.PrevLogIndex{%d}+len(args.Entries){%d}",
                    m_me, getLastLogIndex(), args->prevlogindex(),
                    args->entries_size()));
    // if len(args.Entries) > 0 {
    //	fmt.Printf("[func-AppendEntries  rf:{%v}] ] : args.term:%v, rf.term:%v
    //,rf.logs的长度：%v\n", rf.me, args.Term,
    // rf.currentTerm, len(rf.logs))
    // }
    if (args->leadercommit() > m_commitIndex) {
      m_commitIndex = std::min(args->leadercommit(), getLastLogIndex());
      // 这个地方不能无脑跟上getLastLogIndex()，因为可能存在args->leadercommit()落后于
      // getLastLogIndex()的情况
    }

    // 领导会一次发送完所有的日志
    myAssert(getLastLogIndex() >= m_commitIndex,
             format("[func-AppendEntries1-rf{%d}]  rf.getLastLogIndex{%d} < "
                    "rf.commitIndex{%d}",
                    m_me, getLastLogIndex(), m_commitIndex));
    reply->set_success(true);
    reply->set_term(m_currentTerm);

    //        DPrintf("[func-AppendEntries-rf{%v}]
    //        接收了来自节点{%v}的log，当前lastLogIndex{%v}，返回值：{%v}\n",
    //        rf.me,
    //                args.LeaderId, rf.getLastLogIndex(), reply)

    return;
  }
  // 如果日志不匹配
  else {
    // ? 日志寻找匹配加速
    // 如果日志不匹配的话可以一个一个往前的倒退。但是这样的话可能会设计很多个rpc之后才能找到匹配的日志，那么就一次多倒退几个数。
    // 倒退几个呢？这里认为如果某一个日志不匹配，那么这一个日志所在的term的所有日志大概率都不匹配，那么就倒退到
    // 最后一个日志所在的term的最后那个命令。

    // 优化
    // PrevLogIndex 长度合适，但是不匹配，因此往前寻找 矛盾的term的第一个元素
    // 为什么该term的日志都是矛盾的呢？也不一定都是矛盾的，只是这么优化减少rpc而已
    // ？什么时候term会矛盾呢？很多情况，比如leader接收了日志之后马上就崩溃等等
    reply->set_updatenextindex(args->prevlogindex());

    for (int index = args->prevlogindex(); index >= m_lastSnapshotIncludeIndex;
         --index) {
      if (getLogTermFromLogIndex(index) !=
          getLogTermFromLogIndex(args->prevlogindex())) {
        reply->set_updatenextindex(index + 1);
        break;
      }
    }
    reply->set_success(false);
    reply->set_term(m_currentTerm);

    //        DPrintf("[func-AppendEntries-rf{%v}]
    //        拒绝了节点{%v}，因为prevLodIndex{%v}的args.term{%v}不匹配当前节点的logterm{%v}，返回值：{%v}\n",
    //                rf.me, args.LeaderId, args.PrevLogIndex, args.PrevLogTerm,
    //                rf.logs[rf.getSlicesIndexFromLogIndex(args.PrevLogIndex)].LogTerm,
    //                reply)
    //        DPrintf("[func-AppendEntries-rf{%v}] 返回值:
    //        reply.UpdateNextIndex从{%v}优化到{%v}，优化了{%v}\n", rf.me,
    //                args.PrevLogIndex, reply.UpdateNextIndex,
    //                args.PrevLogIndex - reply.UpdateNextIndex)

    return;
  }
}

void Raft::applierTicker() {
  while (true) {
    m_mtx.lock();
    if (m_status == Leader) {
      DPrintf("[Raft::applierTicker() - raft{%d}]  m_lastApplied{%d}   "
              "m_commitIndex{%d}",
              m_me, m_lastApplied, m_commitIndex);
    }
    auto applyMsgs = getApplyLogs();
    m_mtx.unlock();
    // 使用匿名函数是因为传递管道的时候不用拿锁
    //  todo:好像必须拿锁，因为不拿锁的话如果调用多次applyLog函数，可能会导致应用的顺序不一样
    if (!applyMsgs.empty()) {
      DPrintf("[func- Raft::applierTicker()-raft{%d}] "
              "向kvserver報告的applyMsgs長度爲：{%d}",
              m_me, applyMsgs.size());
    }
    for (auto &message : applyMsgs) {
      applyChan->Push(message);
    }
    // usleep(1000 * ApplyInterval);
    sleepNMilliseconds(ApplyInterval);
  }
}

bool Raft::CondInstallSnapshot(int lastIncludedTerm, int lastIncludedIndex,
                               std::string snapshot) {
  return true;
  //// Your code here (2D).
  // rf.mu.Lock()
  // defer rf.mu.Unlock()
  // DPrintf("{Node %v} service calls CondInstallSnapshot with lastIncludedTerm
  // %v and lastIncludedIndex {%v} to check whether snapshot is still valid in
  // term %v", rf.me, lastIncludedTerm, lastIncludedIndex, rf.currentTerm)
  //// outdated snapshot
  // if lastIncludedIndex <= rf.commitIndex {
  //	return false
  // }
  //
  // lastLogIndex, _ := rf.getLastLogIndexAndTerm()
  // if lastIncludedIndex > lastLogIndex {
  //	rf.logs = make([]LogEntry, 0)
  // } else {
  //	rf.logs = rf.logs[rf.getSlicesIndexFromLogIndex(lastIncludedIndex)+1:]
  // }
  //// update dummy entry with lastIncludedTerm and lastIncludedIndex
  // rf.lastApplied, rf.commitIndex = lastIncludedIndex, lastIncludedIndex
  //
  // rf.persister.Save(rf.persistData(), snapshot)
  // return true
}

/**
 * ! @description:
 * 选举函数，实际发起选举，构造需要发送的rpc，并多线程调用sendRequestVote处理rpc及其相应操作。
 * @return {*}
 */
void Raft::doElection() {
  std::lock_guard<std::mutex> g(m_mtx);

  if (m_status != Leader) {
    DPrintf(
        "[  ticker-func-rf(%d)  ]  选举定时器到期 且不是 leader，开始选举!\n", m_me);
    // 当选举的时候定时器超时就必须重新选举，不然没有选票就会一直卡住
    m_status = Candidate;
    // 开始新一轮的选举
    m_currentTerm += 1; // 任期 term 增加
    m_votedFor = m_me; // 即是自己给自己投
    persist();
    // 代表当前节点给自己的一票
    std::shared_ptr<int> votedNum = std::make_shared<int>(1); // 使用 make_shared 函数初始化
    //	重新设置定时器，记录选举超时计时器被重置的时间
    m_lastResetElectionTime = now();

    //	发布 RequestVote RPC  遍历所有已知节点
    for (int i = 0; i < m_peers.size(); i++) {
      if (i == m_me)
        continue; // 不包括当前节点自身

      int lastLogIndex = -1, lastLogTerm = -1;
      getLastLogIndexAndTerm(&lastLogIndex, &lastLogTerm); // 获取当前节点最后一个log的term和下标
      // 初始化发送参数
      std::shared_ptr<raftRpcProctoc::RequestVoteArgs> requestVoteArgs =
          std::make_shared<raftRpcProctoc::RequestVoteArgs>();
        requestVoteArgs->set_term(m_currentTerm);
        requestVoteArgs->set_candidateid(m_me);
        requestVoteArgs->set_lastlogindex(lastLogIndex);
        requestVoteArgs->set_lastlogterm(lastLogTerm);
      // 创建回复对象
      auto requestVoteReply =
          std::make_shared<raftRpcProctoc::RequestVoteReply>();

      // 使用匿名函数执行避免其拿到锁
      // 创建新线程并执行函数，并传递参数
      // 对于每个节点，创建新线程来异步发送 RequestVote RPC
      std::thread t(&Raft::sendRequestVote, this, i, requestVoteArgs, requestVoteReply, votedNum);
      t.detach(); // 线程分离，允许独立运行而不需要等待主线程结束
    }
  }
}

/**
 * ! @description: 向 follower
 * 实际发送心跳,构造需要发送的rpc，并多线程调用sendRequestVote处理rpc及其相应
 * @return {*}
 */
void Raft::doHeartBeat() {
  std::lock_guard<std::mutex> g(m_mtx); // 当g对象离开作用域后自动解锁

  if (m_status == Leader) {
    DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] "
            "Leader的心跳定时器触发了且拿到 mutex，开始发送AE\n", m_me);
    auto appendNums = std::make_shared<int>(1); // 正确返回的节点的数量

    // 对Follower（除了自己外的所有节点发送AE）

    // todo
    // 这里肯定是要修改的，最好使用一个单独的goruntime来负责管理发送log，因为后面的log发送涉及优化之类的
    // 最少要单独写一个函数来管理，而不是在这一坨
    for (int i = 0; i < m_peers.size(); i++) {
      if (i == m_me)
        continue;
      DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] "
              "Leader的心跳定时器触发了 index:{%d}\n", m_me, i);
      myAssert(m_nextIndex[i] >= 1,
               format("rf.nextIndex[%d] = {%d}", i, m_nextIndex[i]));
      // 日志压缩加入后要判断是发送快照还是发送AE
      if (m_nextIndex[i] <= m_lastSnapshotIncludeIndex) {
        // 该发送的日志已经作为快照了，必须发送快照
        std::thread t(&Raft::leaderSendSnapShot, this, i); // 创建新线程并执行快照发送函数
        t.detach();
        continue;
      }
      // 构造发送值
      int preLogIndex = -1;
      int PrevLogTerm = -1;
      // 获取本次发送的一系列日志的上一条日志的信息，以判断是否匹配
      getPrevLogInfo(i, &preLogIndex, &PrevLogTerm);
      std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> appendEntriesArgs =
          std::make_shared<raftRpcProctoc::AppendEntriesArgs>();
      appendEntriesArgs->set_term(m_currentTerm);
      appendEntriesArgs->set_leaderid(m_me);
      appendEntriesArgs->set_prevlogindex(preLogIndex);
      appendEntriesArgs->set_prevlogterm(PrevLogTerm);
      appendEntriesArgs->clear_entries();
      appendEntriesArgs->set_leadercommit(m_commitIndex);

      // 作用是携带上prelogIndex的下一条日志及其之后的所有日志
      // leader对每个节点发送的日志长短不一，但是都保证从prevIndex发送直到最后
      if (preLogIndex != m_lastSnapshotIncludeIndex) {
        for (int j = getSlicesIndexFromLogIndex(preLogIndex) + 1; j < m_logs.size(); ++j) {
          raftRpcProctoc::LogEntry *sendEntryPtr = appendEntriesArgs->add_entries();
          *sendEntryPtr = m_logs[j]; //=是可以点进去的，可以点进去看下protobuf如何重写这个的
        }
      } else {
        for (const auto &item : m_logs) {
          raftRpcProctoc::LogEntry *sendEntryPtr = appendEntriesArgs->add_entries();
          *sendEntryPtr = item; //=是可以点进去的，可以点进去看下protobuf如何重写这个的
        }
      }
      int lastLogIndex = getLastLogIndex();
      // leader对每个节点发送的日志长短不一，但是都保证从prevIndex发送直到最后
      myAssert(appendEntriesArgs->prevlogindex() + appendEntriesArgs->entries_size() == lastLogIndex,
               format("appendEntriesArgs.PrevLogIndex{%d}+len("
                      "appendEntriesArgs.Entries){%d} != lastLogIndex{%d}",
                      appendEntriesArgs->prevlogindex(),
                      appendEntriesArgs->entries_size(), lastLogIndex));
      // 构造返回值
      const std::shared_ptr<raftRpcProctoc::AppendEntriesReply>
          appendEntriesReply = std::make_shared<raftRpcProctoc::AppendEntriesReply>();
      appendEntriesReply->set_appstate(Disconnected);

      std::thread t(
          &Raft::sendAppendEntries, this, i, appendEntriesArgs, appendEntriesReply, appendNums); // 创建新线程并执行sendAppendEntries函数，并传递参数
      t.detach();
    }
    m_lastResetHearBeatTime = now(); // leader发送心跳，重置心跳时间表示已发送，就不是随机时间了
  }
}

/**
 * @description: 选举超时判断
 *               当节点处于 folloewer 状态时，查看是否发起该选举，如果该发起选举则执行 doElection()
 * @return {*}
 */
void Raft::electionTimeOutTicker() {
  while (true) {
    // 如果不睡眠，那么对于leader，这个函数会一直空转，浪费cpu。
    // 且加入协程之后，空转会导致其他协程无法运行，对于时间敏感的AE，会导致心跳无法正常发送导致异常

    // 如果当前节点已经是 leader状态，则睡眠 避免浪费cpu
    while (m_status == Leader) {
      usleep(HeartBeatTimeout);
      // 定时时间没有严谨设置，因为HeartBeatTimeout比选举超时一般小一个数量级，因此就设置为HeartBeatTimeout了
    }
    std::chrono::duration<signed long int, std::ratio<1, 1000000000>>
        suitableSleepTime{};
    std::chrono::system_clock::time_point wakeTime{};
    {
      m_mtx.lock();
      // 唤醒时间
      wakeTime = now();
      // 计算合适的休眠时间
      suitableSleepTime =
          getRandomizedElectionTimeout() + m_lastResetElectionTime - wakeTime;
      m_mtx.unlock();
    }
    // 休眠时间大于 1 ms
    if (std::chrono::duration<double, std::milli>(suitableSleepTime).count() > 1) {
      // 获取当前时间点
      auto start = std::chrono::steady_clock::now();

      // 开始休眠
      usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count());
      // std::this_thread::sleep_for(suitableSleepTime);

      // 获取函数运行结束后的时间点
      auto end = std::chrono::steady_clock::now();

      // 计算时间差并输出结果（单位为毫秒）
      std::chrono::duration<double, std::milli> duration = end - start;

      // 使用ANSI控制序列将输出颜色修改为紫色
      // 打印出设置的休眠时间和实际的休眠时间
      std::cout << "\033[1;35m electionTimeOutTicker();函数设置睡眠时间为: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(
                       suitableSleepTime)
                       .count()
                << " 毫秒\033[0m" << std::endl;
      std::cout << "\033[1;35m electionTimeOutTicker();函数实际睡眠时间为: "
                << duration.count() << " 毫秒\033[0m" << std::endl;
    }
    // 检查是否在休眠期间重置了选举时间
    // 上次重置选举时间 - 唤醒时间 > 0
    //      这意味着从最后一次重置选举超时计时器到现在的时间是正的，即选举超时计时器尚未用完。
    if (std::chrono::duration<double, std::milli>(m_lastResetElectionTime - wakeTime).count() > 0) {
      // 说明睡眠的这段时间有重置定时器，那么就没有超时，再次睡眠
      continue;
    }
    // 开始选举
    doElection();
  }
}

std::vector<ApplyMsg> Raft::getApplyLogs() {
  std::vector<ApplyMsg> applyMsgs;
  myAssert(
      m_commitIndex <= getLastLogIndex(),
      format("[func-getApplyLogs-rf{%d}] commitIndex{%d} >getLastLogIndex{%d}",
             m_me, m_commitIndex, getLastLogIndex()));

  while (m_lastApplied < m_commitIndex) {
    m_lastApplied++;
    myAssert(
        m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].logindex() ==
            m_lastApplied,
        format("rf.logs[rf.getSlicesIndexFromLogIndex(rf.lastApplied)]."
               "LogIndex{%d} != rf.lastApplied{%d} ",
               m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].logindex(),
               m_lastApplied));
    ApplyMsg applyMsg;
    applyMsg.CommandValid = true;
    applyMsg.SnapshotValid = false;
    applyMsg.Command =
        m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].command();
    applyMsg.CommandIndex = m_lastApplied;
    applyMsgs.emplace_back(applyMsg);
    //        DPrintf("[	applyLog func-rf{%v}	] apply Log,logIndex:%v
    //        ，logTerm：{%v},command：{%v}\n", rf.me, rf.lastApplied,
    //        rf.logs[rf.getSlicesIndexFromLogIndex(rf.lastApplied)].LogTerm,
    //        rf.logs[rf.getSlicesIndexFromLogIndex(rf.lastApplied)].Command)
  }
  return applyMsgs;
}

// 获取新命令应该分配的Index
int Raft::getNewCommandIndex() {
  //	如果len(logs)==0,就为快照的index+1，否则为log最后一个日志+1
  auto lastLogIndex = getLastLogIndex();
  return lastLogIndex + 1;
}

// getPrevLogInfo
// leader调用，传入：服务器index，传出：发送的AE的preLogIndex和PrevLogTerm
void Raft::getPrevLogInfo(int server, int *preIndex, int *preTerm) {
  // logs长度为0返回0,0，不是0就根据nextIndex数组的数值返回
  if (m_nextIndex[server] == m_lastSnapshotIncludeIndex + 1) {
    // 要发送的日志是第一个日志，因此直接返回m_lastSnapshotIncludeIndex和m_lastSnapshotIncludeTerm
    *preIndex = m_lastSnapshotIncludeIndex;
    *preTerm = m_lastSnapshotIncludeTerm;
    return;
  }
  auto nextIndex = m_nextIndex[server];
  *preIndex = nextIndex - 1;
  *preTerm = m_logs[getSlicesIndexFromLogIndex(*preIndex)].logterm();
}

// GetState return currentTerm and whether this server
// believes it is the Leader.
void Raft::GetState(int *term, bool *isLeader) {
  m_mtx.lock();
  DEFER {
    // todo 暂时不清楚会不会导致死锁
    m_mtx.unlock();
  };

  // Your code here (2A).
  *term = m_currentTerm;
  *isLeader = (m_status == Leader);
}

/**
 * @description: 接收leader发来的快照请求，同步快照到本机
 * @return {*}
 */
void Raft::InstallSnapshot(const raftRpcProctoc::InstallSnapshotRequest *args,
                           raftRpcProctoc::InstallSnapshotResponse *reply) {
  m_mtx.lock();
  DEFER { m_mtx.unlock(); };
  if (args->term() < m_currentTerm) {
    reply->set_term(m_currentTerm);
    //        DPrintf("[func-InstallSnapshot-rf{%v}]
    //        leader{%v}.term{%v}<rf{%v}.term{%v} ", rf.me, args.LeaderId,
    //        args.Term, rf.me, rf.currentTerm)

    return;
  }
  if (args->term() > m_currentTerm) {
    // 后面两种情况都要接收日志
    m_currentTerm = args->term();
    m_votedFor = -1;
    m_status = Follower;
    persist();
  }
  m_status = Follower;
  m_lastResetElectionTime = now();
  // outdated snapshot
  if (args->lastsnapshotincludeindex() <= m_lastSnapshotIncludeIndex) {
    //        DPrintf("[func-InstallSnapshot-rf{%v}]
    //        leader{%v}.LastSnapShotIncludeIndex{%v} <=
    //        rf{%v}.lastSnapshotIncludeIndex{%v} ", rf.me, args.LeaderId,
    //        args.LastSnapShotIncludeIndex, rf.me, rf.lastSnapshotIncludeIndex)
    return;
  }
  // 截断日志，修改commitIndex和lastApplied
  // 截断日志包括：日志长了，截断一部分，日志短了，全部清空，其实两个是一种情况
  // 但是由于现在getSlicesIndexFromLogIndex的实现，不能传入不存在logIndex，否则会panic
  auto lastLogIndex = getLastLogIndex();

  if (lastLogIndex > args->lastsnapshotincludeindex()) {
    m_logs.erase(
        m_logs.begin(),
        m_logs.begin() +
            getSlicesIndexFromLogIndex(args->lastsnapshotincludeindex()) + 1);
  } else {
    m_logs.clear();
  }
  m_commitIndex = std::max(m_commitIndex, args->lastsnapshotincludeindex());
  m_lastApplied = std::max(m_lastApplied, args->lastsnapshotincludeindex());
  m_lastSnapshotIncludeIndex = args->lastsnapshotincludeindex();
  m_lastSnapshotIncludeTerm = args->lastsnapshotincludeterm();

  reply->set_term(m_currentTerm);
  ApplyMsg msg;
  msg.SnapshotValid = true;
  msg.Snapshot = args->data();
  msg.SnapshotTerm = args->lastsnapshotincludeterm();
  msg.SnapshotIndex = args->lastsnapshotincludeindex();

  applyChan->Push(msg);
  std::thread t(&Raft::pushMsgToKvServer, this,
                msg); // 创建新线程并执行b函数，并传递参数
  t.detach();
  // 看下这里能不能再优化
  //     DPrintf("[func-InstallSnapshot-rf{%v}] receive snapshot from {%v}
  //     ,LastSnapShotIncludeIndex ={%v} ", rf.me, args.LeaderId,
  //     args.LastSnapShotIncludeIndex)
  // 持久化
  m_persister->Save(persistData(), args->data());
}

void Raft::pushMsgToKvServer(ApplyMsg msg) { applyChan->Push(msg); }

/**
 * ! @description: 负责查看是否该发送心跳了，如果该发起就执行doHeartBeat()
 * @return {*}
 */
void Raft::leaderHearBeatTicker() {
  while (true) {
    // 不是leader的话就没有必要进行后续操作，况且还要拿锁，很影响性能，目前是睡眠，后面再优化
    while (m_status != Leader) {
      usleep(1000 * HeartBeatTimeout);
      // std::this_thread::sleep_for(std::chrono::milliseconds(HeartBeatTimeout));
    }

    static std::atomic<int32_t> atomicCount = 0; // 记录心跳循环的次数
    std::chrono::duration<signed long int, std::ratio<1, 1000000000>> suitableSleepTime{};
    std::chrono::system_clock::time_point wakeTime{};
    {
      std::lock_guard<std::mutex> lock(m_mtx);
      wakeTime = now();
      // 计算睡眠时间
      suitableSleepTime = std::chrono::milliseconds(HeartBeatTimeout) +
                          m_lastResetHearBeatTime - wakeTime;
    }

    if (std::chrono::duration<double, std::milli>(suitableSleepTime).count() > 1) {
      std::cout << atomicCount
                << "\033[1;35m leaderHearBeatTicker();函数设置睡眠时间为: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(
                       suitableSleepTime)
                       .count()
                << " 毫秒\033[0m" << std::endl;
      // 获取当前时间点
      auto start = std::chrono::steady_clock::now();
      // 使当前线程睡眠
      usleep(std::chrono::duration_cast<std::chrono::microseconds>(
                 suitableSleepTime)
                 .count());
      // std::this_thread::sleep_for(suitableSleepTime);

      // 获取函数运行结束后的时间点
      auto end = std::chrono::steady_clock::now();

      // 计算时间差并输出结果（单位为毫秒）
      std::chrono::duration<double, std::milli> duration = end - start;

      // 使用ANSI控制序列将输出颜色修改为紫色
      std::cout << atomicCount
                << "\033[1;35m leaderHearBeatTicker();函数实际睡眠时间为: "
                << duration.count() << " 毫秒\033[0m" << std::endl;
      ++atomicCount; // 心跳次数加一
    }

    if (std::chrono::duration<double, std::milli>(m_lastResetHearBeatTime - wakeTime) .count() > 0) {
      // 睡眠的这段时间有重置定时器，没有超时，再次睡眠
      continue;
    }
    // DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}]
    // Leader的心跳定时器触发了\n", m_me);
    doHeartBeat();
  }
}

/**
 * @description:
 * 负责发送快照的RPC，在发送完rpc后还需要负责接收并处理对端发送回来的响应
 * @param {int} server
 * @return {*}
 */
void Raft::leaderSendSnapShot(int server) {
  m_mtx.lock();
  raftRpcProctoc::InstallSnapshotRequest args;
  args.set_leaderid(m_me);
  args.set_term(m_currentTerm);
  args.set_lastsnapshotincludeindex(m_lastSnapshotIncludeIndex);
  args.set_lastsnapshotincludeterm(m_lastSnapshotIncludeTerm);
  args.set_data(m_persister->ReadSnapshot());

  raftRpcProctoc::InstallSnapshotResponse reply;
  m_mtx.unlock();
  bool ok = m_peers[server]->InstallSnapshot(&args, &reply);
  m_mtx.lock();
  DEFER { m_mtx.unlock(); };
  if (!ok) {
    return;
  }
  if (m_status != Leader || m_currentTerm != args.term()) {
    return; // 中间释放过锁，可能状态已经改变了
  }
  //	无论什么时候都要判断term
  if (reply.term() > m_currentTerm) {
    // 三变
    m_currentTerm = reply.term();
    m_votedFor = -1;
    m_status = Follower;
    persist();
    m_lastResetElectionTime = now();
    return;
  }
  m_matchIndex[server] = args.lastsnapshotincludeindex();
  m_nextIndex[server] = m_matchIndex[server] + 1;
}

void Raft::leaderUpdateCommitIndex() {
  m_commitIndex = m_lastSnapshotIncludeIndex;

  for (int index = getLastLogIndex(); index >= m_lastSnapshotIncludeIndex + 1;
       index--) {
    int sum = 0;
    for (int i = 0; i < m_peers.size(); i++) {
      if (i == m_me) {
        sum += 1;
        continue;
      }
      if (m_matchIndex[i] >= index) {
        sum += 1;
      }
    }

    // !!!只有当前term有新提交的，才会更新commitIndex！！！！
    // rf.getLastIndex())
    if (sum >= m_peers.size() / 2 + 1 &&
        getLogTermFromLogIndex(index) == m_currentTerm) {
      m_commitIndex = index;
      break;
    }
  }
  //    DPrintf("[func-leaderUpdateCommitIndex()-rf{%v}] Leader %d(term%d)
  //    commitIndex %d",rf.me,rf.me,rf.currentTerm,rf.commitIndex)
}

// 进来前要保证logIndex是存在的，即≥rf.lastSnapshotIncludeIndex
// ，而且小于等于rf.getLastLogIndex()
bool Raft::matchLog(int logIndex, int logTerm) {
  myAssert(logIndex >= m_lastSnapshotIncludeIndex &&
               logIndex <= getLastLogIndex(),
           format("不满足：logIndex{%d}>=rf.lastSnapshotIncludeIndex{%d}&&"
                  "logIndex{%d}<=rf.getLastLogIndex{%d}",
                  logIndex, m_lastSnapshotIncludeIndex, logIndex,
                  getLastLogIndex()));
  return logTerm == getLogTermFromLogIndex(logIndex);
}

void Raft::persist() {
  auto data = persistData();
  m_persister->SaveRaftState(data);
}

/**
 * ! @description: 接收别人发来的选举 RequestVote RPC
 * 请求，主要检验是否要给对方投票。
 * @param {RequestVoteArgs*} args
 * @param {RequestVoteReply*} reply
 * @return {*}
 */
// ! 以下内容为选举机制的核心部分，确保投票的合法性和日志的一致性
void Raft::RequestVote(const raftRpcProctoc::RequestVoteArgs *args,
                       raftRpcProctoc::RequestVoteReply *reply) {
  std::lock_guard<std::mutex> lg(m_mtx);
  DEFER {
    // 应该先持久化，再撤销lock
    persist();
  };
  // 对args的 term 的三种情况分别进行处理，大于小于等于 自己的term都是不同的处理

  // 1. 请求的任期号 小于 当前任期号，说明请求已过时，当前节点不会投票给候选人
  if (args->term() < m_currentTerm) {
    // 设置回复状态
    reply->set_term(m_currentTerm);
    reply->set_votestate(Expire);
    reply->set_votegranted(false);
    return;
  }
  // fig2:右下角，如果任何时候rpc请求或者响应的term大于自己的term，更新term，并变成follower
  // 2. 请求的任期号 大于 当前任期号，当前节点更新任期号，变为 follower
  if (args->term() > m_currentTerm) {
    m_status = Follower;
    m_currentTerm = args->term();
    m_votedFor = -1; // 表示没有投票给候选人
  }
  myAssert(
      args->term() == m_currentTerm,
      format(
          "[func--rf{%d}] 前面校验过 args.Term == rf.currentTerm，这里却不等",
          m_me));
  // 现在节点任期都是相同的(任期小的也已经更新到新的args的term了)，还需要检查log的term和index是不是匹配的了
  // 节点任期号相同，检查候选人日志是否至少和自己一样新 ：uptodate()函数
  int lastLogTerm = getLastLogTerm();
  // 只有没投票，且candidate的日志的新的程度 ≥ 接受者的日志新的程度 才会授票
  if (!UpToDate(args->lastlogindex(), args->lastlogterm())) {
    // 检查结果为日志太旧了，拒绝投票
    reply->set_term(m_currentTerm);
    reply->set_votestate(Voted);
    reply->set_votegranted(false);
    return;
  }
  // todo ： 啥时候会出现rf.votedFor == args.CandidateId
  // ，就算candidate选举超时再选举，其term也是不一样的呀
  // 当因为网络质量不好导致的请求丢失重发就有可能！
  // 避免重复投票，当前节点已经投票给另一节点
  if (m_votedFor != -1 && m_votedFor != args->candidateid()) {
    reply->set_term(m_currentTerm);
    reply->set_votestate(Voted);
    reply->set_votegranted(false);
    return;
  } else {
    // 同意投票
    m_votedFor = args->candidateid(); // 更新为投票人的 ID
    m_lastResetElectionTime =
        now(); // 认为必须要在投出票的时候才重置超时选举定时器，

    reply->set_term(m_currentTerm);
    reply->set_votestate(Normal);
    reply->set_votegranted(true);
    return;
  }
}

// ? 检查候选人的日志是否一样新
bool Raft::UpToDate(int index, int term) {
  int lastIndex = -1;
  int lastTerm = -1;
  getLastLogIndexAndTerm(&lastIndex, &lastTerm);
  // 比较当前节点和候选人的 term 和 index
  return term > lastTerm || (term == lastTerm && index >= lastIndex);
}
// ? 获取当前节点的最后一条日志的索引和任期号
void Raft::getLastLogIndexAndTerm(int *lastLogIndex, int *lastLogTerm) {
  if (m_logs.empty()) { // 日志为空，则返回最后保存快照的索引和任期号
    *lastLogIndex = m_lastSnapshotIncludeIndex;
    *lastLogTerm = m_lastSnapshotIncludeTerm;
    return;
  } else {
    *lastLogIndex = m_logs[m_logs.size() - 1].logindex();
    *lastLogTerm = m_logs[m_logs.size() - 1].logterm();
    return;
  }
}
/**
 *
 * @return 最新的log的logindex，即log的逻辑index。区别于log在m_logs中的物理index
 * 可见：getLastLogIndexAndTerm()
 */
int Raft::getLastLogIndex() {
  int lastLogIndex = -1;
  int _ = -1;
  getLastLogIndexAndTerm(&lastLogIndex, &_);
  return lastLogIndex;
}

int Raft::getLastLogTerm() {
  int _ = -1;
  int lastLogTerm = -1;
  getLastLogIndexAndTerm(&_, &lastLogTerm);
  return lastLogTerm;
}

/**
 *
 * @param logIndex log的逻辑index。注意区别于m_logs的物理index
 * @return
 */
int Raft::getLogTermFromLogIndex(int logIndex) {
  myAssert(logIndex >= m_lastSnapshotIncludeIndex,
           format("[func-getSlicesIndexFromLogIndex-rf{%d}]  index{%d} < "
                  "rf.lastSnapshotIncludeIndex{%d}",
                  m_me, logIndex, m_lastSnapshotIncludeIndex));

  int lastLogIndex = getLastLogIndex();

  myAssert(logIndex <= lastLogIndex,
           format("[func-getSlicesIndexFromLogIndex-rf{%d}]  logIndex{%d} > "
                  "lastLogIndex{%d}",
                  m_me, logIndex, lastLogIndex));

  if (logIndex == m_lastSnapshotIncludeIndex) {
    return m_lastSnapshotIncludeTerm;
  } else {
    return m_logs[getSlicesIndexFromLogIndex(logIndex)].logterm();
  }
}

int Raft::GetRaftStateSize() { return m_persister->RaftStateSize(); }

// 找到index对应的真实下标位置！！！
// 限制，输入的logIndex必须保存在当前的logs里面（不包含snapshot）
int Raft::getSlicesIndexFromLogIndex(int logIndex) {
  myAssert(logIndex > m_lastSnapshotIncludeIndex,
           format("[func-getSlicesIndexFromLogIndex-rf{%d}]  index{%d} <= "
                  "rf.lastSnapshotIncludeIndex{%d}",
                  m_me, logIndex, m_lastSnapshotIncludeIndex));
  int lastLogIndex = getLastLogIndex();
  myAssert(logIndex <= lastLogIndex,
           format("[func-getSlicesIndexFromLogIndex-rf{%d}]  logIndex{%d} > "
                  "lastLogIndex{%d}",
                  m_me, logIndex, lastLogIndex));
  int SliceIndex = logIndex - m_lastSnapshotIncludeIndex - 1;
  return SliceIndex;
}

/**
 * ! @description: 负责向指定的服务器发送RequestVote RPC请求，并处理响应
 * @return {*}
 */
bool Raft::sendRequestVote(
    int server, std::shared_ptr<raftRpcProctoc::RequestVoteArgs> args,
    std::shared_ptr<raftRpcProctoc::RequestVoteReply> reply,
    std::shared_ptr<int> votedNum) {

  auto start = now();
  DPrintf("[func-sendRequestVote rf{%d}] 向server{%d} 发送 RequestVote 开始",
          m_me, m_currentTerm, getLastLogIndex());
  // 调用 RequestVote 方法，向指定的服务器发送请求。
  // 并检验 RPC 调用是否成功
  bool ok = m_peers[server]->RequestVote(args.get(), reply.get());
  DPrintf("[func-sendRequestVote rf{%d}] 向server{%d} 发送 RequestVote "
          "完毕，耗时:{%d} ms",
          m_me, m_currentTerm, getLastLogIndex(), now() - start);

  if (!ok)
    return ok; // rpc通信失败就立即返回，避免资源消耗

  std::lock_guard<std::mutex> lg(m_mtx);
  // 检查响应中的任期号term与当前节点的任期号m_currentTerm：
  if (reply->term() > m_currentTerm) {
    m_status = Follower; // 三变：身份，term，和投票
    m_currentTerm = reply->term();
    m_votedFor = -1;
    persist(); // 持久化
    return true;
  } else if (reply->term() < m_currentTerm) // 忽略该响应
  {
    return true;
  }
  myAssert(reply->term() == m_currentTerm,
           format("assert {reply.Term == rf.currentTerm} fail"));

  // 处理投票结果
  // 这个节点因为某些原因没给自己投票，结束本函数
  if (!reply->votegranted())
    return true;

  // 为自己投票
  *votedNum = *votedNum + 1;
  if (*votedNum >= m_peers.size() / 2 + 1) {
    // 投票获得多数，变成leader
    *votedNum = 0; // 重置投票数
    if (m_status == Leader) {
      // 如果已经是leader了，那么是就是了，不会进行下一步处理了
      myAssert(false, format("[func-sendRequestVote-rf{%d}]  term:{%d} 同一个 "
                             "term 当两次 Leader，error",
                             m_me, m_currentTerm));
    }
    // 第一次变成leader，初始化状态和 nextIndex、matchIndex
    m_status = Leader;
    DPrintf("[func-sendRequestVote rf{%d}] elect success, current term:{%d} "
            ",lastLogIndex:{%d}\n",
            m_me, m_currentTerm, getLastLogIndex());

    // todo rb 这一块的逻辑不太理解
    int lastLogIndex = getLastLogIndex();
    for (int i = 0; i < m_nextIndex.size(); i++) {
      m_nextIndex[i] = lastLogIndex + 1; // 有效下标从1开始，因此要+1
      m_matchIndex[i] = 0; // 每换一个领导都是从0开始，见fig2
    }
    std::thread t(&Raft::doHeartBeat, this); // 马上向其他节点宣告自己就是leader
    t.detach();
    persist();
  }
  return true;
}

/**
 * ! @description:
 * 负责发送日志的RPC，在发送完rpc后还需要负责接收并处理对端发送回来的响应
 * @return {*}
 */
bool Raft::sendAppendEntries(
    int server, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args,
    std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply,
    std::shared_ptr<int> appendNums) {
  // 这个ok是网络是否正常通信的ok，而不是requestVote rpc是否投票的rpc
  //  如果网络不通的话肯定是没有返回的，不用一直重试
  //  todo：paper中5.3节第一段末尾提到，如果append失败应该不断的retries
  //  ,直到这个log成功的被store
  DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE "
          "rpc开始 ， args->entries_size():{%d}",
          m_me, server, args->entries_size());
  // 实际发送 AppendEntries RPC 请求，ok 记录返回值
  bool ok = m_peers[server]->AppendEntries(args.get(), reply.get());

  if (!ok) {
    DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE "
            "rpc失败",
            m_me, server);
    return ok;
  }
  DPrintf(
      "[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc成功",
      m_me, server);
  // 回复表明节点已断开连接
  if (reply->appstate() == Disconnected)
    return ok;

  std::lock_guard<std::mutex> lg1(m_mtx);

  // 对reply进行处理， 检查 term 任期值
  // 如果收到的任期更大，则 leader 变为 follwer
  if (reply->term() > m_currentTerm) {
    m_status = Follower;
    m_currentTerm = reply->term();
    m_votedFor = -1;
    return ok;
  }
  // 忽略该回复
  else if (reply->term() < m_currentTerm) {
    DPrintf("[func -sendAppendEntries  rf{%d}]  节点：{%d}的 term{%d} < "
            "rf{%d}的 term{%d}\n",
            m_me, server, reply->term(), m_me, m_currentTerm);
    return ok;
  }
  // 如果不是leader，不需要处理
  if (m_status != Leader)
    return ok;

  // term相等
  myAssert(reply->term() == m_currentTerm,
           format("reply.Term{%d} != rf.currentTerm{%d}   ", reply->term(),
                  m_currentTerm));

  if (!reply->success()) {
    // 日志不匹配，正常来说就是index要往前-1，既然能到这里，第一个日志（idnex =
    //  1）发送后肯定是匹配的，因此不用考虑变成负数
    //  因为真正的环境不会知道是服务器宕机还是发生网络分区了
    if (reply->updatenextindex() != -100) {
      // todo:待总结，就算term匹配，失败的时候nextIndex也不是照单全收的，因为如果发生rpc延迟，leader的term可能从不符合term要求
      // 变得符合term要求
      // 但是不能直接赋值 reply.UpdateNextIndex
      DPrintf("[func -sendAppendEntries  rf{%d}]  "
              "返回的日志term相等，但是不匹配，回缩nextIndex[%d]：{%d}\n",
              m_me, server, reply->updatenextindex());
      m_nextIndex[server] = reply->updatenextindex(); // 失败是不更新mathIndex的
    }
    //	怎么越写越感觉rf.nextIndex数组是冗余的呢，看下论文fig2，其实不是冗余的
  } else // RPC 调用成功
  {
    *appendNums = *appendNums + 1; // 成功返回的节点加一
    DPrintf("-----tmp------- 节点{%d}返回 true,当前*appendNums{%d}", server,
            *appendNums);
    // rf.matchIndex[server] = len(args.Entries)
    // //只要返回一个响应就对其matchIndex应该对其做出反应，
    // 但是这么修改是有问题的，如果对某个消息发送了多遍（心跳时就会再发送），那么一条消息会导致n次上涨
    m_matchIndex[server] = std::max(
        m_matchIndex[server], args->prevlogindex() + args->entries_size());
    m_nextIndex[server] = m_matchIndex[server] + 1;
    int lastLogIndex = getLastLogIndex();

    myAssert(m_nextIndex[server] <= lastLogIndex + 1,
             format("error msg:rf.nextIndex[%d] > lastLogIndex+1, len(rf.logs) "
                    "= %d   lastLogIndex{%d} = %d",
                    server, m_logs.size(), server, lastLogIndex));
    // 超过半数节点返回，可以提交 commit
    if (*appendNums >= 1 + m_peers.size() / 2) {
      // 可以commit了
      // 两种方法保证幂等性，1.赋值为0 	2.上面≥改为==

      *appendNums = 0;
      // todo rb 没有找到具体的日志提交过程
      // todo https://578223592-laughing-halibut-wxvpggvw69qh99q4.github.dev/
      // 不断遍历来统计rf.commitIndex
      // leader只有在当前term有日志提交的时候才更新commitIndex，因为raft无法保证之前term的Index是否提交
      // 只有当前term有日志提交，之前term的log才可以被提交，只有这样才能保证“领导人完备性{当选领导人的节点拥有之前被提交的所有log，当然也可能有一些没有被提交的}”
      // rf.leaderUpdateCommitIndex()
      if (args->entries_size() > 0) {
        DPrintf("args->entries(args->entries_size()-1).logterm(){%d}   "
                "m_currentTerm{%d}",
                args->entries(args->entries_size() - 1).logterm(),
                m_currentTerm);
      }
      if (args->entries_size() > 0 &&
          args->entries(args->entries_size() - 1).logterm() == m_currentTerm) {
        DPrintf("-------tmp------- "
                "当前term有log成功提交，更新leader的m_commitIndex "
                "from{%d} to {%d}",
                m_commitIndex, args->prevlogindex() + args->entries_size());

        m_commitIndex = std::max(m_commitIndex,
                                 args->prevlogindex() + args->entries_size());
      }
      myAssert(m_commitIndex <= lastLogIndex,
               format("[func-sendAppendEntries,rf{%d}] lastLogIndex:%d  "
                      "rf.commitIndex:%d\n",
                      m_me, lastLogIndex, m_commitIndex));
      // fmt.Printf("[func-sendAppendEntries,rf{%v}] len(rf.logs):%v
      // rf.commitIndex:%v\n", rf.me, len(rf.logs), rf.commitIndex)
    }
  }
  return ok;
}

void Raft::AppendEntries(google::protobuf::RpcController *controller,
                         const ::raftRpcProctoc::AppendEntriesArgs *request,
                         ::raftRpcProctoc::AppendEntriesReply *response,
                         ::google::protobuf::Closure *done) {
  AppendEntries1(request, response);
  done->Run();
}

void Raft::InstallSnapshot(
    google::protobuf::RpcController *controller,
    const ::raftRpcProctoc::InstallSnapshotRequest *request,
    ::raftRpcProctoc::InstallSnapshotResponse *response,
    ::google::protobuf::Closure *done) {
  InstallSnapshot(request, response);

  done->Run();
}

void Raft::RequestVote(google::protobuf::RpcController *controller,
                       const ::raftRpcProctoc::RequestVoteArgs *request,
                       ::raftRpcProctoc::RequestVoteReply *response,
                       ::google::protobuf::Closure *done) {
  RequestVote(request, response);
  done->Run();
}

void Raft::Start(Op command, int *newLogIndex, int *newLogTerm,
                 bool *isLeader) {
  std::lock_guard<std::mutex> lg1(m_mtx);
  //    m_mtx.lock();
  //    Defer ec1([this]()->void {
  //       m_mtx.unlock();
  //    });
  if (m_status != Leader) {
    DPrintf("[func-Start-rf{%d}]  is not leader");
    *newLogIndex = -1;
    *newLogTerm = -1;
    *isLeader = false;
    return;
  }

  raftRpcProctoc::LogEntry newLogEntry;
  newLogEntry.set_command(command.asString());
  newLogEntry.set_logterm(m_currentTerm);
  newLogEntry.set_logindex(getNewCommandIndex());
  m_logs.emplace_back(newLogEntry);

  int lastLogIndex = getLastLogIndex();

  // leader应该不停的向各个Follower发送AE来维护心跳和保持日志同步，目前的做法是新的命令来了不会直接执行，而是等待leader的心跳触发
  DPrintf("[func-Start-rf{%d}]  lastLogIndex:%d,command:%s\n", m_me,
          lastLogIndex, &command);
  // rf.timer.Reset(10)
  // //接收到命令后马上给follower发送,改成这样不知为何会出现问题，待修正 todo
  persist();
  *newLogIndex = newLogEntry.logindex();
  *newLogTerm = newLogEntry.logterm();
  *isLeader = true;
}

// Make
// the service or tester wants to create a Raft server. the ports
// of all the Raft servers (including this one) are in peers[]. this
// server's port is peers[me]. all the servers' peers[] arrays
// have the same order. persister is a place for this server to
// save its persistent state, and also initially holds the most
// recent saved state, if any. applyCh is a channel on which the
// tester or service expects Raft to send ApplyMsg messages.
// Make() must return quickly, so it should start goroutines
// for any long-running work.

/**
 * raft系统初始化
 * @param peers 服务器端口
 * @param me 节点自身
 * @param persister 持久化
 * @param applyCh 服务希望在Raft上发送Applymsg消息的通道
 */
void Raft::init(std::vector<std::shared_ptr<RaftRpcUtil>> peers, int me,
                std::shared_ptr<Persister> persister,
                std::shared_ptr<LockQueue<ApplyMsg>> applyCh) {
  m_peers = std::move(peers); // 与其他结点沟通的 RaftRpcUtil rpc类
  m_persister = std::move(persister);
  m_me = me;
  m_mtx.lock();

  // 发送 Applymsg 消息的通道
  this->applyChan = std::move(applyCh);

  m_currentTerm = 0;   // 初始化 term 为0
  m_status = Follower; // 初始化身份为 follower
  m_commitIndex = 0;
  m_lastApplied = 0;
  m_logs.clear();
  for (int i = 0; i < m_peers.size(); i++) {
    m_matchIndex.push_back(0);
    m_nextIndex.push_back(0);
  }
  m_votedFor = -1; // 当前term没有给其他人投过票就用-1表示

  m_lastSnapshotIncludeIndex = 0;
  m_lastSnapshotIncludeTerm = 0;
  m_lastResetElectionTime = now();
  m_lastResetHearBeatTime = now();

  readPersist(m_persister->ReadRaftState());
  if (m_lastSnapshotIncludeIndex > 0) {
    m_lastApplied = m_lastSnapshotIncludeIndex;
    // rf.commitIndex = rf.lastSnapshotIncludeIndex   todo
    // ：崩溃恢复为何不能读取commitIndex
  }

  DPrintf("[Init&ReInit] Sever %d, term %d, lastSnapshotIncludeIndex {%d} , "
          "lastSnapshotIncludeTerm {%d}",
          m_me, m_currentTerm, m_lastSnapshotIncludeIndex,
          m_lastSnapshotIncludeTerm);

  m_mtx.unlock();

  // 使用协程
  m_ioManager = std::make_unique<monsoon::IOManager>(FIBER_THREAD_NUM,
                                                     FIBER_USE_CALLER_THREAD);

  // 启动三个循环定时器
  // todo:原来是启动了三个线程，现在是直接使用了协程，三个函数中
  // leaderHearBeatTicker 、electionTimeOutTicker执行时间是恒定的，
  // applierTicker时间受到数据库响应延迟和两次apply之间请求数量的影响，这个随着数据量增多可能不太合理，最好其还是启用一个线程。
  m_ioManager->scheduler([this]() -> void { this->leaderHearBeatTicker(); });
  m_ioManager->scheduler([this]() -> void { this->electionTimeOutTicker(); });

  std::thread t3(&Raft::applierTicker, this);
  t3.detach();

  // std::thread t(&Raft::leaderHearBeatTicker, this);
  // t.detach();
  //
  // std::thread t2(&Raft::electionTimeOutTicker, this);
  // t2.detach();
  //
  // std::thread t3(&Raft::applierTicker, this);
  // t3.detach();
}

/**
 * @description: 对数据进行持久化操作
 * @return {*}
 */
std::string Raft::persistData() {
  BoostPersistRaftNode boostPersistRaftNode;
  boostPersistRaftNode.m_currentTerm = m_currentTerm;
  boostPersistRaftNode.m_votedFor = m_votedFor;
  boostPersistRaftNode.m_lastSnapshotIncludeIndex = m_lastSnapshotIncludeIndex;
  boostPersistRaftNode.m_lastSnapshotIncludeTerm = m_lastSnapshotIncludeTerm;
  for (auto &item : m_logs) {
    boostPersistRaftNode.m_logs.push_back(item.SerializeAsString());
  }

  std::stringstream ss;
  boost::archive::text_oarchive oa(ss);
  oa << boostPersistRaftNode;
  return ss.str();
}

void Raft::readPersist(std::string data) {
  if (data.empty()) {
    return;
  }
  std::stringstream iss(data);
  boost::archive::text_iarchive ia(iss);
  // read class state from archive
  BoostPersistRaftNode boostPersistRaftNode;
  ia >> boostPersistRaftNode;

  m_currentTerm = boostPersistRaftNode.m_currentTerm;
  m_votedFor = boostPersistRaftNode.m_votedFor;
  m_lastSnapshotIncludeIndex = boostPersistRaftNode.m_lastSnapshotIncludeIndex;
  m_lastSnapshotIncludeTerm = boostPersistRaftNode.m_lastSnapshotIncludeTerm;
  m_logs.clear();
  for (auto &item : boostPersistRaftNode.m_logs) {
    raftRpcProctoc::LogEntry logEntry;
    logEntry.ParseFromString(item);
    m_logs.emplace_back(logEntry);
  }
}

void Raft::Snapshot(int index, std::string snapshot) {
  std::lock_guard<std::mutex> lg(m_mtx);

  if (m_lastSnapshotIncludeIndex >= index || index > m_commitIndex) {
    DPrintf("[func-Snapshot-rf{%d}] rejects replacing log with snapshotIndex "
            "%d as current snapshotIndex %d is larger or "
            "smaller ",
            m_me, index, m_lastSnapshotIncludeIndex);
    return;
  }
  auto lastLogIndex =
      getLastLogIndex(); // 为了检查snapshot前后日志是否一样，防止多截取或者少截取日志

  // 制造完此快照后剩余的所有日志
  int newLastSnapshotIncludeIndex = index;
  int newLastSnapshotIncludeTerm =
      m_logs[getSlicesIndexFromLogIndex(index)].logterm();
  std::vector<raftRpcProctoc::LogEntry> trunckedLogs;
  // todo :这种写法有点笨，待改进，而且有内存泄漏的风险
  for (int i = index + 1; i <= getLastLogIndex(); i++) {
    // 注意有=，因为要拿到最后一个日志
    trunckedLogs.push_back(m_logs[getSlicesIndexFromLogIndex(i)]);
  }
  m_lastSnapshotIncludeIndex = newLastSnapshotIncludeIndex;
  m_lastSnapshotIncludeTerm = newLastSnapshotIncludeTerm;
  m_logs = trunckedLogs;
  m_commitIndex = std::max(m_commitIndex, index);
  m_lastApplied = std::max(m_lastApplied, index);

  // rf.lastApplied = index //lastApplied 和 commit应不应该改变呢？？？ 为什么
  // 不应该改变吧
  m_persister->Save(persistData(), snapshot);

  DPrintf("[SnapShot]Server %d snapshot snapshot index {%d}, term {%d}, loglen "
          "{%d}",
          m_me, index, m_lastSnapshotIncludeTerm, m_logs.size());
  myAssert(m_logs.size() + m_lastSnapshotIncludeIndex == lastLogIndex,
           format("len(rf.logs){%d} + rf.lastSnapshotIncludeIndex{%d} != "
                  "lastLogjInde{%d}",
                  m_logs.size(), m_lastSnapshotIncludeIndex, lastLogIndex));
}
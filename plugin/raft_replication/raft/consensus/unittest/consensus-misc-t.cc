// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "easyNet.h"
#include "fail_point.h"
#include "file_paxos_log.h"
#include "files.h"
#include "paxos.h"
#include "paxos.pb.h"
#include "paxos_configuration.h"
#include "paxos_log.h"
#include "paxos_server.h"
#include "rd_paxos_log.h"
#include "service.h"
#include "ut_helper.h"

using namespace alisql;

const int intervalMS = 1000;
const int timeoutMS = 4000;

TEST_F(CustomizedThreeNodesCluster, stateChangeCallback) {
  createNodes123();
  initNodes123();

  int stateChangeCb1Cnt = 0;
  int stateChangeCb2Cnt = 0;
  int stateChangeCb3Cnt = 0;

  auto cb1 = [&stateChangeCb1Cnt](Paxos* p, Paxos::StateType state,
                                  uint64_t term, uint64_t commitIndex) {
    std::cout << "State change to " << state
              << " in stateChangeCb1 term:" << term
              << " commitIndex:" << commitIndex << std::endl;
    EXPECT_EQ(p->getLocalServer()->strAddr, "127.0.0.1:11001");
    if (stateChangeCb1Cnt == 0) {
      // from follower to candidate
      EXPECT_EQ(state, Paxos::CANDIDATE);
      EXPECT_EQ(term, 2);
    } else if (stateChangeCb1Cnt == 1) {
      // from candidate to leader
      EXPECT_EQ(state, Paxos::LEADER);
      EXPECT_EQ(term, 2);
    } else if (stateChangeCb1Cnt == 2) {
      // from leader to follower
      EXPECT_EQ(state, Paxos::FOLLOWER);
      EXPECT_EQ(term, 3);
    } else if (stateChangeCb1Cnt == 3) {
      // from follower to follower
      EXPECT_EQ(state, Paxos::FOLLOWER);
      EXPECT_EQ(term, 4);
    }
    ++stateChangeCb1Cnt;
  };

  auto cb2 = [&stateChangeCb2Cnt](Paxos* p, Paxos::StateType state,
                                  uint64_t term, uint64_t commitIndex) {
    std::cout << "State change to " << state
              << " in stateChangeCb2 term:" << term
              << " commitIndex:" << commitIndex << std::endl;
    EXPECT_EQ(p->getLocalServer()->strAddr, "127.0.0.1:11002");
    ++stateChangeCb2Cnt;
  };

  auto cb3 = [&stateChangeCb3Cnt](Paxos::StateType state, uint64_t term,
                                  uint64_t commitIndex) {
    std::cout << "stateChangeCb3" << std::endl;
    ++stateChangeCb3Cnt;
  };

  node1->setStateChangeCb(cb1);
  node2->setStateChangeCb(cb2);
  node3->setStateChangeCb(cb3);

  ensureNode1Elected();

  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), 1, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), 1, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), 1, 3000);

  EXPECT_EQ(stateChangeCb1Cnt, 2);  // FOLLOWER -> CANDIDATE -> LEADER
  EXPECT_EQ(stateChangeCb2Cnt, 1);  // FOLLOWER -> FOLLOWER
  EXPECT_EQ(stateChangeCb3Cnt, 1);  // FOLLOWER -> FOLLOWER

  node2->requestVote();
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 5000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), 2, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), 2, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), 2, 3000);
  EXPECT_EQ(stateChangeCb1Cnt, 3);  // LEADER -> FOLLOWER
  EXPECT_EQ(stateChangeCb2Cnt, 3);  // FOLLOWER -> CANDIDATE -> LEADER
  EXPECT_EQ(stateChangeCb3Cnt, 2);  // FOLLOWER -> FOLLOWER

  node3->requestVote();
  EXPECT_EQ_EVENTUALLY(node3->getState(), Paxos::LEADER, 5000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), 3, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), 3, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), 3, 3000);
  EXPECT_EQ(stateChangeCb1Cnt, 4);  // FOLLOWER -> FOLLOWER
  EXPECT_EQ(stateChangeCb2Cnt, 4);  // LEADER -> FOLLOWER
  EXPECT_EQ(stateChangeCb3Cnt, 4);  // FOLLOWER -> CANDIDATE -> LEADER
}

TEST_F(ThreeNodesCluster, nodeStats) {
  const Paxos::StatsType& node1Stats = node1->getStats();
  EXPECT_EQ(node1Stats.countReplicateLog, 1);

  for (uint64_t i = 0; i < 3; i++) {
    t.replicateLogEntry(1, 1, "aaa");
    EXPECT_EQ_EVENTUALLY(node1Stats.countReplicateLog, i + 2, 1000);
  }

  auto oldHbCnt = node1Stats.countHeartbeat.load();
  EXPECT_TRUE_EVENTUALLY(node1Stats.countHeartbeat > oldHbCnt, 1000);
}

TEST_F(ThreeNodesCluster, waitCommitIndexUpdate_return_when_not_leader) {
  std::thread thr([&]() {
    // it waits until node1 is no longer leader
    node1->waitCommitIndexUpdate(10, 3);
    EXPECT_LT(node1->getCommitIndex(), 10);
    EXPECT_LT(node1->getTerm(), 3);
  });

  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       1000);
  node2->requestVote();
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 5000);

  thr.join();
}

// TODO replace this class with failpoint
std::atomic<uint64_t> failCnt(0);
class FailRDPaxosLog : public RDPaxosLog {
 public:
  FailRDPaxosLog(const std::string& dataDir, bool compress,
                 size_t writeBufferSize, bool sync = true)
      : RDPaxosLog(dataDir, compress, writeBufferSize, sync){};
  virtual int getEntry(uint64_t logIndex, LogEntry& entry,
                       bool fastfail = false) {
    if (failCnt > 0) {
      --failCnt;
      std::cout << "getEntry will return fail." << std::endl << std::flush;
      return 1;
    } else {
      return RDPaxosLog::getEntry(logIndex, entry);
    }
  }
};

TEST(CustomizedCluster, AliSQLServer_writeLogDone) {
  PaxosTestbed t;
  std::vector<Paxos::ClusterInfoType> cis;

  auto rlog1 = std::make_shared<FailRDPaxosLog>("paxosLogTestDir1", true,
                                                4 * 1024 * 1024);
  rlog1->set_debug_async();
  Paxos* node1 = t.addNode(new Paxos(2000, rlog1)).getLastNode();
  auto aserver1 = std::make_shared<AliSQLServer>(1);
  node1->init(t.getNodesIpAddrVectorRef2(1, 2), 1, NULL, 2, 2, aserver1);

  auto rlog2 = std::make_shared<FailRDPaxosLog>("paxosLogTestDir2", true,
                                                4 * 1024 * 1024);
  Paxos* node2 = t.addNode(new Paxos(2000, rlog2)).getLastNode();
  node2->init(t.getNodesIpAddrVectorRef2(1, 2), 2);

  auto rlog3 = std::make_shared<FailRDPaxosLog>("paxosLogTestDir3", true,
                                                4 * 1024 * 1024);
  rlog3->set_debug_async();
  Paxos* node3 = t.addNode(new Paxos(2000, rlog3)).getLastNode();
  auto aserver3 = std::make_shared<AliSQLServer>(1);
  node3->initAsLearner(t.getNodeIpAddrRef(3), nullptr, 2, 2, aserver3);

  EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(2)), true, 2000);
  node1->requestVote();
  EXPECT_EQ_EVENTUALLY(node1->getState(), alisql::Paxos::LEADER, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node2->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);

  auto baseLogIndex = node1->getLastLogIndex();
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseLogIndex, 1000);

  t.replicateLogEntry(1, 1, "aaa", kNop);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseLogIndex + 1, 1000);
  t.replicateLogEntry(1, 1, "aaa", kNop);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseLogIndex + 2, 1000);

  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), baseLogIndex, 500, 100);

  aserver1->writeLogDone(rlog1->getLastLogIndex());
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseLogIndex + 2, 1000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseLogIndex + 2, 1000);

  Paxos::MemberInfoType mi;
  node1->getMemberInfo(&mi);
  EXPECT_EQ(mi.serverId, 1);
  EXPECT_EQ(mi.currentTerm, 2);
  EXPECT_EQ(mi.currentLeader, 1);
  EXPECT_EQ(mi.commitIndex, baseLogIndex + 2);
  EXPECT_EQ(mi.lastLogTerm, 2);
  EXPECT_EQ(mi.lastLogIndex, baseLogIndex + 2);
  EXPECT_EQ(mi.role, Paxos::LEADER);

  node2->getMemberInfo(&mi);
  EXPECT_EQ(mi.serverId, 2);
  EXPECT_EQ(mi.currentTerm, 2);
  EXPECT_EQ(mi.currentLeader, 1);
  EXPECT_EQ(mi.commitIndex, baseLogIndex + 2);
  EXPECT_EQ(mi.lastLogTerm, 2);
  EXPECT_EQ(mi.lastLogIndex, baseLogIndex + 2);
  EXPECT_EQ(mi.role, Paxos::FOLLOWER);

  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(3));
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseLogIndex + 3, 1000);
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), baseLogIndex + 3, 1000);

  node3->forceSingleLeader();
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), baseLogIndex + 4, 1000);

  t.replicateLogEntry(3, 1, "aaa", kNop);
  aserver3->writeLogDone(node3->getLastLogIndex());
  EXPECT_EQ(node3->getCommitIndex(), baseLogIndex + 5);

  // will send one appendLog message to node 2 with no log entry
  failCnt.store(1, std::memory_order_relaxed);
  t.replicateLogEntry(1, 1, "aaa", kNop);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseLogIndex + 4, 1000);
  EXPECT_EQ(node1->getCommitIndex(), baseLogIndex + 3);

  t.replicateLogEntry(1, 1, "aaa", kNop);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseLogIndex + 5, 1000);
  EXPECT_EQ(node1->getCommitIndex(), baseLogIndex + 3);

  aserver1->writeLogDone(node1->getLastLogIndex());
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseLogIndex + 5, 1000);
}

TEST_F(ThreeNodesCluster, checksum) {
  std::function<uint32_t(uint32_t, const unsigned char*, size_t)> checksumFunc =
      [](uint32_t base, const unsigned char* buf, size_t len) -> uint32_t {
    return base + len;
  };
  node1->setChecksumCb(checksumFunc);
  node2->setChecksumCb(checksumFunc);
  node3->setChecksumCb(checksumFunc);

  std::cout << "enable checksum..." << std::endl;
  node1->setChecksumMode(true);
  node2->setChecksumMode(true);
  node3->setChecksumMode(true);

  LogEntry le;
  le.set_index(0);
  le.set_optype(1);
  le.set_value("12345");
  le.set_checksum(0);
  node1->replicateLog(le);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node3->getLastLogIndex(),
                       1000);

  le.set_index(0);
  le.set_value("12345");
  le.set_checksum(6);  // 5 is correct
  node1->replicateLog(le);
  for (int i = 0; i < 10; ++i) {
    le.set_index(0);
    le.set_value("12345");
    le.set_checksum(0);
    node1->replicateLog(le);
  }
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex() + 11,
                       1000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node3->getLastLogIndex() + 11,
                       1000);

  std::cout << "disable checksum..." << std::endl;
  node1->setChecksumMode(false);
  node2->setChecksumMode(false);
  node3->setChecksumMode(false);

  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node3->getLastLogIndex(),
                       1000);
}

TEST_F(CustomizedThreeNodesCluster, memory_usage_count) {
  createNodes123();
  t.setEnableMemoryUsageCount(true);
  initNodes123();
  ensureNode1Elected();

  t.replicateLogEntry(1, 10, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node3->getLastLogIndex(),
                       1000);
  // memory usage not equal to 0
  EXPECT_NE(easy_pool_alloc_byte, 0);
  std::cout << "easy_pool_alloc_byte: " << easy_pool_alloc_byte << std::endl;

  t.destroyNode(1);
  t.destroyNode(2);
  t.destroyNode(3);

  // memory usage go back to 0
  EXPECT_EQ(easy_pool_alloc_byte, 0);
  std::cout << "easy_pool_alloc_byte: " << easy_pool_alloc_byte << std::endl;
}

TEST_F(CustomizedThreeNodesCluster, ExtraStore) {
  createNodes123();
  initNodes123();

  class MockExtraStore : public ExtraStore {
   public:
    MockExtraStore(uint p) : port(p) {}
    virtual std::string getRemote() { return data; }
    virtual void setRemote(const std::string& d) { data = d; }
    /* local info to send to others */
    virtual std::string getLocal() { return std::to_string(port); }

    uint getRemotePort() { return data != "" ? stoul(data) : 0; }

   private:
    uint port;
    std::string data;
  };
  node1->setExtraStore(
      std::make_shared<MockExtraStore>(node1->getPort() - 8000));
  node2->setExtraStore(
      std::make_shared<MockExtraStore>(node2->getPort() - 8000));
  node3->setExtraStore(
      std::make_shared<MockExtraStore>(node3->getPort() - 8000));

  ensureNode1Elected();

  auto baseIdx = node1->getLastLogIndex();

  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx, 1000);
  EXPECT_EQ_EVENTUALLY(node3->getLastLogIndex(), baseIdx, 1000);

  /* every follower will have port-8000 from leader node1 */
  EXPECT_EQ(std::dynamic_pointer_cast<MockExtraStore>(node1->getExtraStore())
                ->getRemotePort(),
            node1->getPort() - 8000);
  EXPECT_EQ(std::dynamic_pointer_cast<MockExtraStore>(node2->getExtraStore())
                ->getRemotePort(),
            node1->getPort() - 8000);
  EXPECT_EQ(std::dynamic_pointer_cast<MockExtraStore>(node3->getExtraStore())
                ->getRemotePort(),
            node1->getPort() - 8000);

  node2->requestVote();
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 1000);
  // newTerm will clean option.ExtraStore,
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 1, 1000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 1, 1000);
  EXPECT_EQ_EVENTUALLY(node3->getLastLogIndex(), baseIdx + 1, 1000);

  /* every follower will have port-8000 from new leader node2 */
  EXPECT_EQ(std::dynamic_pointer_cast<MockExtraStore>(node1->getExtraStore())
                ->getRemotePort(),
            node2->getPort() - 8000);
  EXPECT_EQ(std::dynamic_pointer_cast<MockExtraStore>(node2->getExtraStore())
                ->getRemotePort(),
            node2->getPort() - 8000);
  EXPECT_EQ(std::dynamic_pointer_cast<MockExtraStore>(node3->getExtraStore())
                ->getRemotePort(),
            node2->getPort() - 8000);
}

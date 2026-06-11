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

#ifdef FAIL_POINT
TEST_F(ThreeNodesCluster, ConsensusAsync_get_commit_index) {
  auto baseIdx = node1->getLastLogIndex();
  t.replicateLogEntry(1, 2, "aaa");

  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 2, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 2, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getLastLogIndex(), baseIdx + 2, 3000);

  // leader commit no need majority case:
  FailPointData data(baseIdx + 2);
  auto fpLimitMaxSendLogIndex =
      FailPointRegistry::getGlobalFailPointRegistry().find(
          "LimitMaxSendLogIndex");
  fpLimitMaxSendLogIndex->activate(FailPoint::alwaysOn, 0, data);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 3, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 2, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getLastLogIndex(), baseIdx + 2, 3000);
  EXPECT_EQ(node1->getCommitIndex(), baseIdx + 2);

  std::atomic<bool> stopped(false);
  std::thread th([&]() {
    // wait until commitIndex exceeds baseIdx + 2
    node1->waitCommitIndexUpdate(baseIdx + 2);
    stopped.store(true);
  });

  EXPECT_EQ_CONSISTENTLY(stopped.load(), false, 500, 100);
  node1->setConsensusAsync(1);
  EXPECT_EQ_EVENTUALLY(stopped.load(), true, 3000);
  EXPECT_EQ(node1->getCommitIndex(), baseIdx + 3);
  EXPECT_EQ(node1->getLastLogIndex(), baseIdx + 3);
  EXPECT_EQ(node2->getLastLogIndex(), baseIdx + 2);
  EXPECT_EQ(node3->getLastLogIndex(), baseIdx + 2);

  // follower lost log case:
  fpLimitMaxSendLogIndex->deactivate();
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 3, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 3, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getLastLogIndex(), baseIdx + 3, 3000);

  th.join();
}
#endif

TEST_F(FiveNodesCluster, ConsensusAsync_get_commit_index) {
  auto baseIdx = node1->getLastLogIndex();

  t.replicateLogEntry(1, 2, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 2, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 2, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getLastLogIndex(), baseIdx + 2, 3000);
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), baseIdx + 2, 3000);
  EXPECT_EQ_EVENTUALLY(node5->getLastLogIndex(), baseIdx + 2, 3000);

  t.destroyNode(3);
  t.destroyNode(4);
  t.destroyNode(5);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 3, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 3, 3000);

  // check leader
  std::atomic<bool> leaderCommitted(false);
  std::thread th1([&]() {
    // wait for commitIndex surpasses (baseIdx + 2)
    node1->waitCommitIndexUpdate(baseIdx + 2);
    leaderCommitted.store(true);
  });
  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), baseIdx + 2, 500, 100);
  node1->setConsensusAsync(1);
  EXPECT_EQ(node1->getCommitIndex(), baseIdx + 3);
  EXPECT_EQ_EVENTUALLY(leaderCommitted.load(), true, 1000);

  // check follower
  EXPECT_EQ(node2->getCommitIndex(), baseIdx + 2);
  std::atomic<bool> followerCommitted(false);
  std::thread th2([&]() {
    node2->waitCommitIndexUpdate(baseIdx + 2);
    followerCommitted.store(true);
  });
  EXPECT_EQ_CONSISTENTLY(node2->getCommitIndex(), baseIdx + 2, 500, 100);
  node2->setConsensusAsync(1);
  EXPECT_EQ(node2->getCommitIndex(), baseIdx + 2);
  EXPECT_EQ_EVENTUALLY(followerCommitted.load(), false, 1000);

  // force destroy to ensure th1 and th2 complete
  t.destroyNode(1);
  t.destroyNode(2);

  th1.join();
  th2.join();
}

TEST_F(ThreeNodesCluster, ConsensusAsync_reset_match_index) {
  auto baseIdx = node1->getLastLogIndex();
  t.replicateLogEntry(1, 3, "aaa");

  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 3, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 3, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getLastLogIndex(), baseIdx + 3, 3000);

  auto log2 = node2->getLog();
  t.destroyNode(2);
  // leaving log entries with index less than baseIdx + 3
  log2->truncateBackward(baseIdx + 3);
  node2 = t.addNodeAtIndex(2, new Paxos(2000, log2))
              .initAsMember(2, t.getNodesIpAddrVectorRef3(1, 2, 3));
  EXPECT_EQ(node2->getLastLogIndex(), baseIdx + 2);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 4, 1000);
  EXPECT_EQ_CONSISTENTLY(node2->getLastLogIndex(), baseIdx + 2, 500, 100);

  node1->setConsensusAsync(1);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 4, 3000);
}

TEST_F(FiveNodesCluster, ConsensusAsync_configure_change_pending) {
  auto baseIdx = node1->getLastLogIndex();

  t.destroyNode(3);
  t.destroyNode(4);
  t.destroyNode(5);

  // check leader configure change sync
  std::vector<Paxos::ClusterInfoType> cis;
  node1->getClusterInfo(cis);
  EXPECT_EQ(cis.size(), 5);
  EXPECT_EQ(cis[1].electionWeight, 5);
  std::thread th1([&]() { node1->configureMember(2, false, 8); });

  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 1, 2000);
  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), baseIdx, 500, 100);
  EXPECT_EQ(node1->getConfigureChangePrepared(), true);
  EXPECT_EQ(node1->getConfigureChangePreparedIndex(), baseIdx + 1);

  node1->setConsensusAsync(1);

  EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(
      [&]() {
        node1->getClusterInfo(cis);
        EXPECT_EQ(cis[1].electionWeight, 5);
        return cis[1].electionWeight == 5 &&
               node1->getCommitIndex() == baseIdx + 1 &&
               node1->getState() == Paxos::LEADER;
      },
      3000);

  EXPECT_EQ(node1->getConfigureChangePrepared(), true);
  EXPECT_EQ(node1->getConfigureChangePreparedIndex(), baseIdx + 1);

  t.destroyNode(1);
  t.destroyNode(2);

  th1.join();
}

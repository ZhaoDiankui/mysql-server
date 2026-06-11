// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include <iostream>
#include <string>

#include "fail_point.h"
#include "files.h"
#include "paxos.h"
#include "rd_paxos_log.h"
#include "ut_helper.h"

const int intervalMS = 100;
const int timeoutMS = 2000;

using namespace alisql;

TEST_F(ThreeNodesCluster, ConfigureMember_ok) {
  // successfully set
  EXPECT_EQ(node1->configureMember(1, true, 9), 0);
  EXPECT_EQ(node1->configureMember(2, true, 8), 0);
  EXPECT_EQ(node1->configureMember(3, true, 7), 0);

  std::vector<Paxos::ClusterInfoType> cis;
  node1->getClusterInfo(cis);
  EXPECT_EQ(cis.size(), 3);
  EXPECT_EQ(cis[0].electionWeight, 9);
  EXPECT_EQ(cis[0].forceSync, true);
  EXPECT_EQ(cis[1].electionWeight, 8);
  EXPECT_EQ(cis[1].forceSync, true);
  EXPECT_EQ(cis[2].electionWeight, 7);
  EXPECT_EQ(cis[2].forceSync, true);
}

// change the electionWeight to a number that is not allowed
TEST_F(SingleNodeCluster, ConfigureMember_invalid_argument) {
  auto ret = node1->configureMember(1, true, 10);
  EXPECT_EQ(ret, PaxosErrorCode::PE_INVALIDARGUMENT);

  // electionWeight and forceSync didn't change
  std::vector<Paxos::ClusterInfoType> cis;
  node1->getClusterInfo(cis);
  EXPECT_EQ(cis.size(), 1);
  EXPECT_EQ(cis[0].electionWeight, 5);
  EXPECT_EQ(cis[0].forceSync, false);
}

// change a non-existing member's electionWeight and forceSync
TEST_F(SingleNodeCluster, ConfigureMember_not_found) {
  auto ret = node1->configureMember(2, true, 9);
  EXPECT_EQ(ret, PaxosErrorCode::PE_NOTFOUND);

  // electionWeight and forceSync didn't change
  std::vector<Paxos::ClusterInfoType> cis;
  node1->getClusterInfo(cis);
  EXPECT_EQ(cis.size(), 1);
  EXPECT_EQ(cis[0].electionWeight, 5);
  EXPECT_EQ(cis[0].forceSync, false);
}

// change nothing
TEST_F(SingleNodeCluster, ConfigureMember_no_change) {
  auto ret = node1->configureMember(1, false, 5);
  EXPECT_EQ(ret, PaxosErrorCode::PE_NONE);

  // electionWeight and forceSync didn't change
  std::vector<Paxos::ClusterInfoType> cis;
  node1->getClusterInfo(cis);
  EXPECT_EQ(cis.size(), 1);
  EXPECT_EQ(cis[0].electionWeight, 5);
  EXPECT_EQ(cis[0].forceSync, false);
}

// change the learner's electionWeight and forceSync
TEST_F(SingleNodeCluster, ConfigureMember_weight_learner) {
  Paxos* node2 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(2));
  EXPECT_EQ_EVENTUALLY(node1->getClusterSize(), 2, 2000);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEARNER, 2000);

  auto ret = node1->configureMember(t.getNodeIpAddrRef(2), true, 9);
  EXPECT_EQ(ret, PaxosErrorCode::PE_WEIGHTLEARNER);

  // electionWeight and forceSync didn't change
  std::vector<Paxos::ClusterInfoType> cis;
  node1->getClusterInfo(cis);
  EXPECT_EQ(cis.size(), 2);
  EXPECT_EQ(cis[1].electionWeight, 5);
  EXPECT_EQ(cis[1].forceSync, false);
}

#ifdef FAIL_POINT
// change a delayed follower's forceSync,
// upgrade the learner node2 to a follower
TEST_F(ThreeNodesCluster, ConfigureMember_delay_toomuch) {
  auto baseIdx = node1->getCommitIndex();
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx, 2000);

  node1->setMaxDelayIndex4ForceSync(1);
  // isolate node2
  FailPointData dataWith2((uint64_t)2);
  auto fp = FailPointRegistry::getGlobalFailPointRegistry().find(
      "isolateAFollowerBothWay");
  ASSERT_TRUE(fp != NULL);
  fp->activate(FailPoint::alwaysOn, 0, dataWith2);

  t.replicateLogEntry(1, 2, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 2, 2000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx, 2000);
  auto ret = node1->configureMember(t.getNodeIpAddrRef(2), true, 9);
  EXPECT_EQ(ret, PaxosErrorCode::PE_DELAY);

  fp->deactivate();
  node1->setMaxDelayIndex4ForceSync(100);
}
#endif

TEST_F(ThreeNodesCluster, LearnerAndMemberRoleSwitches) {
  t.replicateLogEntry(1, 1, "aaa");

  // Test Learner
  Paxos* node4 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       2000);
  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       2000);

  // Transfer Leader to Server 2 success case
  node1->leaderTransfer(2);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 2000);

  // Test if the learners can still recieve msgs from new leader.
  t.replicateLogEntry(2, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node2->getLastLogIndex(),
                       2000);

  // Remove Learner
  node2->changeLearners(Paxos::CCDelNode, t.getNodesIpAddrVectorRef1(4));
  t.replicateLogEntry(2, 1, "aaa");
  EXPECT_TRUE_CONSISTENTLY(node4->getLastLogIndex() < node2->getLastLogIndex(),
                           500, 100);

  // create 2 learners
  Paxos* node5 = t.createRDLogNode().initAsLearner();
  Paxos* node6 = t.createRDLogNode().initAsLearner();
  node2->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef2(5, 6));
  EXPECT_EQ_EVENTUALLY(node2->getServerNum(), 3, 2000);
  EXPECT_EQ(
      node2->membersToString(("127.0.0.1:11002")),
      std::string("127.0.0.1:11001#5;127.0.0.1:11002#5;127.0.0.1:11003#5@2"));
  EXPECT_EQ_EVENTUALLY(node2->getLearnerNum(), 2, 2000);
  EXPECT_EQ(node2->learnersToString(),
            std::string("127.0.0.1:11005$0;127.0.0.1:11006$0"));
  EXPECT_EQ_EVENTUALLY(node5->getLastLogIndex(), node2->getLastLogIndex(),
                       2000);
  EXPECT_EQ_EVENTUALLY(node6->getLastLogIndex(), node2->getLastLogIndex(),
                       2000);

  // change learners to followers one by one
  node2->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(5));
  t.replicateLogEntry(2, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node5->getState(), Paxos::FOLLOWER, 2000);
  EXPECT_EQ_EVENTUALLY(node2->getServerNum(), 4, 2000);
  EXPECT_EQ_EVENTUALLY(node2->getLearnerNum(), 1, 2000);
  node2->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(6));
  node2->configureMember(5, true, 9);
  t.replicateLogEntry(2, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node6->getState(), Paxos::FOLLOWER, 2000);
  EXPECT_EQ_EVENTUALLY(node2->getServerNum(), 5, 2000);
  EXPECT_EQ_EVENTUALLY(
      node2->membersToString(("127.0.0.1:11002")),
      std::string("127.0.0.1:11001#5;127.0.0.1:11002#5;127.0.0.1:11003#5;"
                  "127.0.0.1:11005#5;127.0.0.1:11006#9S@2"),
      2000);
  EXPECT_EQ_EVENTUALLY(node2->getLearnerNum(), 0, 2000);
  EXPECT_EQ_EVENTUALLY(node2->learnersToString(), std::string(""), 2000);
  EXPECT_EQ_EVENTUALLY(
      node6->membersToString(node6->getLocalServer()->strAddr),
      std::string("127.0.0.1:11001#5;127.0.0.1:11002#5;127.0.0.1:11003#5;"
                  "127.0.0.1:11005#5;127.0.0.1:11006#9S@5"),
      2000);
  std::vector<Paxos::ClusterInfoType> cis;
  node2->getClusterInfo(cis);
  EXPECT_EQ(cis[3].ipPort, t.getNodeIpAddr(5));
  EXPECT_EQ(cis[3].forceSync, false);
  EXPECT_EQ(cis[3].electionWeight, 5);
  EXPECT_EQ(cis[4].ipPort, t.getNodeIpAddr(6));
  EXPECT_EQ(cis[4].forceSync, true);
  EXPECT_EQ(cis[4].electionWeight, 9);
  // check 2 new followers
  EXPECT_EQ_EVENTUALLY(node5->getLastLogIndex(), node2->getLastLogIndex(),
                       2000);
  EXPECT_EQ_EVENTUALLY(node6->getLastLogIndex(), node2->getLastLogIndex(),
                       2000);

  // delete one follower
  node2->changeMember(Paxos::CCDelNode, t.getNodeIpAddrRef(5));
  t.replicateLogEntry(2, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node2->getServerNum(), 4, 2000);
  EXPECT_EQ_EVENTUALLY(node2->getLearnerNum(), 0, 2000);
  EXPECT_EQ_EVENTUALLY(node2->learnersToString(), std::string(""), 2000);
  node2->getClusterInfo(cis);
  EXPECT_EQ(cis[3].ipPort, t.getNodeIpAddr(6));
  EXPECT_TRUE_CONSISTENTLY(node5->getLastLogIndex() < node2->getLastLogIndex(),
                           500, 100);
  EXPECT_EQ_EVENTUALLY(node6->getLastLogIndex(), node2->getLastLogIndex(),
                       2000);

  // Transfer Leader to Server 3 fail case
  t.destroyNode(3);
  node2->leaderTransfer(3);
  EXPECT_EQ(node2->getState(), Paxos::LEADER);
  EXPECT_EQ(node2->getSubState(), Paxos::SubLeaderTransfer);
  EXPECT_EQ_CONSISTENTLY(node2->getState(), Paxos::LEADER, 1000, 100);
  EXPECT_EQ_EVENTUALLY(node2->getSubState(), Paxos::SubNone, 2000);

  t.destroyNode(1);
  t.destroyNode(5);
  t.destroyNode(6);

  // Leader stepdown.
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::FOLLOWER, 10000);
}

TEST_F(TwoNodesCluster, LearnerAndMemberRoleSwitches2) {
  std::vector<Paxos::ClusterInfoType> cis;
  node1->getClusterInfo(cis);
  EXPECT_EQ(cis.size(), 2);
  EXPECT_EQ(cis[0].ipPort, std::string("127.0.0.1:11001"));
  EXPECT_EQ(cis[0].serverId, 1);
  EXPECT_EQ(cis[0].role, Paxos::LEADER);
  EXPECT_EQ(cis[1].ipPort, std::string("127.0.0.1:11002"));
  EXPECT_EQ(cis[1].serverId, 2);
  EXPECT_EQ(cis[1].role, Paxos::FOLLOWER);

  Paxos* node3 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(3));
  node1->getClusterInfo(cis);
  EXPECT_EQ(cis[0].serverId, 1);
  EXPECT_EQ(cis[0].role, Paxos::LEADER);
  EXPECT_EQ(cis[1].serverId, 2);
  EXPECT_EQ(cis[1].role, Paxos::FOLLOWER);
  EXPECT_EQ(cis[2].serverId, 101);
  EXPECT_EQ(cis[2].role, Paxos::LEARNER);

  EXPECT_EQ(0, node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(3)));
  node1->getClusterInfo(cis);
  EXPECT_EQ(cis[0].serverId, 1);
  EXPECT_EQ(cis[0].role, Paxos::LEADER);
  EXPECT_EQ(cis[1].serverId, 2);
  EXPECT_EQ(cis[1].role, Paxos::FOLLOWER);
  EXPECT_EQ(cis[2].serverId, 3);
  EXPECT_EQ(cis[2].role, Paxos::FOLLOWER);

  EXPECT_EQ(0, node1->downgradeMember(3));
  node1->getClusterInfo(cis);
  EXPECT_EQ(cis.size(), 3);
  EXPECT_EQ(cis[0].serverId, 1);
  EXPECT_EQ(cis[0].role, Paxos::LEADER);
  EXPECT_EQ(cis[1].serverId, 2);
  EXPECT_EQ(cis[1].role, Paxos::FOLLOWER);
  EXPECT_EQ(cis[2].serverId, 101);
  EXPECT_EQ(cis[2].role, Paxos::LEARNER);

  // Need wait node3 becomes a learner before executing forceSingleLeader, or
  // node3 may become a leader first and then downgrade to a learner.
  EXPECT_EQ_EVENTUALLY(node3->getState(), Paxos::LEARNER, 3000);
  node3->forceSingleLeader();
  EXPECT_EQ_EVENTUALLY(node3->getState(), Paxos::LEADER, 3000);
  EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(
      [&] {
        node3->getClusterInfo(cis);
        return cis.size() == 1 && cis[0].serverId == 1 &&
               cis[0].role == Paxos::LEADER;
      },
      2000);
}

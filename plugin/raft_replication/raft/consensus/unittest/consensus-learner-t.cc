// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "easyNet.h"
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

TEST_F(ThreeNodesCluster, getServerIdFromAddr) {
  std::ignore = t.createRDLogNode().initAsLearner();
  std::ignore = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef2(4, 5));
  EXPECT_EQ_EVENTUALLY(
      node1->membersToString(("127.0.0.1:11002")),
      std::string("127.0.0.1:11001#5;127.0.0.1:11002#5;127.0.0.1:11003#5@2"),
      3000);
  EXPECT_EQ_EVENTUALLY(node1->learnersToString(),
                       std::string("127.0.0.1:11004$0;127.0.0.1:11005$0"),
                       3000);
  /* test getServerIdFromAddr */
  EXPECT_EQ(node1->getServerIdFromAddr("127.0.0.1:11001"), 1);
  EXPECT_EQ(node1->getServerIdFromAddr("127.0.0.1:11002"), 2);
  EXPECT_EQ(node1->getServerIdFromAddr("127.0.0.1:11003"), 3);
  EXPECT_EQ(node1->getServerIdFromAddr("127.0.0.1:11004"), 101);
  EXPECT_EQ(node1->getServerIdFromAddr("127.0.0.1:11005"), 102);
}

TEST_F(ThreeNodesCluster, configure_learner_source) {
  Paxos* node4 = t.createRDLogNode().initAsLearner();
  Paxos* node5 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef2(4, 5));

  // set the learnerSource of node4 to node3 (of index 2)
  node1->configureLearner(101, 2);
  EXPECT_EQ(node1->getLearnerSource(101), 2);
  EXPECT_EQ_EVENTUALLY(node2->getLearnerSource(101), 2, 1000);
  EXPECT_EQ_EVENTUALLY(node4->getCurrentLeader(), 2, 1000);
  EXPECT_EQ_EVENTUALLY(node5->getCurrentLeader(), 1, 1000);

  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       1000);
  node2->clearStats();
  t.replicateLogEntry(1, 2, "aaa");
  EXPECT_EQ_EVENTUALLY(node2->getStats().countOnMsgAppendLog, 2, 1000);
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node5->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);

  std::vector<Paxos::ClusterInfoType> cis;
  node1->getClusterInfo(cis);
  EXPECT_EQ(cis[0].learnerSource, 0);
  EXPECT_EQ(cis[3].learnerSource, 2);
  EXPECT_EQ(cis[4].learnerSource, 0);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);

  node1->configureLearner(101, 3);
  EXPECT_EQ_EVENTUALLY(node4->getCurrentLeader(), 3, 1000);
  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);

  // set the learnerSource of node4 to leader
  node1->configureLearner(101, 0);
  EXPECT_EQ_EVENTUALLY(node4->getCurrentLeader(), 1, 1000);
  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);

  // Already fixed! Error may happy when server 1 is leader, and we change
  // learnerSource from 1 to 0.
  node1->configureLearner(101, 1);
  EXPECT_EQ_EVENTUALLY(node4->getCurrentLeader(), 1, 1000);
  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);

  // set the learnerSource of node4 to leader
  node1->configureLearner(101, 0);
  EXPECT_EQ_EVENTUALLY(node4->getCurrentLeader(), 1, 1000);
  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);

  // set the learnerSource to another learner
  node1->configureLearner(101, 102);
  EXPECT_EQ_EVENTUALLY(node4->getCurrentLeader(), 102, 1000);
  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);

  // check if the new added member has the forceSync electionWeight and
  // learnerSource info.
  node1->configureMember(2, false, 8);
  node1->configureLearner(101, 2);

  // learn configuration from new learner
  Paxos* node6 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(6));
  node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(6));
  EXPECT_EQ_EVENTUALLY(node6->getCurrentLeader(), 1, 6000);
  EXPECT_EQ_EVENTUALLY(node6->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);
  EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(
      [&]() {
        node6->getClusterInfo(cis);
        return cis[1].electionWeight == 8 && cis[4].learnerSource == 2;
      },
      1000);
}

TEST_F(ThreeNodesCluster, follower_to_learner) {
  Paxos* node4 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));
  EXPECT_EQ(0, node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(4)));

  EXPECT_EQ(0, node1->downgradeMember(4));
  EXPECT_EQ_EVENTUALLY(Paxos::LEARNER, node4->getState(), 1000);
  std::string tmpStr;
  node4->getLog()->getMetaData(std::string(Paxos::keyMemberConfigure), tmpStr);
  EXPECT_EQ(tmpStr, std::string("127.0.0.1:11004#5"));
  node4->getLog()->getMetaData(std::string(Paxos::keyLearnerConfigure), tmpStr);
  EXPECT_EQ(tmpStr, std::string("127.0.0.1:11004$0"));

  EXPECT_EQ(0, node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(4)));
  EXPECT_EQ_EVENTUALLY(Paxos::FOLLOWER, node4->getState(), 1000);

  EXPECT_EQ(0, node1->downgradeMember(4));
  EXPECT_EQ_EVENTUALLY(Paxos::LEARNER, node4->getState(), 1000);
  std::vector<Paxos::ClusterInfoType> cis;
  node1->getClusterInfo(cis);
  EXPECT_EQ(101, cis[3].serverId);
  EXPECT_EQ("127.0.0.1:11004", cis[3].ipPort);

  EXPECT_EQ(0, node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(4)));
  EXPECT_EQ_EVENTUALLY(Paxos::LEARNER, node4->getState(), 1000);

  // can't downgrade a learner
  EXPECT_EQ(PaxosErrorCode::PE_DOWNGRADLEARNER, node1->downgradeMember(101));

  node1->leaderTransfer(4);
  EXPECT_EQ_EVENTUALLY(node4->getState(), Paxos::LEADER, 2000);
}

TEST_F(ThreeNodesCluster, downgraded_follower_do_not_vote) {
  EXPECT_EQ(node1->downgradeMember(2), 0);
  // ensure node3 knows node2 is downgraded
  // another case is downgraded_follower_vote_before_downgrade,
  // which shows when both node2 and node3 doesn't know node2 is downgraded
  // node2 will be elected as leader but downgraded soon when it applies
  // last memebership change log entries.
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), node1->getCommitIndex(), 1000);

  auto remoteServer = node1->getConfig()->getServer(101);
  EXPECT_NE(remoteServer, nullptr);
  remoteServer->disconnect(nullptr);

  // the conneciton will be re-established when the leader replies
  node2->requestVote(true);
  EXPECT_EQ_CONSISTENTLY(node1->getState(), Paxos::LEADER, 500, 100);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEARNER, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getCurrentLeader(), 1, 3000);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), node1->getLastLogIndex(),
                       6000);
}

TEST_F(ThreeNodesCluster, restart_learner) {
  Paxos* node4 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));

  // restart node4
  t.destroyNode(4);
  node4 = t.createRDLogNodeAtIndex(4).initAsLearner(4);
  EXPECT_LOG_GET_METADATA_EQ(node4, Paxos::keyMemberConfigure,
                             "127.0.0.1:11004#5", 1000);
  EXPECT_LOG_GET_METADATA_EQ(node4, Paxos::keyLearnerConfigure,
                             "127.0.0.1:11004$0", 1000);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);

  // add node4 in the middle
  Paxos* node5 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(5));

  const std::string ExpectedLearners = "127.0.0.1:11004$0;127.0.0.1:11005$0";
  // restart node4 again
  t.destroyNode(4);
  node4 = t.createRDLogNodeAtIndex(4).getNode(4);
  node4->initAsLearner(t.getEmptyNodeIpAddrRef());
  EXPECT_LOG_GET_METADATA_EQ(node4, Paxos::keyMemberConfigure,
                             "127.0.0.1:11004#5", 1000);
  EXPECT_LOG_GET_METADATA_EQ(node4, Paxos::keyLearnerConfigure,
                             ExpectedLearners, 1000);
  EXPECT_EQ_EVENTUALLY(node1->learnersToString(), ExpectedLearners, 1000);
  EXPECT_EQ_EVENTUALLY(node2->learnersToString(), ExpectedLearners, 1000);
  EXPECT_EQ_EVENTUALLY(node4->learnersToString(), ExpectedLearners, 1000);
  EXPECT_EQ_EVENTUALLY(node5->learnersToString(), ExpectedLearners, 1000);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);
}

TEST_F(ThreeNodesCluster, restart_follower) {
  EXPECT_EQ(node1->downgradeMember(3), 0);
  EXPECT_EQ_EVENTUALLY(node3->getState(), Paxos::LEARNER, 1000);

  // restart node2
  t.destroyNode(2);
  node2 = t.createRDLogNodeAtIndex(2).initAsMember(
      2, t.getNodesIpAddrVectorRef3(1, 2, 3));
  EXPECT_LOG_GET_METADATA_EQ(node2, Paxos::keyMemberConfigure,
                             "127.0.0.1:11001#5;127.0.0.1:11002#5@2", 1000);
  EXPECT_LOG_GET_METADATA_EQ(node2, Paxos::keyLearnerConfigure,
                             "127.0.0.1:11003$0", 1000);
}

TEST_F(ThreeNodesCluster, configureChange_affect_learner_meta) {
  std::vector<Paxos::ClusterInfoType> cis;

  Paxos* node4 = t.createRDLogNode().initAsLearner();  // 101
  Paxos* node5 = t.createRDLogNode().initAsLearner();  // 102
  std::ignore = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef2(4, 5));
  EXPECT_EQ_EVENTUALLY(node4->learnersToString(),
                       "127.0.0.1:11004$0;127.0.0.1:11005$0", 1000);

  // follower -> learner, 103
  EXPECT_EQ(node1->downgradeMember(2), 0);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEARNER, 3000);
  EXPECT_LOG_GET_METADATA_EQ(
      node2, Paxos::keyLearnerConfigure,
      "127.0.0.1:11004$0;127.0.0.1:11005$0;127.0.0.1:11002$0", 1000);
  EXPECT_LOG_GET_METADATA_EQ(
      node4, Paxos::keyLearnerConfigure,
      "127.0.0.1:11004$0;127.0.0.1:11005$0;127.0.0.1:11002$0", 1000);

  // learner -> follower
  EXPECT_EQ(0, node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(4)));
  EXPECT_EQ_EVENTUALLY(Paxos::FOLLOWER, node4->getState(), 3000);
  EXPECT_LOG_GET_METADATA_EQ(
      node4, Paxos::keyMemberConfigure,
      "127.0.0.1:11001#5;127.0.0.1:11004#5;127.0.0.1:11003#5@2", 1000);
  EXPECT_LOG_GET_METADATA_EQ(node4, Paxos::keyLearnerConfigure,
                             "0;127.0.0.1:11005$0;127.0.0.1:11002$0", 1000);
  EXPECT_LOG_GET_METADATA_EQ(node5, Paxos::keyLearnerConfigure,
                             "0;127.0.0.1:11005$0;127.0.0.1:11002$0", 1000);

  // follower -> learner, 101
  EXPECT_EQ(0, node1->downgradeMember(3));
  EXPECT_LOG_GET_METADATA_EQ(node4, Paxos::keyMemberConfigure,
                             "127.0.0.1:11001#5;127.0.0.1:11004#5@2", 1000);
  EXPECT_LOG_GET_METADATA_EQ(
      node2, Paxos::keyLearnerConfigure,
      "127.0.0.1:11003$0;127.0.0.1:11005$0;127.0.0.1:11002$0", 1000);
  EXPECT_LOG_GET_METADATA_EQ(
      node4, Paxos::keyLearnerConfigure,
      "127.0.0.1:11003$0;127.0.0.1:11005$0;127.0.0.1:11002$0", 1000);
  EXPECT_LOG_GET_METADATA_EQ(
      node5, Paxos::keyLearnerConfigure,
      "127.0.0.1:11003$0;127.0.0.1:11005$0;127.0.0.1:11002$0", 1000);

  // configureLearner
  node1->configureLearner(101, 103);
  // add and del some non-existed learners
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRefRange(
                                              0, 6, 12000));  // add 104-110
  node1->changeLearners(Paxos::CCAddNode,
                        t.getNodesIpAddrVectorRef1(6));  // add 111
  node1->changeLearners(Paxos::CCDelNode,
                        t.getNodesIpAddrVectorRefRange(0, 6, 12000));
  node1->configureLearner(102, 111);
  EXPECT_LOG_GET_METADATA_EQ(
      node2, Paxos::keyLearnerConfigure,
      "127.0.0.1:11003$103;127.0.0.1:11005$111;127.0.0.1:11002$0;"
      "0;0;0;0;0;0;0;127.0.0.1:11006$0",
      1000);
  EXPECT_LOG_GET_METADATA_EQ(
      node4, Paxos::keyLearnerConfigure,
      "127.0.0.1:11003$103;127.0.0.1:11005$111;127.0.0.1:11002$0;"
      "0;0;0;0;0;0;0;127.0.0.1:11006$0",
      1000);
  // configureLearner won't be replicated to 102
  EXPECT_LOG_GET_METADATA_EQ(
      node5, Paxos::keyLearnerConfigure,
      "127.0.0.1:11003$103;127.0.0.1:11005$111;127.0.0.1:11002$0;"
      "0;0;0;0;0;0;0;127.0.0.1:11006$0",
      1000);

  // restart learner
  t.destroyNode(5);
  node5 = t.createRDLogNodeAtIndex(5).getNode(5);
  node5->initAsLearner(t.getEmptyNodeIpAddrRef());
  EXPECT_LOG_GET_METADATA_EQ(
      node5, Paxos::keyLearnerConfigure,
      "127.0.0.1:11003$103;127.0.0.1:11005$111;127.0.0.1:11002$0;"
      "0;0;0;0;0;0;0;127.0.0.1:11006$0",
      1000);
}

TEST_F(ThreeNodesCluster, learner_meta_format) {
  Paxos* node4 = t.createRDLogNode().initAsLearner();  // 101
  std::ignore = t.createRDLogNode().initAsLearner();   // 102
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(5));
  EXPECT_LOG_GET_METADATA_EQ(node4, Paxos::keyMemberConfigure,
                             "127.0.0.1:11004#5", 1000);
  EXPECT_LOG_GET_METADATA_EQ(node4, Paxos::keyLearnerConfigure,
                             "127.0.0.1:11004$0;127.0.0.1:11005$0", 1000);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       6000);
}

// TODO improve this test using some failpoint
TEST_F(ThreeNodesCluster, SyncLearnerAll) {
  std::ignore = t.createRDLogNode().initAsLearner();
  std::ignore = t.createRDLogNode().initAsLearner();
  Paxos* node6 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef2(4, 5));

  t.replicateLogEntry(1, 1, "aaa");
  auto lli = node1->getLastLogIndex();

  t.appendLogEntryToLocalRDLog(6, node1->getTerm(), kMock, lli - 1, "aaa");
  EXPECT_EQ(lli - 1, node6->getLog()->getLastLogIndex());

  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(6));
  EXPECT_EQ_EVENTUALLY(
      node6->learnersToString(),
      std::string("127.0.0.1:11004$0;127.0.0.1:11005$0;127.0.0.1:11006$0"),
      3000);

  /* for old added learner to sync meta */
  node1->changeLearners(Paxos::CCSyncLearnerAll, t.getNodesIpAddrVectorRef0());
  EXPECT_EQ_EVENTUALLY(
      node6->learnersToString(),
      std::string("127.0.0.1:11004$0;127.0.0.1:11005$0;127.0.0.1:11006$0"),
      3000);
}

TEST_F(ThreeNodesCluster, cctimeout) {
  node1->setConfigureChangeTimeout(1000);
  t.destroyNode(2);
  t.destroyNode(3);

  std::ignore = t.createRDLogNode().initAsLearner();
  EXPECT_EQ(
      node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4)),
      PaxosErrorCode::PE_TIMEOUT);
}

TEST_F(CustomizedThreeNodesCluster, learnerSource4) {
  createNodes123();

  Paxos* node4 = t.createRDLogNode().initAsLearner();
  Paxos* node5 = t.createRDLogNode().initAsLearner();
  std::ignore = t.createRDLogNode().initAsLearner();
  std::ignore = t.createRDLogNode().initAsLearner();
  std::ignore = t.createRDLogNode().initAsLearner();

  // We init log with mock log in index 1 to make error happend when we send log
  // entry 1 to learner!
  for (int nodeIdx = 1; nodeIdx <= 8; nodeIdx++) {
    t.appendLogEntryToLocalRDLog(nodeIdx, 1, kMock, 1, "aaa");
  }

  initNodes123();
  ensureNode1Elected();

  node1->changeLearners(Paxos::CCAddNode,
                        t.getNodesIpAddrVectorRef5(4, 5, 6, 7, 8));
  EXPECT_EQ_EVENTUALLY(
      node1->membersToString(("127.0.0.1:11002")),
      std::string("127.0.0.1:11001#5;127.0.0.1:11002#5;127.0.0.1:11003#5@2"),
      1000);
  EXPECT_EQ_EVENTUALLY(
      node1->learnersToString(),
      std::string("127.0.0.1:11004$0;127.0.0.1:11005$0;127.0.0.1:11006$0;"
                  "127.0.0.1:11007$0;127.0.0.1:11008$0"),
      1000);

  // a chain of learner
  node1->configureLearner(101, 2);
  node1->configureLearner(102, 101);
  node1->configureLearner(103, 102);
  node1->configureLearner(104, 103);
  node1->configureLearner(105, 104);
  EXPECT_EQ_EVENTUALLY(
      node1->learnersToString(),
      std::string("127.0.0.1:11004$2;127.0.0.1:11005$101;127.0.0.1:11006$102;"
                  "127.0.0.1:11007$103;127.0.0.1:11008$104"),
      2000);
  EXPECT_EQ_EVENTUALLY(
      node2->learnersToString(),
      std::string("127.0.0.1:11004$2;127.0.0.1:11005$101;127.0.0.1:11006$102;"
                  "127.0.0.1:11007$103;127.0.0.1:11008$104"),
      2000);
  EXPECT_EQ(node2->getLearnerSource(101), 2);
  EXPECT_EQ(node2->getLearnerSource(102), 101);
  EXPECT_EQ(node2->getLearnerSource(103), 102);
  EXPECT_EQ(node2->getLearnerSource(104), 103);
  EXPECT_EQ(node2->getLearnerSource(105), 104);

  auto index = node1->getLastLogIndex();
  t.replicateLogEntry(1, 15, "aaa");
  EXPECT_EQ_EVENTUALLY(index + 15, node1->getLastLogIndex(), 3000);
  EXPECT_EQ_EVENTUALLY(index + 15, node2->getLastLogIndex(), 3000);
  EXPECT_EQ_EVENTUALLY(index + 15, node4->getLastLogIndex(), 3000);
  EXPECT_EQ_EVENTUALLY(index + 15, node5->getLastLogIndex(), 3000);

  // change learnersource
  node1->configureLearner(104, 2);
  EXPECT_EQ_EVENTUALLY(node2->getLearnerSource(104), 2, 1000);

  index = node1->getLastLogIndex();
  t.replicateLogEntry(1, 15, "aaa");
  EXPECT_EQ_EVENTUALLY(index + 15, node1->getLastLogIndex(), 3000);
  EXPECT_EQ_EVENTUALLY(index + 15, node4->getLastLogIndex(), 3000);
  EXPECT_EQ_EVENTUALLY(index + 15, node5->getLastLogIndex(), 3000);
}

/**
 * bug: concurrent configureChange may clear other's flag,
 *      cause later configureChange hang
 */
TEST_F(ThreeNodesCluster, concurrent_cc) {
  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::LEADER, 3000);

  std::thread op1([&]() {
    auto v = t.getNodesIpAddrVector1(4);
    auto ret = node1->changeLearners(Paxos::CCAddNode, v);
    EXPECT_TRUE(ret == 0 || ret == PaxosErrorCode::PE_CONFLICTS);
  });
  std::thread op2([&]() {
    auto v = t.getNodesIpAddrVector1(5);
    auto ret = node1->changeLearners(Paxos::CCAddNode, v);
    EXPECT_TRUE(ret == 0 || ret == PaxosErrorCode::PE_CONFLICTS);
  });
  std::thread op3([&]() {
    auto v = t.getNodesIpAddrVector1(6);
    auto ret = node1->changeLearners(Paxos::CCAddNode, v);
    EXPECT_TRUE(ret == 0 || ret == PaxosErrorCode::PE_CONFLICTS);
  });
  op1.join();
  op2.join();
  op3.join();

  node1->setConfigureChangeTimeout(1000);
  t.destroyNode(2);
  t.destroyNode(3);
  std::thread op4([&]() {
    auto v = t.getNodesIpAddrVector1(7);
    auto ret = node1->changeLearners(Paxos::CCAddNode, v);
    EXPECT_TRUE(ret == PaxosErrorCode::PE_TIMEOUT ||
                ret == PaxosErrorCode::PE_CONFLICTS);
  });
  std::thread op5([&]() {
    auto v = t.getNodesIpAddrVector1(8);
    auto ret = node1->changeLearners(Paxos::CCAddNode, v);
    EXPECT_TRUE(ret == PaxosErrorCode::PE_TIMEOUT ||
                ret == PaxosErrorCode::PE_CONFLICTS);
  });
  op4.join();
  op5.join();
}

TEST_F(ThreeNodesCluster, learner_downgrade_term) {
  Paxos* node4 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);

  // restart node4
  auto log4 = node4->getLog();
  t.destroyNode(4);
  // force to give learner a larger term
  uint64_t cterm1 = 0, cterm2 = 0;
  log4->getMetaData(std::string(Paxos::keyCurrentTerm), &cterm1);
  node1->getLog()->getMetaData(std::string(Paxos::keyCurrentTerm), &cterm2);
  EXPECT_EQ(cterm1, cterm2);
  log4->setMetaData(std::string(Paxos::keyCurrentTerm), cterm1 + 10);
  log4.reset();

  node4 = t.createRDLogNodeAtIndex(4).getNode(4);
  node4->initAsLearner(t.getEmptyNodeIpAddrRef());
  EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(
      [&]() {
        node4->getLog()->getMetaData(std::string(Paxos::keyCurrentTerm),
                                     &cterm1);
        // check term downgrade successfully
        return cterm1 == cterm2;
      },
      3000);
  EXPECT_EQ(cterm1, cterm2);

  t.replicateLogEntry(1, 1, "aaa");
  // check log replicate is normal
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);
}

TEST_F(SingleNodeCluster, learner_heartbeat) {
  std::ignore = t.createRDLogNode().initAsLearner();

  // disable learner heartbeat
  node1->setEnableLearnerHeartbeat(false);
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(2));

  EXPECT_EQ_CONSISTENTLY(node1->getStats().countHeartbeat, 0, 2000, 100);

  // enable learner heartbeat
  node1->changeLearners(Paxos::CCDelNode, t.getNodesIpAddrVectorRef1(2));
  node1->setEnableLearnerHeartbeat(true);
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(2));

  EXPECT_TRUE_EVENTUALLY(node1->getStats().countHeartbeat > 0, 2000);
  easy_log("countHeartbeat: %d\n", node1->getStats().countHeartbeat.load());
}

/* change learnerSource use the address as argument */
TEST_F(ThreeNodesCluster, learnersource_addr) {
  Paxos* node4 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);

  // cannot configure follower
  EXPECT_EQ(node1->configureLearner("127.0.0.1:11002", "127.0.0.1:11002"),
            PaxosErrorCode::PE_NOTFOUND);
  // cannot configure not-exist node
  EXPECT_EQ(node1->configureLearner("127.0.0.1:11005", "127.0.0.1:11002"),
            PaxosErrorCode::PE_NOTFOUND);
  EXPECT_EQ(node1->configureLearner("127.0.0.1:11004", "127.0.0.1:11005"),
            PaxosErrorCode::PE_NOTFOUND);
  // success case
  EXPECT_EQ(node1->configureLearner("127.0.0.1:11004", "127.0.0.1:11003"),
            PaxosErrorCode::PE_NONE);

  EXPECT_EQ_EVENTUALLY(node1->learnersToString(),
                       std::string("127.0.0.1:11004$3"), 1000);
  // reset to 0
  EXPECT_EQ(node1->configureLearner("127.0.0.1:11004", "127.0.0.1:11004"),
            PaxosErrorCode::PE_NONE);

  EXPECT_EQ_EVENTUALLY(node1->learnersToString(),
                       std::string("127.0.0.1:11004$0"), 1000);
}

TEST_F(ThreeNodesCluster, enable_learner_auto_reset_match_index) {
  Paxos* node4 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);

  auto cindex = node1->getLastLogIndex();  // current index
  auto cterm = node1->getTerm();

  std::cout << "shutdown learner" << std::endl;
  // write two additional log entries into node4's log
  t.appendLogEntryToLocalRDLog(4, cterm, 2, "aaa");
  t.destroyNode(4);

  node2->requestVote(true);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 3000);
  EXPECT_EQ_EVENTUALLY(node2->learnersToString(),
                       std::string("127.0.0.1:11004$0"), 1000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), cindex + 1, 6000);
  EXPECT_EQ(node2->getTerm(), cterm + 1);

  // restart node4
  node4 = t.createRDLogNodeAtIndex(4).initAsLearner(4);
  EXPECT_EQ(node2->getEnableLearnerAutoResetMatchIndex(),
            false);  // default is false
  // log can not be replicated
  EXPECT_EQ(t.getNodeLogEntryTerm(4, cindex + 1), cterm);

  node2->setEnableLearnerAutoResetMatchIndex(true);
  t.replicateLogEntry(2, 1, "aaa");
  // log can be replicated
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), cindex + 2, 2000);
  EXPECT_EQ_EVENTUALLY(t.getNodeLogEntryTerm(4, cindex + 1), cterm + 1, 2000);
}

TEST_F(ThreeNodesCluster, forceSingleLearner_do_not_work_for_learner) {
  Paxos* node4 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);

  // forceSingleLearner does not work for learner
  EXPECT_EQ(node4->forceSingleLearner(), 1);
}

// test force single learner
// after a node becomes a single learner, it can be added back to a new cluster,
// and its log position will be automatically matched.
TEST_F(ThreeNodesCluster, forceSingleLearner) {
  std::vector<Paxos::ClusterInfoType> cis;
  auto baseIdx = node1->getLastLogIndex();
  auto baseTerm = node1->getTerm();

  Paxos* node4 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       1000);

  node1->setEnableLearnerAutoResetMatchIndex(true);
  node2->setEnableLearnerAutoResetMatchIndex(true);
  node3->setEnableLearnerAutoResetMatchIndex(true);
  node4->setEnableLearnerAutoResetMatchIndex(true);

  // make node4 from learner to single leader
  node4->forceSingleLeader();
  node4->requestVote();
  t.replicateLogEntry(4, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getCommitIndex(), baseIdx + 3, 6000);
  EXPECT_EQ(node4->getTerm(), baseTerm + 1);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 2, 6000);
  EXPECT_EQ(node1->getTerm(), baseTerm);

  // generate bigger term log to test learner auto reset match index
  node2->requestVote();
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 2000);
  // ensure node1 has full log before vote
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 3, 1000);
  node1->requestVote();
  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::LEADER, 2000);

  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 4, 1000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 4, 1000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 4, 1000);

  // change node2 from follower to single learner
  EXPECT_EQ(
      node2->membersToString(("127.0.0.1:11002")),
      std::string("127.0.0.1:11001#5;127.0.0.1:11002#5;127.0.0.1:11003#5@2"));
  EXPECT_EQ(node2->learnersToString(), std::string("127.0.0.1:11004$0"));
  EXPECT_EQ(node2->forceSingleLearner(), 0);
  EXPECT_EQ(node2->membersToString(), std::string("0;127.0.0.1:11002#5"));
  EXPECT_EQ(node2->learnersToString(), std::string(""));

  // change node1 from leader to single learner
  EXPECT_EQ(
      node1->membersToString(("127.0.0.1:11001")),
      std::string("127.0.0.1:11001#5;127.0.0.1:11002#5;127.0.0.1:11003#5@1"));
  EXPECT_EQ(node1->learnersToString(), std::string("127.0.0.1:11004$0"));
  EXPECT_EQ(node1->forceSingleLearner(), 0);
  EXPECT_EQ(node1->membersToString(), std::string("127.0.0.1:11001#5"));
  EXPECT_EQ(node1->learnersToString(), std::string(""));

  // node3 is useless
  t.destroyNode(3);

  // add node1 as node4's learner
  node4->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(1));

  EXPECT_EQ_EVENTUALLY(node4->getCommitIndex(), baseIdx + 4, 6000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 4, 6000);
  EXPECT_EQ(node4->membersToString(("127.0.0.1:11004")),
            std::string("127.0.0.1:11004#5@1"));
  EXPECT_EQ(node4->learnersToString(), std::string("127.0.0.1:11001$0"));
  EXPECT_EQ(node1->membersToString(), std::string("127.0.0.1:11001#5"));
  EXPECT_EQ_EVENTUALLY(node1->learnersToString(),
                       std::string("127.0.0.1:11001$0"), 3000);

  t.replicateLogEntry(4, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getCommitIndex(), baseIdx + 5, 6000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 5, 6000);

  // add node2 as learner
  node4->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(2));

  EXPECT_EQ_EVENTUALLY(node4->getCommitIndex(), baseIdx + 6, 6000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 6, 6000);
  EXPECT_EQ(node4->membersToString(("127.0.0.1:11004")),
            std::string("127.0.0.1:11004#5@1"));
  EXPECT_EQ(node4->learnersToString(),
            std::string("127.0.0.1:11001$0;127.0.0.1:11002$0"));
  EXPECT_EQ(node2->membersToString(), std::string("0;127.0.0.1:11002#5"));
  EXPECT_EQ(node2->learnersToString(),
            std::string("127.0.0.1:11001$0;127.0.0.1:11002$0"));

  t.replicateLogEntry(4, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getCommitIndex(), baseIdx + 7, 6000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 7, 6000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 7, 6000);

  // upgrade node1 to follower
  node4->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(1));

  EXPECT_EQ_EVENTUALLY(node4->getCommitIndex(), baseIdx + 8, 6000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 8, 6000);
  EXPECT_EQ(node4->membersToString(("127.0.0.1:11004")),
            std::string("127.0.0.1:11004#5;127.0.0.1:11001#5@1"));
  EXPECT_EQ(node4->learnersToString(), std::string("0;127.0.0.1:11002$0"));
  EXPECT_EQ(node1->membersToString(("127.0.0.1:11001")),
            std::string("127.0.0.1:11004#5;127.0.0.1:11001#5@2"));
  EXPECT_EQ(node1->learnersToString(), std::string("0;127.0.0.1:11002$0"));

  t.replicateLogEntry(4, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getCommitIndex(), baseIdx + 9, 6000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 9, 6000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 9, 6000);

  // upgrade node2 to follower
  node4->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(2));
  EXPECT_EQ_EVENTUALLY(node4->getCommitIndex(), baseIdx + 10, 6000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 10, 6000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 10, 6000);
  EXPECT_EQ(
      node4->membersToString(("127.0.0.1:11004")),
      std::string("127.0.0.1:11004#5;127.0.0.1:11001#5;127.0.0.1:11002#5@1"));
  EXPECT_EQ(node4->learnersToString(), std::string(""));
  EXPECT_EQ(
      node1->membersToString(("127.0.0.1:11002")),
      std::string("127.0.0.1:11004#5;127.0.0.1:11001#5;127.0.0.1:11002#5@3"));
  EXPECT_EQ(node1->learnersToString(), std::string(""));

  t.replicateLogEntry(4, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getCommitIndex(), baseIdx + 11, 6000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 11, 6000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 11, 6000);

  // TODO check me
  // since this function will break the protocol,
  // logs might be different on every node
  // may be truncated or not
  EXPECT_TRUE(t.getNodeLogEntryTerm(1, baseIdx + 2) == baseTerm ||
              t.getNodeLogEntryTerm(1, baseIdx + 2) == baseTerm + 1);
  EXPECT_TRUE(t.getNodeLogEntryTerm(2, baseIdx + 2) == baseTerm ||
              t.getNodeLogEntryTerm(2, baseIdx + 2) == baseTerm + 1);

  node2->requestVote(true);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 2000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 12, 2000);
  auto localServer = node2->getLocalServer();
  LogEntry le;
  le.set_optype(0);
  le.set_term(node2->getTerm());
  localServer->writeLog(le);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 13, 6000);
  // TODO check me
  // if bug exists, match index will be baseIdx + 12
  EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(
      [&]() {
        std::vector<Paxos::ClusterInfoType> cis;
        node2->getClusterInfo(cis);
        return cis[2].matchIndex == baseIdx + 13;
      },
      2000);
}

TEST_F(SingleNodeCluster, paxos_learner_restart) {
  t.replicateLogEntry(1, 2, "aaa");

  std::ignore = t.createRDLogNode().getLastNode();
  Paxos* node3 =
      t.createRDLogNode().initAsLearner();  // only node3 is initialized

  // add learners
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRefRange(2, 8));
  EXPECT_EQ_EVENTUALLY(
      node3->learnersToString(),
      std::string(
          "127.0.0.1:11002$0;127.0.0.1:11003$0;127.0.0.1:11004$0;127.0.0.1:"
          "11005$0;127.0.0.1:11006$0;127.0.0.1:11007$0;127.0.0.1:11008$0"),
      1000);

  node1->changeLearners(Paxos::CCDelNode, t.getNodesIpAddrVectorRef1(5));
  EXPECT_EQ_EVENTUALLY(
      node3->learnersToString(),
      std::string("127.0.0.1:11002$0;127.0.0.1:11003$0;127.0.0.1:11004$0;0;"
                  "127.0.0.1:11006$0;127.0.0.1:11007$0;127.0.0.1:11008$0"),
      1000);

  node1->changeLearners(Paxos::CCDelNode, t.getNodesIpAddrVectorRef1(6));
  EXPECT_EQ_EVENTUALLY(
      node3->learnersToString(),
      std::string("127.0.0.1:11002$0;127.0.0.1:11003$0;127.0.0.1:11004$"
                  "0;0;0;127.0.0.1:11007$0;127.0.0.1:11008$0"),
      1000);

  node1->changeLearners(Paxos::CCDelNode, t.getNodesIpAddrVectorRef1(4));
  EXPECT_EQ_EVENTUALLY(
      node3->learnersToString(),
      std::string("127.0.0.1:11002$0;127.0.0.1:11003$0;0;0;0;127.0.0.1:"
                  "11007$0;127.0.0.1:11008$0"),
      1000);

  t.destroyNode(3);
  node3 = t.createRDLogNodeAtIndex(3).getNode(3);
  node3->initAsLearner(t.getEmptyNodeIpAddrRef());

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(
      node3->learnersToString(),
      std::string("127.0.0.1:11002$0;127.0.0.1:11003$0;0;0;0;127.0.0.1:"
                  "11007$0;127.0.0.1:11008$0"),
      1000);
}
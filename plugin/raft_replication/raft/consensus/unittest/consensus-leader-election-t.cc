// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include <atomic>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "fail_point.h"
#include "file_paxos_log.h"
#include "files.h"
#include "paxos.h"
#include "rd_paxos_log.h"
#include "ut_helper.h"

using namespace alisql;

// This files tests the leader election functionality of the Raft algorithm.
// Including the following cases:
// 1. Basic leader election.
// 2. leader re-election after leader failure.
// 3. optimization for unnecessary leader re-election.
// 4. leader transfer

// key concepts of leader election in Raft algorithm:

// In Raft algorithm, A candidate continues in this state until one of three
// things happens: (a) it wins the election, (b) another server establishes
// itself as leader, or (c) another election timeout goes by with no winner. see
// (a) in LeaderElection.BasicElection, see (b) in
// ElectionOptimize.LeaderStickiness, see (c) in LeaderElection.SplitVote

// Election restriction: the requestVote RPC includes information about the
// candidate’s log, and the voter denies its vote if its own log is more
// up-to-date than that of the candidate. see LeaderElection.RequestVote

const int intervalMS = 100;
const int timeoutMS = 3000;

// a helper class to simulate a slow or error disk
class TestFilePaxosLog : public FilePaxosLog {
 public:
  TestFilePaxosLog(const std::string& dataDir)
      : FilePaxosLog(dataDir), delay(0), retError(false) {}

  virtual int getEntry(uint64_t logIndex, LogEntry& entry,
                       bool fastFail = false) {
    if (retError) return -1;
    if (!fastFail && delay) sleep(delay.load());
    return FilePaxosLog::getEntry(logIndex, entry, fastFail);
  }

  void setDelay(int delay) { this->delay = delay; }
  void setRetError(bool retError) { this->retError = retError; }

 private:
  std::atomic<int> delay;
  std::atomic<bool> retError;
};

// 1. Basic leader election.

// This test covers various scenarios related to the requestVote() function,
// including successful leader election, preventing a leader from requesting a
// vote again, disallowing learners from requesting a vote, preventing followers
// with zero election weight from requesting a vote, and handling shut-down
// followers' inability to request a vote.
TEST_F(ThreeNodesCluster, RequestVote_error_codes) {
  // A leader requests vote again, should fail and return -1
  auto ret = node1->requestVote();
  EXPECT_EQ(ret, -1);

  // A learner can not request vote and will return -1
  Paxos* node4 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));
  ret = node4->requestVote();
  EXPECT_EQ(ret, -1);

  // A follower with zero election weight is unable to request a vote and will
  // return -1.
  ret = node1->configureMember(2, false, 0);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ_EVENTUALLY(node2->getLocalServer()->electionWeight, 0, 3000);
  ret = node2->requestVote();
  EXPECT_EQ(ret, -1);

  // A follower that has been shutdown cannot request a vote and will return -1.
  node3->shutdown();
  ret = node3->requestVote();
  EXPECT_EQ(ret, PaxosErrorCode::PE_SHUTDOWN);
}

// This test case verifies the behavior of the onRequestVote function in various
// scenarios, testing different rejection conditions and ensuring the
// algorithm's correctness.
// Case 1: The candidate ID is not found in the configuration, so the request is
// rejected.
// Case 2: The term of node2 is greater than the request's term, resulting in
// rejection.
// Case 3: Leader stickiness is triggered, so the request is rejected.
// Case 4: The requesting node has a smaller log index, leading to rejection.
// Case 5: node3 has been shut down, and the request should return -1.
// Case 6: node4 is unable to retrieve the log entry, resulting in rejection.
TEST_F(ThreeNodesCluster, onRequestVote_reject) {
  // construct a request vote message and its response
  PaxosMsg msg, rsp;
  msg.set_msgtype(Consensus::RequestVote);

  easy_log(
      "Case 1: The candidate ID is not found in the configuration, so the "
      "request is rejected.\n");
  // Can't find the candidateid in the configuration, reject the request.
  // Set term and lastlogindex to a bigger value to test candidate id.
  msg.set_term(10);
  msg.set_lastlogindex(100);
  msg.set_lastlogterm(9);
  msg.set_candidateid(10);
  msg.set_addr("127.0.0.1:11010");
  int ret = node2->onRequestVote(&msg, &rsp);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(rsp.votegranted(), 0);
  uint64_t currentTerm = node2->getTerm();
  EXPECT_EQ(currentTerm, rsp.term());

  easy_log(
      "Case 2: The term of node2 is greater than the request's term, "
      "resulting in rejection.\n");
  // node2's term is bigger than the request's term, reject the request.
  // Set other parameters valid.
  msg.set_term(0);
  msg.set_lastlogindex(100);
  msg.set_lastlogterm(9);
  msg.set_candidateid(3);
  msg.set_addr("127.0.0.1:11003");
  ret = node2->onRequestVote(&msg, &rsp);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(rsp.votegranted(), 0);

  easy_log(
      "Case 3: Leader stickiness is triggered, so the request is "
      "rejected.\n");
  // Leaderstickiness works, reject the request. set every parameter valid.
  msg.set_term(10);
  msg.set_lastlogindex(100);
  msg.set_lastlogterm(9);
  msg.set_candidateid(3);
  msg.set_addr("127.0.0.1:11003");
  ret = node1->onRequestVote(&msg, &rsp);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(rsp.votegranted(), 0);
  EXPECT_EQ(rsp.force(), 1);
  ret = node2->onRequestVote(&msg, &rsp);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(rsp.votegranted(), 0);

  easy_log(
      "Case 4: The requesting node has a smaller log index, leading to "
      "rejection.\n");
  // The node that sent this request has a smaller log index, reject the
  // request. Set other parameters valid.
  msg.set_term(10);
  msg.set_lastlogterm(currentTerm);
  msg.set_lastlogindex(0);  // too small
  msg.set_candidateid(3);
  msg.set_addr("127.0.0.1:11003");
  msg.set_force(1);  // skip leaderstickiness check
  ret = node2->onRequestVote(&msg, &rsp);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(rsp.votegranted(), 0);

  easy_log(
      "Case 5: node3 has been shut down, and the request should return "
      "-1.\n");
  // node3 has been shut down, should return -1.
  node3->shutdown();
  ret = node3->onRequestVote(&msg, &rsp);
  EXPECT_EQ(ret, PaxosErrorCode::PE_SHUTDOWN);
}

TEST_F(CustomizedCluster, onRequestVote_reject) {
  // node1 can't get the log entry, should return -1.
  Paxos* node1 = t.addNode(new Paxos(2000, std::make_shared<TestFilePaxosLog>(
                                               "paxosLogTestDir1")))
                     .initAsMember(t.getNodesIpAddrVectorRef2(1, 2));
  std::ignore = t.addNode(new Paxos(2000, std::make_shared<TestFilePaxosLog>(
                                              "paxosLogTestDir2")))
                    .initAsMember(t.getNodesIpAddrVectorRef2(1, 2));

  ((TestFilePaxosLog*)node1->getLog().get())->setRetError(true);

  // construct a request vote message and its response
  PaxosMsg msg, rsp;
  msg.set_msgtype(Consensus::RequestVote);

  msg.set_term(10);
  msg.set_lastlogindex(100);
  msg.set_lastlogterm(9);
  msg.set_candidateid(2);
  msg.set_addr("127.0.0.1:11002");
  auto ret = node1->onRequestVote(&msg, &rsp);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(rsp.votegranted(), 0);
}

// This test case focuses on testing the leader stickiness mechanism in our Raft
// algorithm. It ensures that when a non-leader node requests a vote without
// forcing an election, the current leader maintains its leadership position.
// This mechanism prevents unnecessary leader elections and promotes stability
// in the cluster.
TEST_F(ThreeNodesCluster, leader_stickiness) {
  t.replicateLogEntry(1, 2, "aaa");

  // The leader stickiness mechanism works when requestVote's force parameter is
  // set to false, which is also set to false in startElectionCallback.
  node2->requestVote(false);
  EXPECT_EQ_CONSISTENTLY(node1->getState(), Paxos::LEADER, 1000, 100);
  EXPECT_EQ_CONSISTENTLY(node2->getState(), Paxos::FOLLOWER, 1000, 100);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       1000);

  // we increase the term of node2, this will not disrupt the leader node1
  // because leader stickness is enabled.
  node2->fakeRequestVote();
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::FOLLOWER, 2000);
  EXPECT_EQ_EVENTUALLY(node2->getTerm(), node1->getTerm(), 10000);

  node2->requestVote(/*defautly true*/);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 1000);
  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::FOLLOWER, 1000);
}

// The purpose of the test code is to verify the basic leader election
// functionality of the Raft algorithm. This test case aims to create a Raft
// cluster with three nodes and verify that leader election occurs correctly.
// It checks that one of the nodes eventually becomes the leader, and it
// performs assertions on the leader and follower statistics to ensure the
// expected messages are exchanged during the election process.
TEST_F(ThreeNodesCluster, check_message_counts_during_election) {
  Paxos::StatsType leaderStats, followerStats1, followerStats2;
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::FOLLOWER, 1000);
  EXPECT_EQ_EVENTUALLY(node3->getState(), Paxos::FOLLOWER, 1000);
  leaderStats = node1->getStats();
  followerStats1 = node2->getStats();
  followerStats2 = node3->getStats();

  // Ensure the leader has sent more than one request vote messages to other
  // nodes.
  EXPECT_TRUE(leaderStats.countMsgRequestVote >= 2);
  // Ensure each follower has received more than one request vote message.
  // If there're more than one candidate, each follower will receive more than
  // one request vote message.
  EXPECT_TRUE(followerStats1.countOnMsgRequestVote >= 1);
  EXPECT_TRUE(followerStats2.countOnMsgRequestVote >= 1);
}

// This test verifies that each node in a cluster can campaign and be elected.
TEST_F(FiveNodesCluster, switch_leader) {
  for (int i = 5; i >= 1; i--) {
    t.ensureGivenNodeElected(i);
  }
}

// This test case focuses on testing the behavior of the Raft algorithm when a
// split vote occurs during leader election. It verifies that when a network
// partition isolates a portion of the cluster, preventing communication between
// the nodes, no new leader will be elected due to the lack of a majority vote.
// This scenario tests the fault tolerance and correctness of the leader
// election process in Raft.
#ifdef FAIL_POINT
TEST_F(ThreeNodesCluster, vote_in_brain_split) {
  // isolate the node 2
  FailPointData dataWith2((uint64_t)2);
  auto fp = FailPointRegistry::getGlobalFailPointRegistry().find(
      "isolateAFollowerBothWay");
  ASSERT_TRUE(fp != NULL);
  fp->activate(FailPoint::alwaysOn, 0, dataWith2);

  t.destroyNode(1);
  node2->requestVote();
  node3->requestVote();
  // The votes are always split
  EXPECT_EQ_CONSISTENTLY(node2->getState(), Paxos::CANDIDATE, 1000, 100);
  EXPECT_EQ(node3->getState(), Paxos::CANDIDATE);
  fp->deactivate();
}
#endif

// 2. leader re-election after leader failure.

// The purpose of the test code is to verify the proper functioning of the
// Raft algorithm during leader re-election. this test case simulates a Raft
// cluster with three nodes and verifies whether the election process can
// successfully elect a new leader in the event of failures and re-elections.
TEST_F(ThreeNodesCluster, reelect_by_stop_leader) {
  t.destroyNode(1);
  EXPECT_TRUE_EVENTUALLY(t.leaderExists(), 10000);
}

// LeaderStepdown verifies a leader will step down itself if lost connection
// with the majority
#ifdef FAIL_POINT
TEST_F(ThreeNodesCluster, leader_stepdown) {
  // isolate node 1
  FailPointData dataWith1((uint64_t)1);
  auto fp = FailPointRegistry::getGlobalFailPointRegistry().find(
      "isolateAFollowerBothWay");
  ASSERT_TRUE(fp != NULL);
  fp->activate(FailPoint::alwaysOn, 0, dataWith1);

  EXPECT_TRUE_EVENTUALLY(
      node2->getState() == Paxos::LEADER || node3->getState() == Paxos::LEADER,
      10000);
  EXPECT_TRUE_EVENTUALLY(node1->getState() == Paxos::FOLLOWER ||
                             node1->getState() == Paxos::CANDIDATE,
                         3000);
  fp->deactivate();
}
#endif

// 3. optimization for unnecessary leader re-election.

// OptimisticHeartbeat make sure that leader will not degrade if follower
// response to heartbeat too slow
TEST_F(CustomizedCluster, sendMsg_timeout_without_optimistic_heartbeat) {
  // heartbeatThreadCnt is 1, a specialized thread pool is created for
  // heartbeat. so that we can make sure that heartbeat will not be blocked
  Paxos* node1 = t.addNode(new Paxos(1000, std::make_shared<TestFilePaxosLog>(
                                               "paxosLogTestDir1")))
                     .getLastNode();
  node1->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 1, NULL, 4, 4, NULL, false,
              1 /*heartbeatThreadCnt*/);
  Paxos* node2 = t.addNode(new Paxos(1000, std::make_shared<TestFilePaxosLog>(
                                               "paxosLogTestDir2")))
                     .getLastNode();
  node2->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 2, NULL, 4, 4, NULL, false,
              1 /*heartbeatThreadCnt*/);
  Paxos* node3 = t.addNode(new Paxos(1000, std::make_shared<TestFilePaxosLog>(
                                               "paxosLogTestDir3")))
                     .getLastNode();
  node3->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 3, NULL, 4, 4, NULL, false,
              1 /*heartbeatThreadCnt*/);

  EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(2)), true, 2000);
  EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(3)), true, 2000);
  node1->requestVote();
  EXPECT_EQ_EVENTUALLY(node1->getState(), alisql::Paxos::LEADER, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node2->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node3->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);

  // 3 seconds is long enough for blocking appendlog messages to trigger
  // stepdown in the leader's epochTimer (about 2 epochs, each epoch is 1s.)
  std::dynamic_pointer_cast<TestFilePaxosLog>(node2->getLog())->setDelay(3);
  std::dynamic_pointer_cast<TestFilePaxosLog>(node3->getLog())->setDelay(3);

  t.replicateLogEntry(1, 1, "aaa");

  // wait at least 2 times of epochTimer callback (2s)
  EXPECT_TRUE_EVENTUALLY(node1->getState() != Paxos::LEADER, 2500)

  std::dynamic_pointer_cast<TestFilePaxosLog>(node2->getLog())->setDelay(0);
  std::dynamic_pointer_cast<TestFilePaxosLog>(node3->getLog())->setDelay(0);
}

TEST_F(CustomizedCluster, sendMsg_timeout_with_optimistic_heartbeat) {
  // heartbeatThreadCnt is 1, a specialized thread pool is created for
  // heartbeat. so that we can make sure that heartbeat will not be blocked
  Paxos* node1 = t.addNode(new Paxos(1000, std::make_shared<TestFilePaxosLog>(
                                               "paxosLogTestDir1")))
                     .getLastNode();
  node1->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 1, NULL, 4, 4, NULL, false,
              1 /*heartbeatThreadCnt*/);
  Paxos* node2 = t.addNode(new Paxos(1000, std::make_shared<TestFilePaxosLog>(
                                               "paxosLogTestDir2")))
                     .getLastNode();
  node2->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 2, NULL, 4, 4, NULL, false,
              1 /*heartbeatThreadCnt*/);
  Paxos* node3 = t.addNode(new Paxos(1000, std::make_shared<TestFilePaxosLog>(
                                               "paxosLogTestDir3")))
                     .getLastNode();
  node3->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 3, NULL, 4, 4, NULL, false,
              1 /*heartbeatThreadCnt*/);

  EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(2)), true, 2000);
  EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(3)), true, 2000);
  node1->requestVote();
  EXPECT_EQ_EVENTUALLY(node1->getState(), alisql::Paxos::LEADER, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node2->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node3->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);

  // According to the issue described here:
  // https://github.com/apecloud/consensus-library/issues/164
  // Even a follower answers the OptimisticHeartbeat msg,
  // it may be dropped as expired message and not be seen by the leader.
  // So here we explicitly modify default network timeout.
  node1->setSendPacketTimeout(500);

  node2->setOptimisticHeartbeat(true);
  node3->setOptimisticHeartbeat(true);
  std::dynamic_pointer_cast<TestFilePaxosLog>(node2->getLog())->setDelay(3);
  std::dynamic_pointer_cast<TestFilePaxosLog>(node3->getLog())->setDelay(3);

  t.replicateLogEntry(1, 1, "aaa");

  // The OptimisticHeartbeat mechanism works when OptimisticHeartbeat_ is true
  // and the heartbeat msg carries no log entry.
  // with optimistic heartbeat, leader will not degrade

  // TODO FIXME: This case may fail!
  // There are two factors that cause this case unstable:
  // 1. The OptimisticHeartbeat mechanism may not be always activated,
  // because there is always one log entry not replicated to follower1
  // and follower2 successfully(because of sleep() in getEntry) and the
  // subsequent heartbeat needs to carry the entry which will disable the
  // OptimisticHeartbeat mechanism.

  EXPECT_EQ_CONSISTENTLY(node1->getState(), Paxos::LEADER, 2500, 100)

  std::dynamic_pointer_cast<TestFilePaxosLog>(node2->getLog())->setDelay(0);
  std::dynamic_pointer_cast<TestFilePaxosLog>(node3->getLog())->setDelay(0);
}

// 4. leader transfer

// The purpose of this test is to ensure that the leader transfer functionality
// behaves correctly in different scenarios.
TEST_F(ThreeNodesCluster, force_promote) {
  Paxos* node4 = t.createFileLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));
  EXPECT_EQ_EVENTUALLY(node1->learnersToString(),
                       std::string("127.0.0.1:11004$0"), 3000);

  /* learner forcePromote: do nothing */
  node4->forcePromote();
  EXPECT_EQ_EVENTUALLY(node4->getState(), Paxos::LEARNER, 1000);

  /* follower forcePromote: become new leader */
  node2->forcePromote();
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 2000);
  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::FOLLOWER, 1000);
}

// This test case ensures that the leader transfer functionality works correctly
// in the raft algorithm. It verifies that when a leader transfers its
// leadership to another node, the new leader assumes the leadership role,
// updates its term and log indices appropriately, and maintains the consistency
// of the replicated log.
TEST_F(ThreeNodesCluster, leader_transfer_should_transfer_log) {
  auto baseTerm = node1->getTerm();
  auto baseIdx = node1->getLastLogIndex();

  for (int i = 0; i < 20; ++i) t.appendLogEntryToLocalRDLog(1, 1, "aaa");

  node1->leaderTransfer(2);

  // wait for leader transfer taking effect
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 10000);
  EXPECT_EQ(node2->getTerm(), baseTerm + 1);  // only one election should happen
  EXPECT_EQ(node2->getLastLogIndex(), baseIdx + 20 + 1);
  // A leader command will executed immediately not waiting for log commit
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 20 + 1, 3000);
}

class PaxosLogTmp : public FilePaxosLog {
 public:
  PaxosLogTmp(const std::string& dataDir, FilePaxosLog::LogTypeT type)
      : FilePaxosLog(dataDir, type) {}
  virtual ~PaxosLogTmp() {}

  virtual bool isStateMachineHealthy() {
    std::cout << "healthy" << std::string(healthy ? "y" : "n");
    return healthy;
  }
  bool healthy = true;
};

class ThreePaxosLogTmpNodesCluster : public ::testing::Test {
 protected:
  void SetUp() override {
    // heartbeatThreadCnt is 1, a specialized thread pool is created for
    // heartbeat. so that we can make sure that heartbeat will not be blocked
    node1 = t.addNode(new Paxos(1000, std::make_shared<PaxosLogTmp>(
                                          "paxosLogTestDir1",
                                          FilePaxosLog::LogType::LTMem)))
                .initAsMember(t.getNodesIpAddrVectorRef3(1, 2, 3));
    node2 = t.addNode(new Paxos(1000, std::make_shared<PaxosLogTmp>(
                                          "paxosLogTestDir2",
                                          FilePaxosLog::LogType::LTMem)))
                .initAsMember(t.getNodesIpAddrVectorRef3(1, 2, 3));
    node3 = t.addNode(new Paxos(1000, std::make_shared<PaxosLogTmp>(
                                          "paxosLogTestDir3",
                                          FilePaxosLog::LogType::LTMem)))
                .initAsMember(t.getNodesIpAddrVectorRef3(1, 2, 3));

    node1->setEnableAutoLeaderTransfer(true);
    node1->setMinNextEpochCheckSeconds(1);
    node1->setAutoLeaderTransferCheckSeconds(1);

    node2->setEnableAutoLeaderTransfer(true);
    node2->setMinNextEpochCheckSeconds(1);
    node2->setAutoLeaderTransferCheckSeconds(1);

    node3->setEnableAutoLeaderTransfer(true);
    node3->setMinNextEpochCheckSeconds(1);
    node3->setAutoLeaderTransferCheckSeconds(1);

    EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(2)), true, 2000);
    EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(3)), true, 2000);
    node1->requestVote();
    EXPECT_EQ_EVENTUALLY(node1->getState(), alisql::Paxos::LEADER, 3000);
    EXPECT_EQ_EVENTUALLY(node1->getLog()->getLastLogTerm(), node1->getTerm(),
                         1000);
    EXPECT_EQ_EVENTUALLY(node2->getLog()->getLastLogTerm(), node1->getTerm(),
                         1000);
    EXPECT_EQ_EVENTUALLY(node3->getLog()->getLastLogTerm(), node1->getTerm(),
                         1000);
  }

  void TearDown() override {}

  PaxosTestbed t;
  alisql::Paxos* node1;
  alisql::Paxos* node2;
  alisql::Paxos* node3;
};

// Following three test cases verify the correctness of the automatic leader
// transfer feature, including:
// 1. the node's log type
// 2. the health status of the state machine
// 3. the randomness of the target selection.
TEST_F(ThreePaxosLogTmpNodesCluster, AutoLeaderTransfer_log_node) {
  // case 1: log type node
  uint64_t term = node1->getTerm();
  node1->setAsLogType(true);

  EXPECT_EQ_EVENTUALLY(node1->getTerm(), term + 1, 2000);
  EXPECT_TRUE_EVENTUALLY(node1->getState() != Paxos::LEADER, 2000);
  // wait until either node2 or node3 becomes leader
  EXPECT_TRUE_EVENTUALLY(
      node2->getState() == Paxos::LEADER || node3->getState() == Paxos::LEADER,
      2000);

  node1->setAsLogType(false);
}

TEST_F(ThreePaxosLogTmpNodesCluster, AutoLeaderTransfer_node_not_healthy) {
  // case 2: state machine not healthy
  uint64_t term = node1->getTerm();

  (std::dynamic_pointer_cast<PaxosLogTmp>(node2->getLog()))->healthy = false;
  node1->leaderTransfer(2);

  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 2000);
  EXPECT_EQ(node2->getTerm(), term + 1);

  EXPECT_TRUE_EVENTUALLY(node2->getState() != Paxos::LEADER, 6000);
  EXPECT_EQ(node2->getTerm(), term + 2);
}

TEST_F(ThreePaxosLogTmpNodesCluster, AutoLeaderTransfer_switch_randomly) {
  // case 3: test whether target will be random
  (std::dynamic_pointer_cast<PaxosLogTmp>(node1->getLog()))->healthy = false;
  (std::dynamic_pointer_cast<PaxosLogTmp>(node2->getLog()))->healthy = false;
  (std::dynamic_pointer_cast<PaxosLogTmp>(node3->getLog()))->healthy = false;

  std::map<int, int> cnts;
  for (int i = 0; i < 10; ++i) {
    auto lastTerm = node1->getTerm();
    EXPECT_TRUE_EVENTUALLY(node1->getTerm() > lastTerm, 5000);
    // wait until some node becomes leader
    EXPECT_TRUE_EVENTUALLY(t.leaderExists(), 2000);
    cnts[t.getLeaderIndex() - 1]++;
    if (cnts.size() == 3) break;
  }

  EXPECT_NE(cnts[0], 10);
  EXPECT_NE(cnts[1], 10);
  EXPECT_NE(cnts[2], 10);
}

// This test case focuses on testing various error conditions during the leader
// transfer process in the Raft algorithm. It ensures that leader transfers to
// learners, non-existent nodes, itself, and followers produce the expected
// error codes and do not cause unexpected state transitions.
TEST_F(TwoNodesCluster, LeaderTransfer_error) {
  Paxos* node3 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(3));
  EXPECT_EQ_EVENTUALLY(node3->getState(), Paxos::LEARNER, 3000);

  // try to transfer leader to a learner, should fail
  EXPECT_EQ(node1->leaderTransfer(t.getNodeIpAddrRef(3)),
            PaxosErrorCode::PE_NOTFOLLOWER);
  EXPECT_EQ(node1->getState(), Paxos::LEADER);

  // try to transfer leader to a non-exist node, should fail
  EXPECT_EQ(node1->leaderTransfer(3), PaxosErrorCode::PE_NOTFOUND);
  EXPECT_EQ(node1->getState(), Paxos::LEADER);

  // try to transfer leader to itself, nothing happens
  EXPECT_EQ(node1->leaderTransfer(1), PaxosErrorCode::PE_NONE);
  EXPECT_EQ(node1->getState(), Paxos::LEADER);

  // try to transfer a follower, should fail
  EXPECT_EQ(node2->leaderTransfer(1), PaxosErrorCode::PE_NOTLEADR);
  EXPECT_EQ(node1->getState(), Paxos::LEADER);
}

// LeaderTransfer liveness tests, there are three cases:
// 1.Leader transfer failures will not affect cluster status and future leader
// transfer.
#ifdef FAIL_POINT
TEST_F(ThreeNodesCluster, LeaderTransferFail) {
  // First fault, early return
  auto fp = FailPointRegistry::getGlobalFailPointRegistry().find(
      "LeaderTransferFail");
  ASSERT_TRUE(fp != NULL && fp->name() == "LeaderTransferFail");
  // plan to let leader transfer fail twice, and we expect the cluster continues
  // to be normal
  fp->activate(FailPoint::finiteTimes, 2);

  uint64_t targetId = 2;
  node1->leaderTransfer(targetId);
  EXPECT_EQ_CONSISTENTLY(node1->getState(), Paxos::LEADER, 1000, 100);

  node1->leaderTransfer(targetId);
  EXPECT_EQ_CONSISTENTLY(node1->getState(), Paxos::LEADER, 1000, 100);

  node1->leaderTransfer(targetId);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 2000);

  fp->deactivate();
}

// LeaderTransfer liveness tests, there are three cases:
// 2.A leader transfer hang with lock will block this peer, and other peers will
// elect a new leader.
TEST_F(ThreeNodesCluster, LeaderTransferHangWithLock) {
  // hang with lock
  auto fp = FailPointRegistry::getGlobalFailPointRegistry().find(
      "LeaderTransferHangWithLock");
  ASSERT_TRUE(fp != NULL && fp->name() == "LeaderTransferHangWithLock");
  fp->activate(FailPoint::alwaysOn);

  std::thread transfer_thread1([&]() {
    auto ret = node1->leaderTransfer(3);  // will hang 10 seconds
    EXPECT_EQ(ret, 0);
  });
  // node1 'think' itself still the leader
  EXPECT_EQ_CONSISTENTLY(node1->getState(), Paxos::LEADER, 1000, 100);

  // The old leader node1 hung, node2 or node3 should be the new leader after
  // election timeout. The leader transfer process above made nonsense.
  EXPECT_TRUE_EVENTUALLY(
      node2->getState() == Paxos::LEADER || node3->getState() == Paxos::LEADER,
      10000);

  fp->deactivate();
  transfer_thread1.join();
}

// LeaderTransfer liveness tests, there are three cases:
// 3.A leader transfer hang without lock will not block this peer, and the
// leader transfer will succeed eventually.
TEST_F(ThreeNodesCluster, LeaderTransferHangWithoutLock) {
  // third fault, hang without lock
  auto fp = FailPointRegistry::getGlobalFailPointRegistry().find(
      "LeaderTransferHangWithoutLock");
  fp->activate(FailPoint::alwaysOn);

  std::thread transfer_thread2([&]() {
    auto ret = node1->leaderTransfer(2);  // will hang 10 seconds
    EXPECT_EQ(ret, 0);
  });

  // leader still the leader
  EXPECT_EQ_CONSISTENTLY(node1->getState(), Paxos::LEADER, 1000, 100);
  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::FOLLOWER, 10000);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 2000);

  fp->deactivate();
  transfer_thread2.join();
}

// The purpose of this test is to ensure that blocking the update of the commit
// index does not interfere with the leader transfer operation and that the
// commit indexes remain consistent across the nodes eventually.
TEST_F(ThreeNodesCluster,
       block_update_commit_index_do_not_affect_leader_transfer) {
  auto baseIdx = node1->getLastLogIndex();
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx, 2000);

  auto fp = FailPointRegistry::getGlobalFailPointRegistry().find(
      "SkipUpdateCommitIndex");
  fp->activate(FailPoint::alwaysOn);

  t.replicateLogEntry(1, 10, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       2000);

  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 10, 2000);
  EXPECT_EQ(node2->getCommitIndex(), baseIdx);

  node1->leaderTransfer(2);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 2000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 10, 2000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 11, 2000);

  fp->deactivate();
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 11, 2000);
}
#endif

TEST_F(ThreeNodesCluster, weight_election_highest_weight) {
  // configureMember
  node1->configureMember(1, false, 9);
  // bug: crash after configureMember but server not found
  node1->configureMember(10, false, 5);
  EXPECT_LOG_GET_METADATA_EQ(
      node1, Paxos::keyMemberConfigure,
      "127.0.0.1:11001#9;127.0.0.1:11002#5;127.0.0.1:11003#5@1", 1000);

  node1->leaderTransfer(3);
  EXPECT_EQ_EVENTUALLY(node3->getState(), Paxos::LEADER, 1000);

  // leader transferred to node1 because it has the highest election weight
  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::LEADER, 5000);
}

// electionWeight == 0 case
TEST_F(ThreeNodesCluster, weight_election_zero_weight) {
  node1->configureMember(1, false, 5);
  node1->configureMember(3, false, 0);
  EXPECT_LOG_GET_METADATA_EQ(
      node1, Paxos::keyMemberConfigure,
      "127.0.0.1:11001#5;127.0.0.1:11002#5;127.0.0.1:11003#0@1", 1000);

  // node3 cannot be leader
  node1->leaderTransfer(3);
  EXPECT_EQ_CONSISTENTLY(node1->getState(), Paxos::LEADER, 1000, 100);
  node3->requestVote();
  EXPECT_EQ_CONSISTENTLY(node1->getState(), Paxos::LEADER, 1000, 100);
}

TEST_F(ThreeNodesCluster, weight_election_live) {
  // configureMember
  node1->configureMember(2, false, 9);
  node1->configureMember(3, false, 1);

  EXPECT_LOG_GET_METADATA_EQ(
      node1, Paxos::keyMemberConfigure,
      "127.0.0.1:11001#5;127.0.0.1:11002#9;127.0.0.1:11003#1@1", 1000);

  t.destroyNode(1);

  node3->requestVote();
  EXPECT_EQ_EVENTUALLY(node3->getState(), Paxos::LEADER, 1000);

  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 4000);
}

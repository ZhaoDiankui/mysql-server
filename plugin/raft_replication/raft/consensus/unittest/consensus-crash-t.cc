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

// node1 has extra log entries in term 0 (index:1-3), node2/node3 only have
// index:1 in term 0; But node2 became leader in term 2. This case used to check
// if the node1 can truncate the log(2-3). We limit MaxPacketSize to 1 to let
// leader send one log entry for each time.
TEST_F(CustomizedThreeNodesCluster, follower_has_more_log) {
  createNodes123();

  // inject log entries to simulate state before crash
  t.appendLogEntryToLocalRDLog(1, 0, 3, "aaa");
  t.appendLogEntryToLocalRDLog(2, 0, 1, "aaa");
  t.appendLogEntryToLocalRDLog(3, 0, 1, "aaa");

  node2->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 2);
  node3->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 3);
  EXPECT_EQ_EVENTUALLY(node2->isConnected(t.getNodeIpAddr(3)), true, 2000);

  node2->setMaxPacketSize(1);

  EXPECT_EQ_EVENTUALLY(node2->isConnected(t.getNodeIpAddr(3)), true, 2000);
  node2->requestVote();
  EXPECT_TRUE_EVENTUALLY(node2->getState() == Paxos::LEADER, 2000);
  EXPECT_EQ_EVENTUALLY(node2->getLog()->getLastLogTerm(), node2->getTerm(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node3->getLog()->getLastLogTerm(), node2->getTerm(),
                       1000);

  const Paxos::StatsType& stats = node1->getStats();
  EXPECT_EQ_EVENTUALLY(stats.countTruncateBackward, 0, 1000);
  node1->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 1);
  EXPECT_EQ_EVENTUALLY(stats.countTruncateBackward, 1, 10000);

  t.replicateLogEntry(2, 2, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       2000);
  LogEntry checkEntry;
  node1->getLog()->getEntry(1, checkEntry);
  EXPECT_EQ(checkEntry.term(), 0);
  node1->getLog()->getEntry(2, checkEntry);
  EXPECT_EQ(checkEntry.term(), 2);
  node1->getLog()->getEntry(3, checkEntry);
  EXPECT_EQ(checkEntry.term(), 2);
  node1->getLog()->getEntry(4, checkEntry);
  EXPECT_EQ(checkEntry.term(), 2);
}

#ifdef FAIL_POINT
// node1 has more log in term 0 (index:1-3), node2/node3 only have index:1
// in term 0; But node1 became leader in term 2. We limit MaxPacketSize to 1
// to let leader send on log for each time. We should not push
// matchIndex/commitIndex when we not send this term's log entry to the
// follower.
TEST_F(CustomizedThreeNodesCluster, leader_has_more_log) {
  createNodes123();

  // inject log entries to simulate state before crash
  t.appendLogEntryToLocalRDLog(1, 0, 3, "aaa");
  t.appendLogEntryToLocalRDLog(2, 0, 1, "aaa");
  t.appendLogEntryToLocalRDLog(3, 0, 1, "aaa");

  initNodes123();

  // this fp has no effect to election, but will pause commit
  node1->setMaxPacketSize(1);
  FailPointData data(2);
  auto fpLimitMaxSendLogIndex =
      FailPointRegistry::getGlobalFailPointRegistry().find(
          "LimitMaxSendLogIndex");
  fpLimitMaxSendLogIndex->activate(FailPoint::alwaysOn, 0, data);

  // wait until connections to node2 and node3 established
  EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(2)), true, 2000);
  EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(3)), true, 2000);
  node1->requestVote(true);
  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::LEADER, 10000);

  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), 0, 500, 100);

  fpLimitMaxSendLogIndex->deactivate();

  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), 4, 3000);
}
#endif

// node1 has more log in term 0 (index:1-3), node2/node3 only have index:1
// in term 0, 2-3 in term 1; This case used to check if the node2 can send
// 2-3 to node1, and the node1 can truncate the log(2-3). We limit
// MaxPacketSize to 1 to let leader send on log for each time.
TEST_F(CustomizedThreeNodesCluster, follower_has_unmatched_log) {
  createNodes123();

  // inject log entries to simulate state before crash
  t.appendLogEntryToLocalRDLog(1, 0, 3, "aaa");
  t.appendLogEntryToLocalRDLog(2, 0, 1, "aaa");
  t.appendLogEntryToLocalRDLog(2, 1, 2, "aaa");
  t.appendLogEntryToLocalRDLog(3, 0, 1, "aaa");

  node2->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 2);
  node3->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 3);

  node2->setMaxPacketSize(1);

  EXPECT_EQ_EVENTUALLY(node2->isConnected(t.getNodeIpAddr(3)), true, 2000);
  node2->requestVote();
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 2000);
  EXPECT_EQ_EVENTUALLY(node2->getLog()->getLastLogTerm(), node2->getTerm(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node3->getLog()->getLastLogTerm(), node2->getTerm(),
                       1000);

  const Paxos::StatsType& stats = node1->getStats();
  EXPECT_EQ_EVENTUALLY(stats.countTruncateBackward, 0, 1000);
  node1->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 1);
  node1->requestVote();  // wont't become leader even request vote is sent
  EXPECT_EQ_EVENTUALLY(stats.countTruncateBackward, 1, 1000);

  t.replicateLogEntry(2, 2, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       1000);
  LogEntry checkEntry;
  node1->getLog()->getEntry(2, checkEntry);
  EXPECT_EQ(checkEntry.term(), 1);
  node1->getLog()->getEntry(3, checkEntry);
  EXPECT_EQ(checkEntry.term(), 1);
  node1->getLog()->getEntry(4, checkEntry);
  EXPECT_EQ(checkEntry.term(), 2);
}

// node1 has more log in term 1 (index:1-10), but 1-3 is mock log.
// node2/node3 have index:1-5 in term 1;
// node1 became leader in term 2, maxDelayIndex is 2, node2/node3 became
// disable pipeling mode We should make sure leader will not read the mock log
// in this case.
TEST_F(CustomizedThreeNodesCluster, may_read_mock_log) {
  createNodes123();

  auto log1 = node1->getLog();
  auto log2 = node2->getLog();
  auto log3 = node3->getLog();

  for (int i = 1; i <= 3; i++)
    log1->append(t.constructTestEntry(0, kMock, i, "aaa", "bbb"));
  for (int i = 4; i <= 10; i++)
    log1->append(t.constructTestEntry(1, kNormal, i, "aaa", "bbb"));

  for (int i = 1; i <= 5; i++) {
    log2->append(t.constructTestEntry(1, kNormal, i, "aaa", "bbb"));
    log3->append(t.constructTestEntry(1, kNormal, i, "aaa", "bbb"));
  }

  initNodes123();

  node1->setMaxPacketSize(2);
  node1->setMaxDelayIndex(2);
  ensureNode1Elected();

  // check log1's log
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), 11, 1000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), 11, 1000);
  EXPECT_EQ_EVENTUALLY(node3->getLastLogIndex(), 11, 1000);

  // entry 1-2 in node2 and node2 won't be covered
  LogEntry checkEntry;
  log2->getEntry(1, checkEntry, false);
  EXPECT_EQ(checkEntry.term(), 1);
  EXPECT_EQ(checkEntry.optype(), kNormal);
  log2->getEntry(2, checkEntry, false);
  EXPECT_EQ(checkEntry.term(), 1);
  EXPECT_EQ(checkEntry.optype(), kNormal);
  log2->getEntry(3, checkEntry, false);
  EXPECT_EQ(checkEntry.term(), 1);
  EXPECT_EQ(checkEntry.optype(), kNormal);
}

// leader has sent log to follower, and follower's match index has proceeded,
// but follower fails to write the log to disk due to crash.
TEST_F(ThreeNodesCluster, follower_lose_log) {
  auto baseIdx = node1->getLastLogIndex();

  t.replicateLogEntry(1, 2, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 2, 3000);

  node1->setEnableAutoResetMatchIndex(true);

  // fake follower's crash
  auto log2 = node2->getLog();
  EXPECT_EQ_EVENTUALLY(log2->getLastLogIndex(), baseIdx + 2, 3000);
  t.destroyNode(2);
  // erase log entry baseIdx + 1 and baseIdx + 2
  log2->truncateBackward(baseIdx + 1);
  EXPECT_EQ(log2->getLastLogIndex(), baseIdx);
  node2 = t.addNodeAtIndex(2, new Paxos(2000, log2))
              .initAsMember(2, t.getNodesIpAddrVectorRef3(1, 2, 3));

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 3, 3000);
  EXPECT_EQ_EVENTUALLY(log2->getLastLogIndex(), baseIdx + 3, 3000);
}

#ifdef FAIL_POINT
TEST_F(ThreeNodesCluster, leader_truncate_configure_change) {
  auto baseIdx = node1->getLastLogIndex();

  t.replicateLogEntry(1, 2, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 2, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 2, 3000);

  // leader won't send out log entries with index larger than baseIdx + 2
  FailPointData data(baseIdx + 2);
  auto fpLimitMaxSendLogIndex =
      FailPointRegistry::getGlobalFailPointRegistry().find(
          "LimitMaxSendLogIndex");
  fpLimitMaxSendLogIndex->activate(FailPoint::alwaysOn, 0, data);

  // init a configuration change
  std::atomic<bool> done(false);
  std::thread op([&]() {
    std::vector<std::string> v = t.getNodesIpAddrVectorRef1(5);
    node1->changeLearners(Paxos::CCAddNode, v);
    done.store(true);
  });

  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 3, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 2, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getLastLogIndex(), baseIdx + 2, 3000);

  EXPECT_EQ_CONSISTENTLY(done.load(), false, 500, 100);

  // switch leader
  node2->requestVote(true);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 3000);
  fpLimitMaxSendLogIndex->deactivate();

  op.join();  // changeLearners is aborted

  // the configuration change log entires on node1 will be truncated
  t.replicateLogEntry(2, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 4, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 4, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getLastLogIndex(), baseIdx + 4, 3000);
}

TEST_F(FiveNodesCluster, leader_should_not_commit_last_term_entry_directly) {
  auto baseIdx = node1->getLastLogIndex();

  // keep the paxos1 as leader
  Paxos::debugDisableStepDown = true;
  Paxos::debugDisableElection = true;

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getLastLogIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node5->getLastLogIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node4->getCommitIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node5->getCommitIndex(), baseIdx + 1, 3000);

  // leave 2 peers
  t.destroyNode(3);
  t.destroyNode(4);
  t.destroyNode(5);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 2, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 2, 3000);
  EXPECT_EQ(node1->getCommitIndex(), baseIdx + 1);
  EXPECT_EQ(node2->getCommitIndex(), baseIdx + 1);

  // only increase term
  node1->fakeRequestVote();

  // recover the cluster
  node3 = t.createRDLogNodeAtIndex(3).initAsMember(
      3, t.getNodesIpAddrVectorRef5(1, 2, 3, 4, 5));
  node4 = t.createRDLogNodeAtIndex(4).initAsMember(
      4, t.getNodesIpAddrVectorRef5(1, 2, 3, 4, 5));
  node5 = t.createRDLogNodeAtIndex(5).initAsMember(
      5, t.getNodesIpAddrVectorRef5(1, 2, 3, 4, 5));

  // wait until a peer (such as peer5) recovers as a follower before continuing
  // the test.
  EXPECT_EQ_EVENTUALLY(node5->getState(), Paxos::FOLLOWER, 3000);

  // The leader should not consider entries from last term committed
  // automatically, or some bad cases may happen the reason is explained in the
  // raft paper: "In Search of an Understandable Consensus Algorithm",
  // chapter 5.4.2 "Committing entries from previous terms".

  // The leader's term increases. it determine commitment using log entries
  // from older terms. some bad case may occur. here is an example:

  // There are 5 peers in a raft group, and the leader is P1, P2-P5 are
  // followers. P1 writes a log entry with log index 1 and term 1, we name it
  // (a,1,1), and only P2 receives it. P3, P4 and P5 do not receive it for some
  // reason such as network partition. P1       P2       | P3     P4     P5
  // (a,1,1)  (a,1,1)  | -      -      -

  // P5 can not receive the leader's heartbeat, so it starts an election and
  // receives votes from P3 and P4. P5 becomes the leader and writes a log entry
  // b with log index 1 and term 2, we name it (b,1,2), then P5 crashes. No peer
  // in the cluster receives (b,1,2). P1       P2       | P3     P4     P5
  // (a,1,1)  (a,1,1)  | -      -      (b,1,2)

  // And now the network is recovered, and P1 becomes the leader again with
  // term 3. It starts to replicate log entries (a,1,1), and P3 receives it.
  // (a,1,1) is replicated to a majority of the cluster, and can be committed.
  // P1       P2       P3       P4     P5
  // (a,1,1)  (a,1,1)  (a,1,1)  -      (b,1,2)

  // And now P1 crashes, P5 could be elected leader with term 4, because it has
  // the biggest term and most up-to-date log(log index 1). P5 will replicate
  // (b,1,2) and overwrite (a,1,1) on P1, P2 and P3. P1       P2       P3 P4 P5
  // (b,1,2)  (b,1,2)  (b,1,2)  (b,1,2)  (b,1,2)

  // So a leader can't conclude that an entry from a previous term is committed
  // once it is stored on a majority of servers. Because these entries may still
  // be overwritten by a future leader.

  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node4->getCommitIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node5->getCommitIndex(), baseIdx + 1, 3000);

  // However, if P1 replicates an entry from its current term on a majority of
  // the servers before crashing, then this entry can be committed safely (P5
  // cannot win an election). At this point all preceding entries in the log
  // are committed as well.
  // P1       P2       P3       P4       P5
  // (a,1,1)  (a,1,1)  (a,1,1)  -        (b,1,2)
  // (c,2,3)  (a,2,3)  (a,2,3)  -        -

  t.appendLogEntryToLocalRDLog(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 3, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 3, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), baseIdx + 3, 3000);
  EXPECT_EQ_EVENTUALLY(node4->getCommitIndex(), baseIdx + 3, 3000);
  EXPECT_EQ_EVENTUALLY(node5->getCommitIndex(), baseIdx + 3, 3000);

  // static variables should be reset
  Paxos::debugDisableStepDown = false;
  Paxos::debugDisableElection = false;

}

TEST_F(FiveNodesCluster, leader_should_commit_current_term_entry_directly) {
  auto baseIdx = node1->getLastLogIndex();

  // keep the paxos1 as leader
  Paxos::debugDisableStepDown = true;
  Paxos::debugDisableElection = true;

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getLastLogIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node5->getLastLogIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node4->getCommitIndex(), baseIdx + 1, 3000);
  EXPECT_EQ_EVENTUALLY(node5->getCommitIndex(), baseIdx + 1, 3000);

  // leave 2 peers
  t.destroyNode(3);
  t.destroyNode(4);
  t.destroyNode(5);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), baseIdx + 2, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 2, 3000);
  EXPECT_EQ(node1->getCommitIndex(), baseIdx + 1);
  EXPECT_EQ(node2->getCommitIndex(), baseIdx + 1);

  // recover the cluster
  node3 = t.createRDLogNodeAtIndex(3).initAsMember(
      3, t.getNodesIpAddrVectorRef5(1, 2, 3, 4, 5));
  node4 = t.createRDLogNodeAtIndex(4).initAsMember(
      4, t.getNodesIpAddrVectorRef5(1, 2, 3, 4, 5));
  node5 = t.createRDLogNodeAtIndex(5).initAsMember(
      5, t.getNodesIpAddrVectorRef5(1, 2, 3, 4, 5));

  // wait until a peer (such as peer5) recovers as a follower before continuing
  // the test.
  EXPECT_EQ_EVENTUALLY(node5->getState(), Paxos::FOLLOWER, 3000);

  // Diffent from above case, the leader's term keeps unchanged, so it is safe
  // to continue to replicate and commit the log entries it wrote in this term.
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 2, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 2, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), baseIdx + 2, 3000);
  EXPECT_EQ_EVENTUALLY(node4->getCommitIndex(), baseIdx + 2, 3000);
  EXPECT_EQ_EVENTUALLY(node5->getCommitIndex(), baseIdx + 2, 3000);

  // static variables should be reset
  Paxos::debugDisableStepDown = false;
  Paxos::debugDisableElection = false;
}
#endif

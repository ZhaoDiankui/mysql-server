// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include "paxos.h"
#include "rd_paxos_log.h"
#include "ut_helper.h"

using namespace alisql;

// add a learner with some extra log entries,
// extra log entries should be truncated
TEST_F(SingleNodeCluster, learner_with_extra_log_entries) {
  EXPECT_EQ(node1->getLastLogIndex(), 1);

  Paxos* node2 = t.createRDLogNode().getLastNode();
  auto log2 = std::dynamic_pointer_cast<RDPaxosLog>(node2->getLog());
  LogEntry regularEntry = t.constructTestEntry(1, kNormal);
  log2->debugSetLastLogIndex(3);
  log2->append(regularEntry);
  log2->append(regularEntry);
  EXPECT_EQ(log2->getLastLogIndex(), 5);

  LogEntry checkEntry;
  log2->getEntry(5, checkEntry, false);
  EXPECT_EQ(checkEntry.index(), 5);
  EXPECT_EQ(checkEntry.term(), 1);

  t.initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(2));
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), 2, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), 2, 3000);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), 3, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), 3, 3000);
}

// add a learner with unmatched log entries,
// unmatched log entries won't be truncated
TEST_F(SingleNodeCluster, learner_with_unmatched_log_entries) {
  t.replicateLogEntry(1, 5, "aaa");
  EXPECT_EQ(node1->getLastLogIndex(), 6);

  Paxos* node2 = t.createRDLogNode().getLastNode();
  auto log2 = std::dynamic_pointer_cast<RDPaxosLog>(node2->getLog());
  LogEntry regularEntry = t.constructTestEntry(1, kNormal);
  log2->debugSetLastLogIndex(2);
  log2->append(regularEntry);
  EXPECT_EQ(log2->getLastLogIndex(), 3);

  LogEntry checkEntry;
  node1->getLog()->getEntry(3, checkEntry, false);
  EXPECT_EQ(checkEntry.index(), 3);
  EXPECT_EQ(checkEntry.term(), 2);
  log2->getEntry(3, checkEntry, false);
  EXPECT_EQ(checkEntry.index(), 3);
  EXPECT_EQ(checkEntry.term(), 1);

  t.initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(2));
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), 7, 3000);
  EXPECT_EQ_CONSISTENTLY(node2->getLastLogIndex(), 3, 500, 100);
}

// add a learner with some extra log entries, however, the learner does't have a
// log entry with leader's prev index because the learners log has some holes.
// the extra log entries won't be truncated
TEST_F(SingleNodeCluster, learner_with_extra_log_entries_and_holes) {
  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ(node1->getLastLogIndex(), 2);

  Paxos* node2 = t.createRDLogNode().getLastNode();
  auto log2 = std::dynamic_pointer_cast<RDPaxosLog>(node2->getLog());
  LogEntry regularEntry = t.constructTestEntry(1, kNormal);
  // node2 doesn't have a log entry with index 1 and 2, there is a hole
  log2->debugSetLastLogIndex(3);
  log2->append(regularEntry);
  EXPECT_EQ(log2->getLastLogIndex(), 4);

  LogEntry checkEntry;
  node1->getLog()->getEntry(1, checkEntry, false);
  EXPECT_EQ(checkEntry.index(), 1);
  EXPECT_EQ(checkEntry.term(), 2);
  EXPECT_EQ(log2->getEntry(1, checkEntry, false), -1);  // doesn't exist

  t.initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(2));
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), 3, 3000);
  EXPECT_EQ_CONSISTENTLY(node2->getLastLogIndex(), 4, 500, 100);
}

// add follower with unmatched log entries,
// the follower's unmatched log entries should be truncated
TEST_F(CustomizedThreeNodesCluster, follower_with_unmatched_log_entries) {
  createNodes123();
  node1->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 1);
  node2->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 2);
  EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(2)), true, 2000);
  node1->requestVote();
  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::LEADER, 2000);
  EXPECT_EQ_EVENTUALLY(node1->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node2->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);

  t.replicateLogEntry(1, 5, "aaa");

  // init a follower with some unmatched log entries
  auto log3 = std::dynamic_pointer_cast<RDPaxosLog>(node3->getLog());

  LogEntry regularEntry = t.constructTestEntry(1, kNormal);
  log3->debugSetLastLogIndex(2);
  log3->append(regularEntry);
  EXPECT_EQ(log3->getLastLogIndex(), 3);

  LogEntry checkEntry;
  node1->getLog()->getEntry(3, checkEntry, false);
  EXPECT_EQ(checkEntry.index(), 3);
  EXPECT_EQ(checkEntry.term(), 2);
  log3->getEntry(3, checkEntry, false);
  EXPECT_EQ(checkEntry.index(), 3);
  EXPECT_EQ(checkEntry.term(), 1);

  node3->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 3);

  // follower's log should be truncated
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node3->getLastLogIndex(),
                       2000);

  t.replicateLogEntry(1, 5, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node3->getLastLogIndex(),
                       2000);
}

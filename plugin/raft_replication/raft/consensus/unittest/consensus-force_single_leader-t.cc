// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include "paxos.h"
#include "ut_helper.h"

using namespace alisql;

TEST_F(ThreeNodesCluster, force_leader) {
  auto baseCommitIndex = node1->getCommitIndex();

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseCommitIndex + 1, 1000);

  t.destroyNode(1);  // node1
  t.destroyNode(2);  // node2
  EXPECT_EQ_EVENTUALLY(node3->getState(), Paxos::CANDIDATE, 3000);

  node3->forceSingleLeader();
  EXPECT_EQ_EVENTUALLY(node3->getState(), Paxos::LEADER, 1000);
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), baseCommitIndex + 2, 1000);

  t.replicateLogEntry(3, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), baseCommitIndex + 3, 1000);
}

TEST_F(ThreeNodesCluster, force_learner) {
  auto baseCommitIndex = node1->getCommitIndex();

  Paxos* node4 = t.createFileLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseCommitIndex + 2, 1000);
  EXPECT_EQ_EVENTUALLY(node4->getState(), Paxos::LEARNER, 3000);
  EXPECT_EQ(node1->getClusterSize(), 4);

  node4->forceSingleLeader();
  EXPECT_EQ_EVENTUALLY(node4->getState(), Paxos::LEADER, 1000);

  t.replicateLogEntry(4, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node4->getCommitIndex(), baseCommitIndex + 4, 1000);
  EXPECT_EQ(node4->getClusterSize(), 1);
  EXPECT_EQ(node1->getClusterSize(), 4);

  // check old cluster
  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseCommitIndex + 3, 1000);

  // check new cluster
  t.replicateLogEntry(4, 1, "aaa");
  // onAppendLog msg from old leader will be rejected since it's not in the
  // configuration
  EXPECT_EQ_CONSISTENTLY(node4->getCommitIndex(), baseCommitIndex + 5, 1000,
                         100);
  EXPECT_EQ(node4->getClusterSize(), 1);
  EXPECT_EQ(node1->getClusterSize(), 4);
}

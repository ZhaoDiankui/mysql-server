// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include "paxos.h"
#include "ut_helper.h"

using namespace alisql;

TEST_F(SingleNodeCluster, DelNode_delete_leader) {
  EXPECT_EQ(node1->changeMember(Paxos::CCDelNode, t.getNodeIpAddrRef(1)), 0);
}

TEST_F(ThreeNodesCluster, DelNode_delete_leader_of_3_node_cluster) {
  EXPECT_EQ(node1->changeMember(Paxos::CCDelNode, t.getNodeIpAddrRef(1)), 0);
  EXPECT_TRUE_EVENTUALLY((node2->getState() == Paxos::LEADER ||
                          node3->getState() == Paxos::LEADER),
                         5000);
  EXPECT_EQ_EVENTUALLY(node2->getServerNum(), 2, 3000);
  EXPECT_EQ_EVENTUALLY(node3->getServerNum(), 2, 3000);
}

TEST_F(TwoNodesCluster, DelNode_not_existed_node) {
  EXPECT_EQ(node1->changeMember(Paxos::CCDelNode, t.getNodeIpAddrRef(3)),
            PaxosErrorCode::PE_NOTFOUND);

  EXPECT_EQ(node1->changeMember(Paxos::CCDelNode, t.getNodeIpAddrRef(2)), 0);
  EXPECT_EQ(node1->getClusterSize(), 1);
  EXPECT_EQ(node1->changeMember(Paxos::CCDelNode, t.getNodeIpAddrRef(2)),
            PaxosErrorCode::PE_NOTFOUND);
}

TEST_F(SingleNodeCluster, DelNode_delete_learner_use_change_member) {
  std::ignore = t.createRDLogNode().initAsLearner();
  EXPECT_EQ(
      node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(2)),
      0);
  EXPECT_EQ(node1->getLearnerNum(), 1);

  EXPECT_EQ(node1->changeMember(Paxos::CCDelNode, t.getNodeIpAddrRef(2)),
            PE_NOTFOUND);
  EXPECT_EQ(node1->getLearnerNum(), 1);

  EXPECT_EQ(
      node1->changeLearners(Paxos::CCDelNode, t.getNodesIpAddrVectorRef1(2)),
      0);
  EXPECT_EQ(node1->getLearnerNum(), 0);
}

TEST_F(TwoNodesCluster, DelNode_delete_member_use_change_learners) {
  EXPECT_EQ(node1->getServerNum(), 2);

  EXPECT_EQ(
      node1->changeLearners(Paxos::CCDelNode, t.getNodesIpAddrVectorRef1(2)),
      PE_NOTFOUND);
  EXPECT_EQ(node1->getServerNum(), 2);
  EXPECT_EQ(node1->changeMember(Paxos::CCDelNode, t.getNodeIpAddrRef(2)), 0);
  EXPECT_EQ(node1->getServerNum(), 1);
}
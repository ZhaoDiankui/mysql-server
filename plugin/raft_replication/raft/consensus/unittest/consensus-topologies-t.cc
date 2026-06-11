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

/*
 * corner case, only has one node in cluster
 * tests leader election, this node will become leader
 * also tests leader election when election timer expires
 */
TEST(CustomizedCluster, Paxos_add_node_corner_case1) {
  PaxosTestbed t;
  t.initRDLogNodes(1);
  Paxos* node1 = t.getNode(1);

  /* only has one node in cluster */
  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::FOLLOWER, 2000);

  /* election timer expires, election is automatically triggered */
  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::LEADER, 6000);
}

TEST(CustomizedCluster, Paxos_add_node_corner_case2) {
  PaxosTestbed t;
  t.initRDLogNodes(1);
  Paxos* node1 = t.getNode(1);

  /* only has one node in cluster */
  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::FOLLOWER, 2000);

  /* trigger election manually */
  node1->requestVote();
  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::LEADER, 3000);
}

TEST_F(SingleNodeCluster, oneNode) {
  t.replicateLogEntry(1, 2, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node1->getCommitIndex(), 1000);

  // add node in one node cluster.
  Paxos* learner1 = t.createFileLogNode().initAsLearner();
  Paxos* learner2 = t.createFileLogNode().initAsLearner();

  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef2(2, 3));

  EXPECT_EQ_EVENTUALLY(node1->learnersToString(),
                       std::string("127.0.0.1:11002$0;127.0.0.1:11003$0"),
                       1000);

  node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(2));
  node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(3));

  EXPECT_EQ_EVENTUALLY(node1->getServerNum(), 3, 1000);
  EXPECT_EQ_EVENTUALLY(node1->getConfig()->getServerNumLockFree(), 3, 1000);
  EXPECT_EQ_EVENTUALLY(
      node1->membersToString(("127.0.0.1:11001")),
      std::string("127.0.0.1:11001#5;127.0.0.1:11002#5;127.0.0.1:11003#5@1"),
      1000);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(learner1->getState(), Paxos::FOLLOWER, 3000);
  EXPECT_EQ_EVENTUALLY(learner2->getState(), Paxos::FOLLOWER, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), node1->getLastLogIndex(), 3000);
  EXPECT_EQ_EVENTUALLY(learner1->getCommitIndex(), node1->getLastLogIndex(),
                       3000);
  EXPECT_EQ_EVENTUALLY(learner2->getCommitIndex(), node1->getLastLogIndex(),
                       3000);

  EXPECT_EQ(learner1->learnersToString(), std::string(""));
  EXPECT_EQ(
      learner1->membersToString(("127.0.0.1:11002")),
      std::string("127.0.0.1:11001#5;127.0.0.1:11002#5;127.0.0.1:11003#5@2"));
}
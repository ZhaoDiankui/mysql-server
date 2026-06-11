// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include "files.h"
#include "paxos.h"
#include "rd_paxos_log.h"
#include "ut_helper.h"

using namespace alisql;

TEST_F(CustomizedThreeNodesCluster, purgeLog) {
  createNodes123();
  initNodes123();
  node1->initAutoPurgeLog(true);
  ensureNode1Elected();

  t.replicateLogEntry(1, 2, "aaa");
  // wait replicate
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), 3, 3000);
  node1->updateAppliedIndex(node1->getCommitIndex() - 1);  // purge node1

  // wait purge log, default purge log timeout is 3s
  EXPECT_EQ_EVENTUALLY(node1->getLog()->getLength(), 2, 5000);
}

TEST_F(CustomizedThreeNodesCluster, purgeLog_forcepurge) {
  createNodes123();
  initNodes123();
  node1->initAutoPurgeLog(true);
  ensureNode1Elected();

  t.replicateLogEntry(1, 2, "aaa");
  // wait replicate
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), 3, 3000);
  node1->updateAppliedIndex(node1->getCommitIndex() - 1);  // purge node1

  // wait purge log, default purge log timeout is 2s
  EXPECT_TRUE_CONSISTENTLY(node1->getLog()->getLength() != 2, 2000, 100);
  // test force purge log here
  node1->forcePurgeLog(false, 1000);

  EXPECT_EQ_EVENTUALLY(node1->getLog()->getLength(), 2, 5000);
}

TEST_F(CustomizedThreeNodesCluster, purgeLog_disable_appliedIndex) {
  createNodes123();
  initNodes123();
  // disable appliedIndex use commitIndex
  node1->initAutoPurgeLog(true, false, NULL);
  ensureNode1Elected();

  t.replicateLogEntry(1, 2, "aaa");
  // wait replicate
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), 3, 3000);
  // wait purge log
  EXPECT_EQ_EVENTUALLY(node1->getLog()->getLength(), 1, 5000);
}

TEST_F(CustomizedThreeNodesCluster, purgeLog_local) {
  createNodes123();
  initNodes123();
  node1->initAutoPurgeLog(false, false, NULL);  // disable autoPurge
  node2->initAutoPurgeLog(false, false, NULL);  // disable autoPurge
  ensureNode1Elected();

  t.replicateLogEntry(1, 1, "aaa");

  std::ignore = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));

  node1->configureLearner(101, 1);
  EXPECT_EQ_EVENTUALLY(node1->learnersToString(),
                       std::string("127.0.0.1:11004$1"), 1000);

  uint64_t len = node1->getLog()->getLength();
  node1->forceFixMatchIndex(101, 2);
  node1->forcePurgeLog(true /* local */);
  EXPECT_EQ_EVENTUALLY(node1->getLog()->getLength(), len - 2, 5000);

  /* test follower purge local */
  node1->configureLearner(101, 2);

  t.replicateLogEntry(1, 1, "aaa");
  // node2 has received the latest log entries
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), node1->getLastLogIndex(), 6000)
  // A follower's log should only be purged when the its downstream learner has
  // received the log
  EXPECT_EQ_EVENTUALLY(std::dynamic_pointer_cast<RemoteServer>(
                           node2->getConfig()->getServer(101))
                           ->matchIndex,
                       node2->getLastLogIndex(), 6000);
  node2->forcePurgeLog(true /* local */);
  EXPECT_EQ_EVENTUALLY(node2->getLog()->getLength(), 1, 5000);

  t.destroyNode(4);
  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node2->getLog()->getLength(), 2, 1000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), node1->getLastLogIndex(),
                       6000);
  node2->forcePurgeLog(true);
  EXPECT_EQ_EVENTUALLY(node2->getLog()->getLength(), 2,
                       5000);  // still 2 because learner is shutdown
}

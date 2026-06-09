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

TEST_F(ThreeNodesCluster, forceSync) {
  // configureMember
  node1->configureMember(3, true, 6);
  EXPECT_LOG_GET_METADATA_EQ(
      node1, Paxos::keyMemberConfigure,
      "127.0.0.1:11001#5;127.0.0.1:11002#5;127.0.0.1:11003#6S@1", 1000);
  EXPECT_LOG_GET_METADATA_EQ(
      node2, Paxos::keyMemberConfigure,
      "127.0.0.1:11001#5;127.0.0.1:11002#5;127.0.0.1:11003#6S@2", 1000);
  t.destroyNode(3);

  auto baseIdx = node1->getLastLogIndex();
  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), baseIdx, 500, 100);
  EXPECT_EQ(node1->getForceSyncStatus(), 1);

  // Force sync is disabled after election timeout.
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 1, 4000);
  EXPECT_EQ(node1->getForceSyncStatus(), 0);

  baseIdx = node1->getLastLogIndex();
  // check force sync after change leader
  node1->leaderTransfer(2);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 1000);
  EXPECT_EQ_CONSISTENTLY(node2->getCommitIndex(), baseIdx, 500, 100);
  EXPECT_EQ(node2->getForceSyncStatus(), 1);

  // Force sync is disabled after election timeout.
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 1, 4000);
  EXPECT_EQ(node2->getForceSyncStatus(), 0);

  // check force sync after change leader
  baseIdx = node2->getLastLogIndex();
  node1->setForceSyncEpochDiff(1000);
  node2->leaderTransfer(1);
  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::LEADER, 1000);
  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), baseIdx, 500, 100);
  EXPECT_EQ(node1->getForceSyncStatus(), 1);

  // Force sync is not disabled after election timeout.
  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), baseIdx, 4000, 100);
  EXPECT_EQ(node1->getForceSyncStatus(), 1);

  node1->setForceSyncEpochDiff(0);
  // Force sync is disabled after election timeout.
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 1, 4000);
  EXPECT_EQ(node1->getForceSyncStatus(), 0);
}

// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include <atomic>
#include <string>
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

/*
 * testcase list:
 *  followers should keep consistency in all cases
 *  normal case: large trx commitindex update atomically
 *  leader crash during a large trx (cannot transfer leader during a large trx )
 *  write new logs during a large trx recovery
 *  debug very slow case:
 *    just very slow, keep heartbeat
 *    leader transfer during a large trx recovery (weight election should also
 * not work) crash during a large trx recovery another leader requestvote during
 * a large trx recovery
 */

// show the basic usage of kCommitDep and kCommitDepEnd log entry.
TEST_F(ThreeNodesCluster, trx_commit_dependency) {
  LogEntry le = t.constructTestEntry(0, kNormal);
  node1->replicateLog(le);
  uint64_t lastNormalIndex = le.index();
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), lastNormalIndex, 1000);
  t.replicateLogEntry(1, 10, "commitDep", kCommitDep);
  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), lastNormalIndex, 1000, 100);
  t.replicateLogEntry(1, 1, "commitDepEnd", kCommitDepEnd);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), lastNormalIndex + 11, 1000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), node1->getLastLogIndex(), 1000);
  t.replicateLogEntry(1, 1, "normal");
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), lastNormalIndex + 12, 1000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), node2->getCommitIndex(), 6000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), node3->getCommitIndex(), 6000);
}

// kCommitDep entries can be committed when a normal entry with larger index is
// committed
TEST_F(ThreeNodesCluster, trx_commit_dependency_log_committed_by_normal_log) {
  uint64_t lastNormalIndex = node1->getLastLogIndex();
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), lastNormalIndex, 1000);
  t.replicateLogEntry(1, 10, "commitDep", kCommitDep);
  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), lastNormalIndex, 1000, 100);
  EXPECT_EQ_CONSISTENTLY(node1->getLastLogIndex(), lastNormalIndex + 10, 1000,
                         100);
  uint64_t lastLogIndex = node1->getLastLogIndex();
  t.replicateLogEntry(1, 1, "normal", kNormal);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), lastLogIndex + 1, 1000);
  for (int i = 0; i < 10; ++i) {
    LogEntry le;
    node1->getLog()->getEntry(lastLogIndex - i, le, false);
    EXPECT_EQ(le.optype(), kCommitDep);
  }
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), node1->getLastLogIndex(), 2000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), node2->getCommitIndex(), 2000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), node3->getCommitIndex(), 2000);
}

// When the leader changes, the new leader will replace kCommitDep entries
// with normal type empty entries
TEST_F(ThreeNodesCluster, trx_commit_dependency_reset_by_leader_change) {
  uint64_t lastNormalIndex = node1->getLastLogIndex();
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), lastNormalIndex, 1000);
  t.replicateLogEntry(1, 10, "commitDep", kCommitDep);
  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), lastNormalIndex, 1000, 100);
  t.ensureGivenNodeElected(2);
  // 10 kCommitDep entries and a leader transfer
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), lastNormalIndex + 10 + 1,
                       3000);
  uint64_t lastLogIndex = node2->getLastLogIndex();
  t.replicateLogEntry(2, 1, "normal", kNormal);
  for (int i = 0; i < 10; ++i) {
    LogEntry le;
    node2->getLog()->getEntry(lastLogIndex - i, le, false);
    EXPECT_NE(le.optype(), kCommitDep);
  }
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), node2->getLastLogIndex(), 2000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), node2->getLastLogIndex(), 2000);
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), node2->getLastLogIndex(), 2000);
}

// When a leader crashes, the new leader will replace kCommitDep entries
// with normal type empty entries
TEST_F(ThreeNodesCluster, commit_dependency_reset_by_leader_crash) {
  uint64_t lastNormalIndex = node1->getLastLogIndex();
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), lastNormalIndex, 1000);
  t.replicateLogEntry(1, 10, "commitDep", kCommitDep);
  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), lastNormalIndex, 1000, 100);
  t.destroyNode(1);
  EXPECT_TRUE_EVENTUALLY(t.getLeaderIndex() != -1 && t.getLeaderIndex() != 1,
                         10000);

  auto leader = t.getLeader();
  if (leader == nullptr) return;
  // 10 kCommitDep entries and a leader election
  EXPECT_EQ_EVENTUALLY(leader->getLastLogIndex(), lastNormalIndex + 10 + 1,
                       3000);
  uint64_t lastLogIndex = leader->getLastLogIndex();
  LogEntry insertEntry = t.constructTestEntry(0, kNormal);
  leader->replicateLog(insertEntry);
  for (int i = 0; i < 10; ++i) {
    LogEntry checkEntry;
    leader->getLog()->getEntry(lastLogIndex - i, checkEntry, false);
    EXPECT_NE(checkEntry.optype(), kCommitDep);
  }
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), lastLogIndex + 1, 2000);
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), lastLogIndex + 1, 2000);
}

extern easy_log_level_t easy_log_level;
easy_log_level_t save_easy_log_level;

#ifdef FAIL_POINT
// Simulate a situation that a large trx with a lot of kCommitDep entries.
// A leader transfer or leader crash may cause a very slow recovery and no
// entry can be replicated. However, after the recovery, all 3 nodes' kCommitDep
// should have be reset and keep consistent.
// Now, the slow recovery may cause leader step down, see:
// https://github.com/apecloud/consensus-library/issues/172
TEST_F(ThreeNodesCluster, trx_recovery_slow) {
  save_easy_log_level = easy_log_level;
  easy_log_level = EASY_LOG_INFO;
  uint64_t lastNormalIndex = node1->getLastLogIndex();
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), lastNormalIndex, 1000);
  t.replicateLogEntry(1, 3, "commitDep", kCommitDep);
  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), lastNormalIndex, 1000, 100);
  uint64_t lastLogIndex = node1->getLastLogIndex();

  auto fp =
      FailPointRegistry::getGlobalFailPointRegistry().find("ResetLogSlow");
  fp->activate(FailPoint::alwaysOn);

  t.ensureGivenNodeElected(2);
  // make sure other is not CANDIDATE but is FOLLOWER
  // otherwise, when other changes from CANDIDATE to FOLLOWER, callback function
  // will be triggered and foo will be true in an unexpected way
  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::FOLLOWER, 1000);
  EXPECT_EQ_EVENTUALLY(node3->getState(), Paxos::FOLLOWER, 1000);

  LogEntry le = t.constructTestEntry(0, kNormal);
  node2->replicateLog(le);  // should fail
  EXPECT_EQ(le.term(), 0);
  // an leader step down may happen, because the epoch task may be assinged
  // the same thread that is executing the slow recovery.
  EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(
      [&]() {
        auto leader = t.getLeader();
        if (leader == nullptr) return false;
        return leader && leader->getCommitIndex() >= (lastLogIndex + 1);
      },
      15000);

  for (int i = 0; i < 5; ++i) {
    auto leader = t.getLeader();
    EXPECT_TRUE(leader != nullptr);
    if (leader) {
      leader->replicateLog(le);  // success
      EXPECT_NE(le.term(), 0);
    }
  }
  /* recovery is slow but still send heartbeat */
  /* ensure other never statechange (requestVote) */
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), node2->getLastLogIndex(), 3000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), node3->getCommitIndex(), 3000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), node1->getCommitIndex(), 3000);
  fp->deactivate();
  easy_log_level = save_easy_log_level;
}

// leader transfer is forbidden during kCommitDep entry recovery.
TEST_F(ThreeNodesCluster, trx_recovery_slow_leader_transfer) {
  uint64_t lastNormalIndex = node1->getLastLogIndex();
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), lastNormalIndex, 2000);
  t.replicateLogEntry(1, 3, "commitDep", kCommitDep);
  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), lastNormalIndex, 1000, 100);
  uint64_t lastLogIndex = node1->getLastLogIndex();

  auto fp =
      FailPointRegistry::getGlobalFailPointRegistry().find("ResetLogSlow");
  fp->activate(FailPoint::alwaysOn);

  t.ensureGivenNodeElected(2);
  // leader transfer is disabled
  EXPECT_EQ(node2->leaderTransfer(3), PaxosErrorCode::PE_CONFLICTS);
  EXPECT_EQ(node2->getState(), Paxos::LEADER);

  // a leader may step down, so the commit index may exceed lastLogIndex + 1
  // see: https://github.com/apecloud/consensus-library/issues/172
  EXPECT_TRUE_EVENTUALLY(node2->getCommitIndex() >= (lastLogIndex + 1), 30000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), node2->getLastLogIndex(),
                       30000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), node3->getCommitIndex(), 30000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), node1->getCommitIndex(), 30000);
  fp->deactivate();
}

// test forceSingleLeader will reset kCommitDep entries
TEST_F(ThreeNodesCluster, trx_recovery_slow_forceSingleLeader) {
  uint64_t lastNormalIndex = node1->getLastLogIndex();
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), lastNormalIndex, 1000);
  t.replicateLogEntry(1, 3, "commitDep", kCommitDep);
  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), lastNormalIndex, 1000, 100);
  uint64_t lastLogIndex = node1->getLastLogIndex();

  auto fp =
      FailPointRegistry::getGlobalFailPointRegistry().find("ResetLogSlow");
  fp->activate(FailPoint::alwaysOn);
  t.ensureGivenNodeElected(2);
  node1->forceSingleLeader();
  node2->forceSingleLeader();
  node3->forceSingleLeader();
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), lastLogIndex + 1, 10000);
  fp->deactivate();

  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), node2->getCommitIndex(), 3000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), node3->getCommitIndex(), 3000);
}

TEST_F(ThreeNodesCluster, trx_recovery_slow_recv_requestVote) {
  uint64_t lastNormalIndex = node1->getLastLogIndex();
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), lastNormalIndex, 1000);
  t.replicateLogEntry(1, 3, "commitDep", kCommitDep);
  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), lastNormalIndex, 1000, 100);
  auto fp =
      FailPointRegistry::getGlobalFailPointRegistry().find("ResetLogSlow");
  fp->activate(FailPoint::alwaysOn);
  t.ensureGivenNodeElected(2);
  fp->deactivate();
  t.ensureGivenNodeElected(3);
  EXPECT_EQ_EVENTUALLY(node3->getLastLogIndex(), node3->getCommitIndex(), 1000);
  EXPECT_EQ_EVENTUALLY(node3->getLastLogIndex(), node2->getLastLogIndex(),
                       30000);
}
#endif

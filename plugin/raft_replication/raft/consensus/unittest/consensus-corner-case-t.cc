// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include "fail_point.h"
#include "paxos.h"
#include "paxos.pb.h"
#include "paxos_configuration.h"
#include "paxos_log.h"
#include "paxos_server.h"
#include "rd_paxos_log.h"
#include "ut_helper.h"

using namespace alisql;

// The leader crashes with some extra log entries which doesn't be replicated to
// any other nodes yet. The rest nodes will elect a new leader and the old
// leader join the cluster as a follower, then the extra log entries will be
// truncated. Finally the old leader downgrades to a learner.
//
// It is a real case from Alibaba production environment. Has been fixed.
TEST_F(ThreeNodesCluster,
       old_leader_with_extra_log_entries_join_as_follower_then_learner) {
  Paxos* node4 = t.createRDLogNode().initAsLearner();
  auto log4 = std::dynamic_pointer_cast<RDPaxosLog>(node4->getLog());
  EXPECT_EQ(
      node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4)),
      0);

  t.replicateLogEntry(1, 10, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       3000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node3->getLastLogIndex(),
                       3000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node4->getLastLogIndex(),
                       3000);
  /* Now, all nodes are normal */

  /*node1 write a new log and nobody receive */
  node1->set_flow_control(2, -2);
  node1->set_flow_control(3, -2);
  node1->set_flow_control(101, -2);
  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex() + 1,
                       3000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node3->getLastLogIndex() + 1,
                       3000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node4->getLastLogIndex() + 1,
                       3000);

  /* node1 crash, node2 become new leader */
  easy_warn_log("Now stop node1...");
  auto log1 = node1->getLog();
  t.destroyNode(1);
  node2->requestVote();
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 3000);

  /* equal but last one is different */
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), log1->getLastLogIndex(), 3000);

  /* change node1 to learner, change node3 to follower */
  node2->downgradeMember(1);
  node2->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(4));

  t.replicateLogEntry(2, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), node3->getLastLogIndex(),
                       3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), node4->getLastLogIndex(),
                       3000);

  /* restart node1 */
  easy_warn_log("Now restart node1...");
  node1 = t.addNodeAtIndex(1, new Paxos(2000, log1))
              .initAsMember(1, t.getNodesIpAddrVectorRef0());

  /* If bug still exists */
  //  EXPECT_EQ(node2->getLastLogIndex(), node1->getLastLogIndex() + 3);
  /* If bug fixed */
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), node1->getLastLogIndex(),
                       3000);
}

// The leader crashes with some extra log entries which doesn't be replicated to
// any other nodes yet. The rest nodes will elect a new leader and the old
// leader join the cluster as learner. However, it will be rejected since it
// contains mismatched log entries.
//
// Hasn't fixed yet
TEST_F(ThreeNodesCluster,
       new_learner_inited_from_old_leader_with_extra_entries) {
  Paxos* node4 = t.createRDLogNode().initAsLearner();
  auto log4 = std::dynamic_pointer_cast<RDPaxosLog>(node4->getLog());
  EXPECT_EQ(
      node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4)),
      0);

  t.replicateLogEntry(1, 10, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       3000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node3->getLastLogIndex(),
                       3000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node4->getLastLogIndex(),
                       3000);
  /* Now, all nodes are normal */

  /*node1 write a new log and nobody receive */
  node1->set_flow_control(2, -2);
  node1->set_flow_control(3, -2);
  node1->set_flow_control(101, -2);
  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex() + 1,
                       3000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node3->getLastLogIndex() + 1,
                       3000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node4->getLastLogIndex() + 1,
                       3000);

  /* node1 crash, node2 become new leader */
  easy_warn_log("Now stop node1...");
  auto log1 = node1->getLog();
  t.destroyNode(1);
  node2->requestVote();
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 3000);

  /* equal but last one is different */
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), log1->getLastLogIndex(), 3000);

  /* change node1 to learner, change node3 to follower */
  node2->downgradeMember(1);
  node2->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(4));

  t.replicateLogEntry(2, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), node3->getLastLogIndex(),
                       3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), node4->getLastLogIndex(),
                       3000);

  /* restart node1 */
  easy_warn_log("Now restart node1...");
  node1 = t.addNodeAtIndex(1, new Paxos(2000, log1)).initAsLearner(1);

  /* If bug still exists */
  EXPECT_EQ(node2->getLastLogIndex(), node1->getLastLogIndex() + 3);
  /* If bug fixed */
  // EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), node1->getLastLogIndex(),
  //  3000);
}

// change learner to follower, but learner do not receive this log, and then
// leader crashes, two followers and one learner will try to elect a new leader.
//
// Solved by allowing learner to vote.
TEST_F(ThreeNodesCluster, elect_with_learner) {
  Paxos* node4 = t.createRDLogNode().initAsLearner();
  EXPECT_EQ(
      node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4)),
      0);

  t.replicateLogEntry(1, 10, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       3000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node3->getLastLogIndex(),
                       3000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node4->getLastLogIndex(),
                       3000);
  /* Now, all nodes are normal */

  /* node1 change node4 to follower, but node4 do not receive this log */
  node1->set_flow_control(101, -2);
  node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(4));

  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getCommitIndex(),
                       30000);
  EXPECT_EQ(node1->getLastLogIndex(), node2->getLastLogIndex());
  EXPECT_EQ(node1->getLastLogIndex(), node3->getLastLogIndex());
  EXPECT_EQ(node1->getLastLogIndex(), node4->getLastLogIndex() + 1);

  /* node1 die */
  t.destroyNode(1);
  /* try to elect a leader */
  node2->requestVote();

  /* If bug still exists, no leader will be elected */
  // EXPECT_TRUE_CONSISTENTLY(!t.leaderExists(), 10000, 100);
  /* If bug fixed, leader exists */
  EXPECT_TRUE_EVENTUALLY(t.leaderExists(), 10000);
}

#ifdef FAIL_POINT
// This case tests the scenario that the leader succeeds to upgrade a learner to
// a follower, but the learner fails to apply the upgrade before crashing. There
// was a bug in belowing process:
// 1. Init an one-node cluster, leader is node1.
// 2. Add a learner node2 to node1.
// 3. Upgrade node2 to a follower.
// 4. Upgrading log is persisted both in node1 and node2.
// 5. node1 applies the upgrading log, there are 2 followers in node1's view.
// 6. node2 crashes before applying the upgrading log.(simulate by a failpoint)
// 7. node1 loses connection to node2, it steps down and tries to request
// votes from node2.
// 8. node2 restarts as a learner, and refuses to vote paxos0(because a learner
// doesn't have rights to vote).
// 9. node2 can't apply the upgrading log because it doesn't know the commit
// index.(have fix it here)
// 10. node1 can't become a leader because it can only get 1 vote out of 2
// followers.

// After the learner restarts, it should apply the upgrading log which has been
// persisted before crashing. See initAsLearner() in paxos.cc.
TEST_F(SingleNodeCluster, learner_upgrade_log_applied_after_restart) {
  Paxos* node2 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(2));
  EXPECT_EQ_EVENTUALLY(node1->getClusterSize(), 2, 2000);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEARNER, 2000);

  // Enable a fail point to simulate that node2 fails to apply the upgrade.
  // The server id is set to 2 because node2 is going to become a follower in
  // node1's view.
  FailPointData data(2);
  auto fp = FailPointRegistry::getGlobalFailPointRegistry().find(
      "LearnerUpgradeFail");
  ASSERT_TRUE(fp != NULL);
  fp->activate(FailPoint::alwaysOn, 0, data);

  // Try to upgrade the learner to a follower
  node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(2));

  // The leader has upgraded node2 to a follower successfully in its own view
  EXPECT_EQ_EVENTUALLY(node1->getServerNum(), 2, 2000);
  EXPECT_EQ_EVENTUALLY(node1->getLearnerNum(), 0, 2000);

  // The failpoint is activated, so node2 is still a learner
  EXPECT_EQ_CONSISTENTLY(node2->getState(), Paxos::LEARNER, 500, 100);

  // node2 crashes
  t.destroyNode(2);
  // disable the fail point
  fp->deactivate();
  // wait for node1 to detect the crash of node2
  EXPECT_TRUE_EVENTUALLY(node1->getState() != Paxos::LEADER, 10000);
  node2 = t.createRDLogNodeAtIndex(2).initAsLearner(2);

  EXPECT_TRUE_EVENTUALLY(t.leaderExists(), 10000);
  EXPECT_TRUE_EVENTUALLY(t.isLeaderOrFollower(1), 2000);
  EXPECT_TRUE_EVENTUALLY(t.isLeaderOrFollower(2), 2000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), node2->getCommitIndex(), 2000);
}
#endif

#ifdef FAIL_POINT
TEST_F(SingleNodeCluster, learner_can_vote_and_become_follower) {
  Paxos* node2 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(2));
  EXPECT_EQ_EVENTUALLY(node1->getClusterSize(), 2, 2000);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEARNER, 2000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       2000);
  auto log2 = node2->getLog();

  auto fp = FailPointRegistry::getGlobalFailPointRegistry().find(
      "LearnerUpgradeFailBeforeAppendLog");
  ASSERT_TRUE(fp != NULL);
  fp->activate(FailPoint::alwaysOn, 0);

  // Try to upgrade the learner to a follower
  node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(2));

  // The leader has upgraded node2 to a follower successfully in its own view
  EXPECT_EQ_EVENTUALLY(node1->getServerNum(), 2, 2000);
  EXPECT_EQ_EVENTUALLY(node1->getLearnerNum(), 0, 2000);

  // The failpoint is activated, so node2 is still a learner
  EXPECT_EQ_CONSISTENTLY(node2->getState(), Paxos::LEARNER, 500, 100);

  // node2 crashes
  delete node2;
  // disable the fail point
  fp->deactivate();
  // wait for node1 to detect the crash of node2
  EXPECT_TRUE_EVENTUALLY(node1->getState() != Paxos::LEADER, 10000);

  // the upgrade log is not appended to the log because of the fail point
  // the first log is node1 becomes leader
  // the second log is node2 becomes learner
  EXPECT_EQ(log2->getLastLogIndex(), 2);
  node2 = t.addNodeAtIndex(2, new Paxos(2000, log2)).initAsLearner(2);

  // only node1 can start a new election
  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::LEADER, 10000);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::FOLLOWER, 2000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), node2->getCommitIndex(), 2000);
}

#endif

#ifdef FAIL_POINT

TEST_F(ThreeNodesCluster, downgraded_follower_vote_before_downgrade) {
  EXPECT_EQ_EVENTUALLY(node2->isConnected(t.getNodeIpAddr(3)), true, 2000);

  auto baseTerm = node2->getTerm();

  auto fp = FailPointRegistry::getGlobalFailPointRegistry().find(
      "SkipUpdateCommitIndex");
  ASSERT_TRUE(fp != NULL);
  fp->activate(FailPoint::alwaysOn, 0);

  EXPECT_EQ(node1->downgradeMember(2), 0);

  // the conneciton will be re-established when the leader replies
  node2->requestVote();
  // the leader of baseTerm + 1 is node2, but after it applies the
  // memebership change log entry, it downgrades to learner immediately.
  // next election happens after electionTimeout + randome stage time
  EXPECT_EQ_EVENTUALLY(node2->getTerm(), baseTerm + 2, 4000);
  EXPECT_EQ(node2->getStats().countReplicateLog, 1);
  fp->deactivate();

  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEARNER, 3000);
  EXPECT_TRUE_EVENTUALLY(
      node2->getCurrentLeader() == 1 || node2->getCurrentLeader() == 3, 3000);
}

#endif
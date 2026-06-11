// Portions Copyright (c) 2023 ApeCloud, Inc.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "easyNet.h"
#include "fail_point.h"
#include "files.h"
#include "paxos.h"
#include "paxos.pb.h"
#include "paxos_log.h"
#include "paxos_server.h"
#include "rd_paxos_log.h"
#include "service.h"
#include "ut_helper.h"

using namespace alisql;

// In order to test the fault tolerance of the system by using the fail point
// mechanism, we need to compile the code with the macro "WITH_FAIL_POINT=ON"
// defined.(add -D WITH_FAIL_POINT=ON in cmake command)
#ifdef FAIL_POINT
// IsolateAFollowerOneWay tests the case that a follower can't receive any
// message from other peers but it can send messages to other peers. In this
// case, the follower's log index will be behind the leader's, but can't always
// be a candidate but become a follower quickly because other peers will reject
// its vote request because of the leader stickiness mechanism.
TEST_F(
    ThreeNodesCluster,
    IsolateAFollowerOneWay_follower_heartbeat_timeout_rejected_by_leader_stickness) {
  // to record node2's state change times
  int stateChangeCount = 0;
  auto cb = [&stateChangeCount](Paxos* p, Paxos::StateType state, uint64_t term,
                                uint64_t commitIndex) {
    std::cout << "State change to " << state
              << " in stateChangeCb term:" << term
              << " commitIndex:" << commitIndex << std::endl;
    EXPECT_EQ(p->getLocalServer()->strAddr, "127.0.0.1:11002");
    ++stateChangeCount;
  };
  node2->setStateChangeCb(cb);

  // isolate node2, cannot receive messages from other servers, especially the
  // leader
  FailPointData data((uint64_t)2);
  auto fp = FailPointRegistry::getGlobalFailPointRegistry().find(
      "isolateAFollowerOneWay");
  ASSERT_TRUE(fp != NULL);
  fp->activate(FailPoint::alwaysOn, 0, data);
  // Although node2 can't receive the leader's heartbeat, which will trigger
  // node2 to become candidate, leader stickness mechanism makes node2 still
  // be follower
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::FOLLOWER, 2000);
  EXPECT_EQ_EVENTUALLY(stateChangeCount, 2,
                       10000);  // Follower --> Candidate --> Follower

  t.replicateLogEntry(1, 1, "12345");
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), node3->getCommitIndex(), 2000);
  EXPECT_TRUE_EVENTUALLY(node1->getCommitIndex() > node2->getCommitIndex(),
                         2000);

  fp->deactivate();
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), node2->getCommitIndex(), 2000);
}

// IsolateAFollowerBothWay tests the case that a follower can't receive or send
// any message from/to other peers. In this case, the follower's log index will
// be behind the leader's. And the follower will become a candidate. When the
// candidate rejoin the cluster, it will not become the leader because the
// leader stickness mechanism even though the other follower is isolated.
TEST_F(
    ThreeNodesCluster,
    IsolateAFollowerBothWay_follower_heartbeat_timeout_rejected_by_leader_stickness) {
  auto originalTerm = node1->getTerm();

  // isolate node2
  FailPointData dataWith2((uint64_t)2);
  auto fp = FailPointRegistry::getGlobalFailPointRegistry().find(
      "isolateAFollowerBothWay");
  ASSERT_TRUE(fp != NULL);
  fp->activate(FailPoint::alwaysOn, 0, dataWith2);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::CANDIDATE, 10000);
  EXPECT_EQ_EVENTUALLY(node2->getTerm(), node1->getTerm() + 1, 2000);
  // node1 is still the leader
  EXPECT_EQ(node1->getState(), Paxos::LEADER);

  fp->deactivate();
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::FOLLOWER, 2000);
  // node2's term should be the same as node1's term which never changes
  EXPECT_EQ_EVENTUALLY(node2->getTerm(), originalTerm, 2000);

  fp->deactivate();
}

// TryUpdateCommitIndexFail tests that a peer fails to update its commit index
// for a while is fine.
TEST_F(ThreeNodesCluster, TryUpdateCommitIndexEarlyReturn) {
  auto baseIdx = node1->getLastLogIndex();

  auto fp = FailPointRegistry::getGlobalFailPointRegistry().find(
      "TryUpdateCommitIndexEarlyReturn");
  ASSERT_TRUE(fp != NULL && fp->name() == "TryUpdateCommitIndexEarlyReturn");
  // only fail once
  fp->activate(FailPoint::alwaysOn);
  // do not update commit index, but it's fine, later write will update commit
  // index
  t.replicateLogEntry(1, 1, "aaa");
  // update commit index
  EXPECT_EQ_CONSISTENTLY(node1->getCommitIndex(), baseIdx, 500, 100);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx, 2000);
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), baseIdx, 2000);

  fp->deactivate();
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 1, 2000);
  EXPECT_EQ_EVENTUALLY(node2->getCommitIndex(), baseIdx + 1, 2000);
  EXPECT_EQ_EVENTUALLY(node3->getCommitIndex(), baseIdx + 1, 2000);
}
#endif

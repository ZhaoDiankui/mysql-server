// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include <thread>

#include "fail_point.h"
#include "paxos.h"
#include "rd_paxos_log.h"
#include "ut_helper.h"

using namespace alisql;

TEST_F(SingleNodeCluster, AddNode_scaleout) {
  for (int i = 0; i < 3; i++) {
    Paxos* newNode = t.createRDLogNode().initAsLearner();
    node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(i + 2));
    node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(i + 2));
    EXPECT_EQ_EVENTUALLY(newNode->getState(), Paxos::FOLLOWER, 4000);
  }
}

// wrong_cluster_id checks the behavior of the Raft cluster when attempting to
// add a node with the wrong cluster ID. It verifies that the node is not added
// as a regular member and remains a learner in the cluster.
TEST_F(SingleNodeCluster, AddNode_wrong_cluster_id) {
  Paxos* node2 = t.createRDLogNode().getLastNode();
  node2->getLog()->setMetaData(Paxos::keyClusterId, 1);  // wrong cluster id
  t.initAsLearner();

  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(2));
  EXPECT_EQ_EVENTUALLY(node1->learnersToString(),
                       std::string("127.0.0.1:11002$0"), 3000);
  EXPECT_EQ_CONSISTENTLY(node2->getLog()->getLastLogIndex(), 0, 500, 100);
}

// Below 2 tests checks the behavior of the Raft cluster when adding and
// deleting a learner node repeatedly with and without node rebooting.
TEST_F(SingleNodeCluster, AddNode_add_and_del_node_repeatedly_with_reboot) {
  auto net = node1->getService()->getNet();

  // add and delete a node repeatly for multiple times
  for (int i = 0; i < 3; i++) {
    std::ignore = t.createRDLogNode().initAsLearner();
    node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(2));
    EXPECT_EQ_EVENTUALLY(node1->learnersToString(), "127.0.0.1:11002$0", 1000);
    EXPECT_EQ(net->getConnCnt(), 1);

    node1->changeLearners(Paxos::CCDelNode, t.getNodesIpAddrVectorRef1(2));
    EXPECT_EQ_EVENTUALLY(node1->learnersToString(), std::string(""), 1000);
    EXPECT_EQ(net->getConnCnt(), 0);

    t.destroyNode(2);
  }
}

TEST_F(SingleNodeCluster, AddNode_add_and_del_node_repeatedly_without_reboot) {
  auto net = node1->getService()->getNet();

  std::ignore = t.createRDLogNode().initAsLearner();
  // add and delete a node repeatly for multiple times
  for (int i = 0; i < 3; i++) {
    node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(2));
    EXPECT_EQ_EVENTUALLY(node1->learnersToString(), "127.0.0.1:11002$0", 1000);
    EXPECT_EQ(net->getConnCnt(), 1);

    node1->changeLearners(Paxos::CCDelNode, t.getNodesIpAddrVectorRef1(2));
    EXPECT_EQ_EVENTUALLY(node1->learnersToString(), std::string(""), 1000);
    EXPECT_EQ(net->getConnCnt(), 0);
  }
}

// This test verifies the error handling of the Raft cluster when attempting to
// add or delete nodes in different scenarios, such as when the node already
// exists or when it is not found. The expected error codes are checked to
// ensure the proper behavior of the cluster.
TEST_F(SingleNodeCluster, AddNode_node_exists_or_not_found) {
  // add existed member
  EXPECT_EQ(node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(1)),
            PaxosErrorCode::PE_EXISTS);
  // add not existed learner as member
  EXPECT_EQ(node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(4)),
            PaxosErrorCode::PE_NOTFOUND);
  // delete not existed member
  EXPECT_EQ(node1->changeMember(Paxos::CCDelNode, t.getNodeIpAddrRef(4)),
            PaxosErrorCode::PE_NOTFOUND);

  // add existed member as follower
  EXPECT_EQ(
      node1->changeMember(Paxos::CCAddLearnerAutoChange, t.getNodeIpAddrRef(1)),
      PaxosErrorCode::PE_EXISTS);

  // add existed member as learner
  EXPECT_EQ(
      node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(1)),
      PaxosErrorCode::PE_EXISTS);
  // add not existed node as learner
  EXPECT_EQ(
      node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4)),
      PaxosErrorCode::PE_NONE);
  // del existed member as learner
  EXPECT_EQ(
      node1->changeLearners(Paxos::CCDelNode, t.getNodesIpAddrVectorRef1(1)),
      PaxosErrorCode::PE_NOTFOUND);
  // del not existed node as learner
  EXPECT_EQ(
      node1->changeLearners(Paxos::CCDelNode, t.getNodesIpAddrVectorRef1(5)),
      PaxosErrorCode::PE_NOTFOUND);
}

// node_has_delay verifies the behavior of the Raft cluster when adding a new
// node with a delay that exceeds the configured maximum delay index. It checks
// that the addition of the new member fails and returns a PE_DELAY error code,
// indicating that the configured maximum delay index has been surpassed. This
// test ensures that the cluster handles delayed node additions correctly and
// prevents adding nodes with excessive delays.
TEST_F(SingleNodeCluster, AddNode_node_has_delay) {
  std::ignore = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(2));
  EXPECT_EQ_EVENTUALLY(node1->getClusterSize(), 2, 2000);

  t.appendLogEntryToLocalRDLog(1, 15, "aaa");

  node1->setMaxDelayIndex4NewMember(10);
  EXPECT_EQ(node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(2)),
            PaxosErrorCode::PE_DELAY);

  node1->setMaxDelayIndex4NewMember(20);
  EXPECT_EQ(node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(2)), 0);
}

#ifdef FAIL_POINT
TEST_F(ThreeNodesCluster, AddNode_pause_advance_and_apply) {
  // add 2 learners
  Paxos* node4 = t.createRDLogNode().initAsLearner();
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));
  EXPECT_EQ_EVENTUALLY(node1->getClusterSize(), 4, 2000);
  EXPECT_EQ_EVENTUALLY(node4->getState(), Paxos::LEARNER, 2000);

  FailPointData data(1);  // server id 1
  auto notApplyFp = FailPointRegistry::getGlobalFailPointRegistry().find(
      "AppointedNodeDoNotApply");
  ASSERT_TRUE(notApplyFp != NULL);
  notApplyFp->activate(FailPoint::alwaysOn, 0, data);

  // Change both of them to followers, but we do not advance the commit index
  // until both of them are appended to the log.
  auto notAdvanceFp = FailPointRegistry::getGlobalFailPointRegistry().find(
      "DoNotAdvanceCommitIndex");
  ASSERT_TRUE(notAdvanceFp != NULL);
  notAdvanceFp->activate(FailPoint::alwaysOn, 0);

  // add 2 followers consecutively, this should not be allowed...
  // these two threads will be blocked until the fail point is disabled
  std::atomic<bool> th1Ending(false);
  std::thread th1([&]() {
    int ret = node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(4));
    EXPECT_EQ(ret, 0);
    th1Ending.store(true);
  });

  EXPECT_EQ_CONSISTENTLY(th1Ending.load(), false, 500, 100);
  EXPECT_EQ(node1->getServerNum(), 3);
  EXPECT_EQ(node2->getServerNum(), 3);
  EXPECT_EQ(node3->getServerNum(), 3);
  EXPECT_EQ(node4->getServerNum(), 1);

  // disable the fail point, the conf change should be committed
  notAdvanceFp->deactivate();
  EXPECT_EQ_CONSISTENTLY(th1Ending.load(), false, 500, 100);

  // node1 do not apply the conf change, it still thinks that the cluster has 3
  // nodes.
  EXPECT_EQ(node1->getServerNum(), 3);
  EXPECT_EQ_EVENTUALLY(node2->getServerNum(), 4, 2000);
  EXPECT_EQ_EVENTUALLY(node3->getServerNum(), 4, 2000);
  EXPECT_EQ_EVENTUALLY(node4->getServerNum(), 4, 2000);

  // node1 still is the leader of the cluster
  EXPECT_EQ(node1->getState(), Paxos::LEADER);

  notApplyFp->deactivate();
  // trigger apply configure change on node1 again
  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(th1Ending.load(), true, 2000);
  th1.join();

  EXPECT_EQ_EVENTUALLY(node1->getServerNum(), 4, 2000);
}
#endif

// delete_after_add tests normal adding and deleting node operations. And make
// true the log index and meta data is as expected.
TEST_F(ThreeNodesCluster, AddNode_delete_after_add) {
  // add 2 learners
  Paxos* node4 = t.createRDLogNode().initAsLearner();  // 127.0.0.1:11004
  Paxos* node5 = t.createRDLogNode().initAsLearner();  // 127.0.0.1:11005
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef2(4, 5));
  // add another learner, which does not existed
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(6));

  EXPECT_EQ_EVENTUALLY(node1->getServerNum(), 3, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getLearnerNum(), 3, 3000);

  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       3000);
  EXPECT_EQ_EVENTUALLY(node5->getLastLogIndex(), node1->getLastLogIndex(),
                       3000);

  std::string tmpStr;
  auto rlog1 = node1->getLog();
  rlog1->getMetaData(std::string(Paxos::keyLearnerConfigure), tmpStr);
  EXPECT_EQ(
      tmpStr,
      std::string("127.0.0.1:11004$0;127.0.0.1:11005$0;127.0.0.1:11006$0"));

  // change learners to followers one by one 1
  node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(4));

  EXPECT_EQ_EVENTUALLY(node4->getState(), Paxos::FOLLOWER, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getServerNum(), 4, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getLearnerNum(), 2, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), node4->getCommitIndex(), 1000);
  tmpStr.clear();
  rlog1->getMetaData(std::string(Paxos::keyMemberConfigure), tmpStr);
  EXPECT_EQ(tmpStr, std::string("127.0.0.1:11001#5;127.0.0.1:11002#5;127.0.0."
                                "1:11003#5;127.0.0.1:11004#5@1"));
  tmpStr.clear();
  rlog1->getMetaData(std::string(Paxos::keyLearnerConfigure), tmpStr);
  EXPECT_EQ(tmpStr, std::string("0;127.0.0.1:11005$0;127.0.0.1:11006$0"));

  // restart a node, the cluster meta is still there.
  auto rlog2 = t.getNode(2)->getLog();
  t.destroyNode(2);
  node2 = t.addNodeAtIndex(2, new Paxos(2000, rlog2))
              .initAsMember(2, t.getNodesIpAddrVectorRef0());
  EXPECT_EQ_EVENTUALLY(
      node2->membersToString(("127.0.0.1:11002")),
      std::string("127.0.0.1:11001#5;127.0.0.1:11002#5;127.0.0.1:11003#5;"
                  "127.0.0.1:11004#5@2"),
      3000);

  // change learners to followers one by one 2
  node1->changeMember(Paxos::CCAddNode, t.getNodeIpAddrRef(5));
  EXPECT_EQ_EVENTUALLY(node5->getState(), Paxos::FOLLOWER, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getServerNum(), 5, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getLearnerNum(), 1, 3000);
  // check 2 new followers
  EXPECT_EQ_EVENTUALLY(node4->getLastLogIndex(), node1->getLastLogIndex(),
                       3000);
  EXPECT_EQ_EVENTUALLY(node5->getLastLogIndex(), node1->getLastLogIndex(),
                       3000);
  std::vector<Paxos::ClusterInfoType> cis;
  node1->getClusterInfo(cis);
  EXPECT_EQ(cis[3].ipPort, t.getNodeIpAddrRef(4));
  EXPECT_EQ(cis[4].ipPort, t.getNodeIpAddrRef(5));

  // delete one follower
  node1->changeMember(Paxos::CCDelNode, t.getNodeIpAddrRef(4));
  LogEntry le = t.constructTestEntry(0, kNormal);
  node1->replicateLog(le);
  EXPECT_EQ_EVENTUALLY(node1->getServerNum(), 4, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getLearnerNum(), 1, 3000);
  EXPECT_LT(node4->getLastLogIndex(), node1->getLastLogIndex());
  node1->getClusterInfo(cis);
  EXPECT_EQ(cis[3].ipPort, t.getNodeIpAddrRef(5));
}

// learner can be changed to follower even the lag exceeds
// MaxDelayIndex4NewMember at the time point of executing AddLearnerAutoChange
TEST_F(SingleNodeCluster, AddLearnerAutoChange_node_has_delay) {
  Paxos* node2 = t.createRDLogNode().initAsLearner();

  t.appendLogEntryToLocalRDLog(1, 15, "aaa");

  node1->setMaxDelayIndex4NewMember(10);
  node1->changeLearners(Paxos::CCAddLearnerAutoChange,
                        t.getNodesIpAddrVectorRef1(2));
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::FOLLOWER, 3000);
}

// learner can be initialized after AddLearnerAutoChange op
TEST_F(SingleNodeCluster, AddLearnerAutoChange_init_learner_later) {
  /* init learner after add node2 as follower */
  Paxos* node2 = t.createRDLogNode().getLastNode();

  std::thread th1([&]() {
    // this is a blocking call, changeMember will block until learner is auto
    // changed
    EXPECT_EQ(node1->changeMember(Paxos::CCAddLearnerAutoChange,
                                  t.getNodeIpAddrRef(2)),
              0);
    EXPECT_EQ(node1->getClusterSize(), 2);
  });

  std::thread th2([&]() {
    sleep(1);
    // the leader will try to establish a connection with the new learner.
    // when the new learner starts up, the RemoteServer::onConnect callback
    // will be invoked, and a AppendLog message will be sent.
    // the leader will change learner to follower when processing the
    // AppendLog response message.
    node2->initAsLearner(t.getNodeIpAddrRef(2));
  });

  th1.join();
  th2.join();
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::FOLLOWER, 3000);
}

// leader transfer happens during add follower
// changeMember returns -1
TEST_F(TwoNodesCluster, AddLearnerAutoChange_leader_transfer_in_the_middle) {
  Paxos* node3 = t.createRDLogNode().getLastNode();

  std::thread th1([&]() {
    node1->setConfigureChangeTimeout(0);
    // detect current node is not leader anymore, abort.
    // however, AddLearnerAutoChange op will be handled by the new leader
    EXPECT_EQ(node1->changeMember(Paxos::CCAddLearnerAutoChange,
                                  t.getNodeIpAddrRef(3)),
              -1);
    EXPECT_EQ(node1->getClusterSize(), 3);
  });

  std::thread th2([&]() {
    sleep(1);
    node1->leaderTransfer(2);
    EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::LEADER, 3000);
    node3->initAsLearner(t.getNodeIpAddrRef(3));
  });

  th1.join();
  th2.join();
  EXPECT_EQ_EVENTUALLY(node3->getState(), Paxos::FOLLOWER, 5000);
}

// del node happens during add follower
// changeMember returns -2
TEST_F(SingleNodeCluster, AddLearnerAutoChange_del_node_in_the_middle) {
  Paxos* node2 = t.createRDLogNode().getLastNode();

  std::thread th1([&]() {
    // detect the DelNode op executed, abort the AddLearnerAutoChange op
    EXPECT_EQ(node1->changeMember(Paxos::CCAddLearnerAutoChange,
                                  t.getNodeIpAddrRef(2)),
              -2);
    EXPECT_EQ(node1->getClusterSize(), 1); /* still 1 not 2 */
  });

  std::thread th2([&]() {
    sleep(1);
    node1->changeLearners(Paxos::CCDelNode, t.getNodesIpAddrVectorRef1(2));
    node2->initAsLearner(t.getNodeIpAddrRef(2));
  });

  th1.join();
  th2.join();
}

TEST_F(SingleNodeCluster, AddLearnerAutoChange_configure_change_timeout) {
  Paxos* node2 = t.createRDLogNode().getLastNode();

  node1->setConfigureChangeTimeout(100);
  EXPECT_EQ(
      node1->changeMember(Paxos::CCAddLearnerAutoChange, t.getNodeIpAddrRef(2)),
      PaxosErrorCode::PE_TIMEOUT);
  // rejected because only one pending change is allowed
  EXPECT_EQ(
      node1->changeMember(Paxos::CCAddLearnerAutoChange, t.getNodeIpAddrRef(2)),
      PaxosErrorCode::PE_DEFAULT);
  node2->initAsLearner(t.getNodeIpAddrRef(2));

  EXPECT_EQ(node1->getClusterSize(), 2);
  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::FOLLOWER, 3000);

  // add follower for the second time returns existed
  EXPECT_EQ(
      node1->changeMember(Paxos::CCAddLearnerAutoChange, t.getNodeIpAddrRef(2)),
      PaxosErrorCode::PE_EXISTS);
}

/*
 * reject multiple nodes added by AddLearnerAutoChange in one op
 */
TEST_F(SingleNodeCluster, AddLearnerAutoChange_add_multiple_learners_at_once) {
  // the first CCAddLearnerAutoChange op is pending
  EXPECT_EQ(node1->changeLearners(Paxos::CCAddLearnerAutoChange,
                                  t.getNodesIpAddrVectorRef2(2, 3)),
            PaxosErrorCode::PE_INVALIDARGUMENT);
  EXPECT_EQ(node1->getClusterSize(), 1);
}

/*
 * when multiple learners are added and converted to followers at the same time
 * only one learner can be added and conerted to follower
 * other learners are still learners and haven't be added into cluster
 */
TEST_F(SingleNodeCluster,
       AddLearnerAutoChange_add_multiple_learners_simultaneously) {
  /* add another follower when adding follower */
  Paxos* node2 = t.createRDLogNode().getLastNode();
  Paxos* node3 = t.createRDLogNode().getLastNode();

  std::thread th1([&]() {
    // the first CCAddLearnerAutoChange op is pending
    EXPECT_EQ(node1->changeMember(Paxos::CCAddLearnerAutoChange,
                                  t.getNodeIpAddrRef(2)),
              0);
    EXPECT_EQ(node1->getClusterSize(), 2);
  });

  std::thread th2([&]() {
    sleep(1);
    // the second CCAddLearnerAutoChange op is rejected becase only one pending
    // op is allowed
    EXPECT_EQ(node1->changeMember(Paxos::CCAddLearnerAutoChange,
                                  t.getNodeIpAddrRef(3)),
              PaxosErrorCode::PE_DEFAULT);
    node2->initAsLearner(t.getNodeIpAddrRef(2));
    node3->initAsLearner(t.getNodeIpAddrRef(3));
  });

  th1.join();
  th2.join();

  EXPECT_EQ_EVENTUALLY(node2->getState(), Paxos::FOLLOWER, 4000);
  EXPECT_EQ_CONSISTENTLY(node3->getState(), Paxos::LEARNER, 500, 100);

  /* add last failed learner to follower */
  EXPECT_EQ(
      node1->changeMember(Paxos::CCAddLearnerAutoChange, t.getNodeIpAddrRef(3)),
      0);
  EXPECT_EQ(node1->getClusterSize(), 3);
  EXPECT_EQ_EVENTUALLY(node3->getState(), Paxos::FOLLOWER, 4000);
}

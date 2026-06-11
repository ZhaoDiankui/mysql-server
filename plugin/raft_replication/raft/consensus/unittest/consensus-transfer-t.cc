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

TEST_F(CustomizedCluster, largeBatchMode) {
  Paxos* node1 =
      t.createFileLogNode().initAsMember(t.getNodesIpAddrVectorRef3(1, 2, 3));
  Paxos* node2 =
      t.createFileLogNode().initAsMember(t.getNodesIpAddrVectorRef3(1, 2, 3));
  Paxos* node3 = t.createFileLogNode().getLastNode();
  Paxos* node4 = t.createFileLogNode().getLastNode();

  EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(2)), true, 2000);
  node1->requestVote();
  EXPECT_EQ_EVENTUALLY(node1->getState(), alisql::Paxos::LEADER, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node2->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);

  // All nodes are working in in Paxos::LargeBatchMode mode,
  // the maxPacketSize is multiplied with largeBatchRatio_
  // (default is 5), so the maxPacketSize is 50,
  // which allows transfer 4 log entries in one packet. And there
  // are at least 22 log entries to send, so need more than six
  // rounds to tranfer all log entries.
  node1->setMaxPacketSize(10);
  // disable pipeline
  node1->setMaxDelayIndex(1);
  node1->setMinDelayIndex(1);
  node1->setLargeBatchRatio(5);

  t.replicateLogEntry(1, 20, "aaa");

  node3->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 3, NULL);
  node4->initAsLearner(t.getNodeIpAddrRef(4));
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));

  EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(
      [&]() {
        const Paxos::StatsType& stats = node3->getStats();
        std::cout << "countMsgAppendLog:" << stats.countMsgAppendLog
                  << " countMsgRequestVote:" << stats.countMsgRequestVote
                  << " countOnMsgAppendLog:" << stats.countOnMsgAppendLog
                  << " countHeartbeat:" << stats.countHeartbeat
                  << " countOnMsgRequestVote:" << stats.countOnMsgRequestVote
                  << " countOnHeartbeat:" << stats.countOnHeartbeat
                  << " countReplicateLog:" << stats.countReplicateLog
                  << std::endl
                  << std::flush;
        const Paxos::StatsType& stats1 = node4->getStats();
        std::cout << "countMsgAppendLog:" << stats1.countMsgAppendLog
                  << " countMsgRequestVote:" << stats1.countMsgRequestVote
                  << " countOnMsgAppendLog:" << stats1.countOnMsgAppendLog
                  << " countHeartbeat:" << stats1.countHeartbeat
                  << " countOnMsgRequestVote:" << stats1.countOnMsgRequestVote
                  << " countOnHeartbeat:" << stats1.countOnHeartbeat
                  << " countReplicateLog:" << stats1.countReplicateLog
                  << std::endl
                  << std::flush;

        // coutOnMsgAppendLog - countOnHeartbeat means the number of AppendLog
        // messages received which have payload.
        // when an empty node connects to leader, leader will try to send the
        // latest log entry to the new node, but will be rejected because the
        // new node has no previous log entries. So there will be an extra round
        // of message counted.
        return stats.countOnMsgAppendLog - stats.countOnHeartbeat >= 7 &&
               stats1.countOnMsgAppendLog - stats1.countOnHeartbeat >= 7;
      },
      3000);
}

TEST_F(CustomizedCluster, pipelining) {
  Paxos* node1 =
      t.createFileLogNode().initAsMember(t.getNodesIpAddrVectorRef3(1, 2, 3));
  Paxos* node2 =
      t.createFileLogNode().initAsMember(t.getNodesIpAddrVectorRef3(1, 2, 3));
  Paxos* node3 = t.createFileLogNode().getLastNode();
  Paxos* node4 = t.createFileLogNode().getLastNode();

  EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(2)), true, 2000);
  node1->requestVote();
  EXPECT_EQ_EVENTUALLY(node1->getState(), alisql::Paxos::LEADER, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node2->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);

  // There are least 22 log entries to send (one for leader election,
  // 20 data messages, and one for adding learner).
  //
  // The follows works in the Paxos::Normal mode, and the maxPacketSize
  // is 10, so it allows transfer only one log entries in one packet.
  // So it needs at least 22 rounds to tranfer all log entries.
  //
  // Since we doesn't enable learner pipelineing, the learner is working
  // in Paxos::LargeBatchMode. The maxPacketSize is multiplied with
  // largeBatchRatio_ (default is 5), so the maxPacketSize is 50,
  // which allows transfer 4 log entries in one packet. So it needs
  // at least six rounds to tranfer all log entries.
  node1->setMaxPacketSize(10);
  // disable pipeline
  node1->setMaxDelayIndex(1000);
  node1->setMinDelayIndex(1);
  node1->setLargeBatchRatio(5);

  node1->setConsensusAsync(true);

  t.replicateLogEntry(1, 20, "aaa");

  node3->init(t.getNodesIpAddrVectorRef3(1, 2, 3), 3, NULL);
  node4->initAsLearner(t.getNodeIpAddrRef(4));
  node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(4));

  EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(
      [&]() {
        const Paxos::StatsType& stats = node3->getStats();
        std::cout << "countMsgAppendLog:" << stats.countMsgAppendLog
                  << " countMsgRequestVote:" << stats.countMsgRequestVote
                  << " countOnMsgAppendLog:" << stats.countOnMsgAppendLog
                  << " countHeartbeat:" << stats.countHeartbeat
                  << " countOnMsgRequestVote:" << stats.countOnMsgRequestVote
                  << " countOnHeartbeat:" << stats.countOnHeartbeat
                  << " countReplicateLog:" << stats.countReplicateLog
                  << std::endl
                  << std::flush;
        const Paxos::StatsType& stats1 = node4->getStats();
        std::cout << "countMsgAppendLog:" << stats1.countMsgAppendLog
                  << " countMsgRequestVote:" << stats1.countMsgRequestVote
                  << " countOnMsgAppendLog:" << stats1.countOnMsgAppendLog
                  << " countHeartbeat:" << stats1.countHeartbeat
                  << " countOnMsgRequestVote:" << stats1.countOnMsgRequestVote
                  << " countOnHeartbeat:" << stats1.countOnHeartbeat
                  << " countReplicateLog:" << stats1.countReplicateLog
                  << std::endl
                  << std::flush;
        // coutOnMsgAppendLog - countOnHeartbeat means the number of AppendLog
        // messages received which have payload.
        // when an empty node connects to leader, leader will try to send the
        // latest log entry to the new node, but will be rejected because the
        // new node has no previous log entries. So there will be an extra round
        // of message counted.
        return stats.countOnMsgAppendLog - stats.countOnHeartbeat >= 23 &&
               stats1.countOnMsgAppendLog - stats1.countOnHeartbeat >= 7;
      },
      3000);
}

TEST_F(ThreeNodesCluster, disable_pipelining_large_entry) {
  t.replicateLogEntry(1, 10, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node3->getLastLogIndex(),
                       1000);

  /* pipelining is on */
  std::vector<Paxos::ClusterInfoType> cis;
  EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(
      [&]() {
        node1->getClusterInfo(cis);
        return cis[1].pipelining == true && cis[2].pipelining == true;
      },
      1000);

  std::string data(3ULL * 1024 * 1024, 'a');
  t.replicateLogEntry(1, 10, data);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node3->getLastLogIndex(),
                       1000);
  /* pipelining is off */
  EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(
      [&]() {
        node1->getClusterInfo(cis);
        return cis[1].pipelining == false && cis[2].pipelining == false;
      },
      1000);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node3->getLastLogIndex(),
                       1000);
  /* pipelining is on again */
  EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(
      [&]() {
        node1->getClusterInfo(cis);
        return cis[1].pipelining == true && cis[2].pipelining == true;
      },
      1000);
}

TEST_F(TwoNodesCluster, groupSend) {
  LogEntry le;
  le.set_index(0);
  le.set_optype(0);
  le.set_value("AAAAAAAAAA");

  node1->setMaxPacketSize(1);

  node1->replicateLog(le);
  auto baseIdx = node1->getLastLogIndex();
  EXPECT_TRUE_EVENTUALLY(node2->getLastLogIndex() == baseIdx, 2000);

  const Paxos::StatsType& stats = node2->getStats();
  uint64_t last_countOnMsgAppendLog = stats.countOnMsgAppendLog;
  uint64_t last_countOnHeartbeat = stats.countOnHeartbeat;
  std::cout << "countMsgAppendLog:" << stats.countMsgAppendLog
            << " countMsgRequestVote:" << stats.countMsgRequestVote
            << " countOnMsgAppendLog:" << stats.countOnMsgAppendLog
            << " countHeartbeat:" << stats.countHeartbeat
            << " countOnMsgRequestVote:" << stats.countOnMsgRequestVote
            << " countOnHeartbeat:" << stats.countOnHeartbeat
            << " countReplicateLog:" << stats.countReplicateLog << std::endl
            << std::flush;
  ::google::protobuf::RepeatedPtrField<LogEntry> entries;
  le.set_index(baseIdx + 1);
  *(entries.Add()) = le;
  le.set_index(baseIdx + 2);
  *(entries.Add()) = le;
  node1->getLog()->append(entries);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 2, 2000);
  std::cout << "countMsgAppendLog:" << stats.countMsgAppendLog
            << " countMsgRequestVote:" << stats.countMsgRequestVote
            << " countOnMsgAppendLog:" << stats.countOnMsgAppendLog
            << " countHeartbeat:" << stats.countHeartbeat
            << " countOnMsgRequestVote:" << stats.countOnMsgRequestVote
            << " countOnHeartbeat:" << stats.countOnHeartbeat
            << " countReplicateLog:" << stats.countReplicateLog << std::endl
            << std::flush;
  EXPECT_EQ(stats.countOnMsgAppendLog -
                (stats.countOnHeartbeat - last_countOnHeartbeat),
            last_countOnMsgAppendLog + 2);

  entries.Clear();
  le.set_index(baseIdx + 3);
  le.set_info(1);
  *(entries.Add()) = le;
  le.set_index(baseIdx + 4);
  le.set_info(1);
  *(entries.Add()) = le;
  node1->getLog()->append(entries);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 4, 2000);
  std::cout << "countMsgAppendLog:" << stats.countMsgAppendLog
            << " countMsgRequestVote:" << stats.countMsgRequestVote
            << " countOnMsgAppendLog:" << stats.countOnMsgAppendLog
            << " countHeartbeat:" << stats.countHeartbeat
            << " countOnMsgRequestVote:" << stats.countOnMsgRequestVote
            << " countOnHeartbeat:" << stats.countOnHeartbeat
            << " countReplicateLog:" << stats.countReplicateLog << std::endl
            << std::flush;
  EXPECT_EQ(stats.countOnMsgAppendLog -
                (stats.countOnHeartbeat - last_countOnHeartbeat),
            last_countOnMsgAppendLog + 3);

  entries.Clear();
  le.set_index(baseIdx + 5);
  le.set_info(2);
  *(entries.Add()) = le;
  le.set_index(baseIdx + 6);
  le.set_info(2);
  *(entries.Add()) = le;
  le.set_index(baseIdx + 7);
  le.set_info(1);
  *(entries.Add()) = le;
  le.set_index(baseIdx + 8);
  le.set_info(1);
  *(entries.Add()) = le;
  node1->getLog()->append(entries);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 8, 2000);
  std::cout << "countMsgAppendLog:" << stats.countMsgAppendLog
            << " countMsgRequestVote:" << stats.countMsgRequestVote
            << " countOnMsgAppendLog:" << stats.countOnMsgAppendLog
            << " countHeartbeat:" << stats.countHeartbeat
            << " countOnMsgRequestVote:" << stats.countOnMsgRequestVote
            << " countOnHeartbeat:" << stats.countOnHeartbeat
            << " countReplicateLog:" << stats.countReplicateLog << std::endl
            << std::flush;
  EXPECT_EQ(stats.countOnMsgAppendLog -
                (stats.countOnHeartbeat - last_countOnHeartbeat),
            last_countOnMsgAppendLog + 5);

  entries.Clear();
  le.set_index(baseIdx + 9);
  le.set_info(0);
  *(entries.Add()) = le;
  le.set_index(baseIdx + 10);
  le.set_info(0);
  *(entries.Add()) = le;
  node1->getLog()->append(entries);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 10, 2000);
  std::cout << "countMsgAppendLog:" << stats.countMsgAppendLog
            << " countMsgRequestVote:" << stats.countMsgRequestVote
            << " countOnMsgAppendLog:" << stats.countOnMsgAppendLog
            << " countHeartbeat:" << stats.countHeartbeat
            << " countOnMsgRequestVote:" << stats.countOnMsgRequestVote
            << " countOnHeartbeat:" << stats.countOnHeartbeat
            << " countReplicateLog:" << stats.countReplicateLog << std::endl
            << std::flush;
  EXPECT_EQ(stats.countOnMsgAppendLog -
                (stats.countOnHeartbeat - last_countOnHeartbeat),
            last_countOnMsgAppendLog + 7);
}

TEST_F(ThreeNodesCluster, flow_control) {
  auto baseIdx = node1->getLastLogIndex();

  uint64_t mps = node1->getMaxPacketSize();

  // enable flowcontrol
  // send one message at one time
  node1->setMaxPacketSize(1);
  // since flowControl is less than FlowControlMode::Normal, node1 will not
  // replicate log to node2 until force send (e.g. heartbeat).
  // but it's not guaranteed that each heartbeat can carry log entries,
  // because in the implementation, if there is any concurrency thread holds
  // the lock, appendLogFillForEach won't be called.
  node1->set_flow_control(2, Paxos::FlowControlMode::Slow);
  // since flowControl is less than FlowControlMode::Slow, node1 will stop
  // replicate log to node3
  node1->set_flow_control(3, Paxos::FlowControlMode::Slow - 1);

  t.replicateLogEntry(1, 4, "aaa");
  for (int i = 1; i <= 4; i++) {
    EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + i, 2000);
    EXPECT_EQ(node3->getLastLogIndex(), baseIdx);
  }

  // recover, disable node3's flowcontrol
  node1->setMaxPacketSize(mps);
  node1->set_flow_control(2, Paxos::FlowControlMode::Normal);
  node1->set_flow_control(3, Paxos::FlowControlMode::Normal);

  t.replicateLogEntry(1, 4, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node3->getLastLogIndex(),
                       1000);
}
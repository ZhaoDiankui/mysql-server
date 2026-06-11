// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include "paxos.h"
#include "ut_helper.h"

using namespace alisql;

TEST_F(ThreeNodesCluster, reset_match_index_of_naughty_server) {
  auto baseIdx = node1->getLastLogIndex();

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 1, 1000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 1, 1000);

  // follower 2 match index should be 2
  {
    std::vector<Paxos::ClusterInfoType> cis;
    node1->getClusterInfo(cis);
    EXPECT_EQ(cis[1].matchIndex, baseIdx + 1);
  }

  // let follower 2 increase its term, then on leader its match index will be
  // reset to 0
  easy_warn_log("now fake election");
  node2->fakeRequestVote();

  // follower 2 match index will be set to 0
  {
    EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(
        [&]() {
          std::vector<Paxos::ClusterInfoType> cis;
          node1->getClusterInfo(cis);
          return cis[1].matchIndex == 0;
        },
        3000);
  }

  node2->unfakeRequestVote();

  // follower 2 match index will be set right
  {
    EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(
        [&]() {
          std::vector<Paxos::ClusterInfoType> cis;
          node1->getClusterInfo(cis);
          return cis[1].matchIndex == baseIdx + 1;
        },
        3000);
  }

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getCommitIndex(), baseIdx + 2, 1000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), baseIdx + 2, 1000);
}

#if 0
/* temporary disable this test because it accesses protected/private class members */
TEST(misc, minMatchIndex)
{
  std::vector<std::string> strConfig;
  strConfig.emplace_back("127.0.0.1:11001");
  uint64_t timeout= 12500;
  std::shared_ptr<PaxosLog> rlog;
  rlog= std::make_shared<RDPaxosLog>("paxosLogTestDir1", true, 4 * 1024 * 1024);
  Paxos *node1= new Paxos(timeout, rlog, 3000);
  node1->init(strConfig, 1, NULL);
  sleep(1);
  node1->requestVote();

  EXPECT_EQ_EVENTUALLY(node1->getState(), Paxos::LEADER, 100, 3000);
  /* replicate a log */
  LogEntry le;
  le.set_index(0);
  le.set_optype(1);
  le.set_value("12345");
  while(node1->getLastLogIndex() < 5)
    node1->replicateLog(le);
  sleep(2);
  std::vector<Paxos::ClusterInfoType> cis;
  uint64_t ret;
  Paxos::ClusterInfoType ci;
  ci.serverId = 1;
  ci.role = Paxos::LEADER;
  ci.matchIndex = 5;
  cis.push_back(ci); // 0
  ci.serverId = 2;
  ci.role = Paxos::FOLLOWER;
  ci.matchIndex = 4;
  cis.push_back(ci); // 1
  ci.serverId = 100;
  ci.role = Paxos::LEARNER;
  ci.matchIndex = 3;
  ci.learnerSource = 0;
  cis.push_back(ci); // 2
  ci.serverId = 101;
  ci.role = Paxos::LEARNER;
  ci.matchIndex = 2;
  ci.learnerSource = 0;
  cis.push_back(ci); // 3

  cis[0].matchIndex= 0;
  ret = node1->collectMinMatchIndex(cis, false, UINT64_MAX);
  EXPECT_EQ(ret, 2);
  ret = node1->collectMinMatchIndex(cis, true, UINT64_MAX);
  EXPECT_EQ(ret, 2);
  cis[0].matchIndex= 5;

  node1->state_ = Paxos::FOLLOWER;
  node1->localServer_->serverId = 2;
  cis[1].matchIndex= 0;
  ret = node1->collectMinMatchIndex(cis, true, UINT64_MAX);
  EXPECT_EQ(ret, 5);
  cis[3].learnerSource= 2;
  ret = node1->collectMinMatchIndex(cis, true, UINT64_MAX);
  EXPECT_EQ(ret, 2);
  cis[1].matchIndex= 5;

  node1->state_ = Paxos::LEARNER;
  node1->localServer_->serverId = 100;
  ret = node1->collectMinMatchIndex(cis, true, UINT64_MAX);
  EXPECT_EQ(ret, 5);

  cis[3].learnerSource = 100;
  ret = node1->collectMinMatchIndex(cis, true, UINT64_MAX);
  EXPECT_EQ(ret, 2);

  delete node1;
  deleteDir("paxosLogTestDir1");
}
#endif

// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "file_paxos_log.h"
#include "files.h"
#include "paxos.h"
#include "paxos.pb.h"
#include "ut_helper.h"

using namespace alisql;

class FilePaxosLogTest : public FilePaxosLog {
 public:
  FilePaxosLogTest(const std::string& dataDir, LogTypeT type = LTMem)
      : FilePaxosLog(dataDir, type) {}
  void setLastCachedLogIndex(uint64_t ci) { cache_index = ci; }
  virtual uint64_t getLastCachedLogIndex() { return cache_index; }
  uint64_t cache_index = 0;
};

TEST_F(CustomizedCluster, replicate_with_cache_log) {
  Paxos* node1 = t.addNode(new Paxos(2000, std::make_shared<FilePaxosLogTest>(
                                               "paxosLogTestDir1",
                                               FilePaxosLog::LogType::LTMem)))
                     .initAsMember(t.getNodesIpAddrVectorRef3(1, 2, 3));
  Paxos* node2 = t.addNode(new Paxos(2000, std::make_shared<FilePaxosLogTest>(
                                               "paxosLogTestDir2",
                                               FilePaxosLog::LogType::LTMem)))
                     .initAsMember(t.getNodesIpAddrVectorRef3(1, 2, 3));
  Paxos* node3 = t.addNode(new Paxos(2000, std::make_shared<FilePaxosLogTest>(
                                               "paxosLogTestDir3",
                                               FilePaxosLog::LogType::LTMem)))
                     .initAsMember(t.getNodesIpAddrVectorRef3(1, 2, 3));

  EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(2)), true, 2000);
  EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(3)), true, 2000);
  node1->requestVote();
  EXPECT_EQ_EVENTUALLY(node1->getState(), alisql::Paxos::LEADER, 3000);
  EXPECT_EQ_EVENTUALLY(node1->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node2->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);
  EXPECT_EQ_EVENTUALLY(node3->getLog()->getLastLogTerm(), node1->getTerm(),
                       1000);

  uint64_t base = node1->getLog()->getLastLogIndex();

  node1->setReplicateWithCacheLog(true);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLog()->getLastLogIndex(), base + 1, 1000);
  EXPECT_TRUE_EVENTUALLY(node2->getLog()->getLastLogIndex() == base &&
                             node3->getLog()->getLastLogIndex() == base,
                         2000);

  std::dynamic_pointer_cast<FilePaxosLogTest>(node1->getLog())
      ->setLastCachedLogIndex(node1->getLog()->getLastLogIndex());
  node1->appendLog(false);
  EXPECT_TRUE_EVENTUALLY(node2->getLog()->getLastLogIndex() == base + 1 &&
                             node3->getLog()->getLastLogIndex() == base + 1,
                         2000);

  node1->setReplicateWithCacheLog(false);

  t.replicateLogEntry(1, 1, "aaa");
  EXPECT_TRUE_EVENTUALLY(node2->getLog()->getLastLogIndex() == base + 2 &&
                             node3->getLog()->getLastLogIndex() == base + 2,
                         2000);
}

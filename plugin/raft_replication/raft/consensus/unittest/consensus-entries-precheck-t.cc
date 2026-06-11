// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include "paxos.h"
#include "paxos.pb.h"
#include "paxos_log.h"
#include "rd_paxos_log.h"
#include "ut_helper.h"

using namespace alisql;

/* check server-id */
class rdsRDPaxosLog : public RDPaxosLog {
 public:
  uint32_t rds_server_id;

  rdsRDPaxosLog(const std::string& dataDir)
      : RDPaxosLog(dataDir, true, 4 * 1024 * 1024), rds_server_id(0) {}
  virtual int getEntry(uint64_t logIndex, LogEntry& entry,
                       bool fastfail = false) {
    int ret = RDPaxosLog::getEntry(logIndex, entry, fastfail);
    // add rds fields to entry
    RDSFields opaque;
    opaque.set_rdsserverid(rds_server_id);
    entry.set_opaque(opaque.SerializeAsString());
    return ret;
  }
  virtual bool entriesPreCheck(
      const ::google::protobuf::RepeatedPtrField<LogEntry>& entries) {
    if (entries.size() > 0) {
      RDSFields opaque;
      opaque.ParseFromString(entries.Get(0).opaque());
      // check rds_server_id
      if (opaque.has_rdsserverid() && opaque.rdsserverid() != rds_server_id)
        return 1;
    }
    return 0;
  }
};

/* incorrect opaque causes pre-check fail */
TEST_F(CustomizedCluster, pre_check) {
  const uint64_t timeout = 2000;
  auto log1 = std::make_shared<rdsRDPaxosLog>("paxosLogTestDir1");
  Paxos* node1 = t.addNode(new Paxos(timeout, log1))
                     .initAsMember(t.getNodesIpAddrVectorRef1(1));
  EXPECT_EQ_EVENTUALLY(node1->getState(), alisql::Paxos::FOLLOWER, 2000);
  node1->requestVote();
  EXPECT_EQ_EVENTUALLY(node1->getState(), alisql::Paxos::LEADER, 3000);

  auto log2 = std::make_shared<rdsRDPaxosLog>("paxosLogTestDir2");
  Paxos* node2 = t.addNode(new Paxos(timeout, log2)).initAsLearner();
  EXPECT_EQ(
      node1->changeLearners(Paxos::CCAddNode, t.getNodesIpAddrVectorRef1(2)),
      0);

  /* ---- main testcase: rds_server_id pre-check ---- */
  t.replicateLogEntry(1, 2, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       3000);

  log2->rds_server_id = 1;  // use a different server-id
  easy_warn_log("Set rds_server_id to 1");
  t.replicateLogEntry(1, 2, "aaa");
  EXPECT_EQ_CONSISTENTLY(node1->getLastLogIndex(), node2->getLastLogIndex() + 2,
                         500, 100);
  t.replicateLogEntry(1, 2, "aaa");
  EXPECT_EQ_CONSISTENTLY(node1->getLastLogIndex(), node2->getLastLogIndex() + 4,
                         500, 100);

  log2->rds_server_id = 0;  // reset to same server-id
  easy_warn_log("Set rds_server_id to 0");
  t.replicateLogEntry(1, 2, "aaa");
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(),
                       3000);
  /* ---- end of testcase ---- */
}

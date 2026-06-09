// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include "files.h"
#include "paxos.pb.h"
#include "rd_paxos_log.h"

using namespace alisql;

TEST(RDPaxosLog, truncate_speed) {
  std::string dir = "StressTest-paxoslog";
  RDPaxosLog* log = new RDPaxosLog(dir, true, 4 * 1024 * 1024);

  EXPECT_EQ(0, log->getLastLogIndex());
  EXPECT_EQ(1, log->getLength());
  // 40w, 76800 bytes each log
  LogEntry le;
  le.set_index(0);
  le.set_term(1);
  le.set_optype(1);
  int DATA1 = 76800;
  int DATA2 = 400000;
  int DATA3 = 100;
  char buf[DATA1 + 1];
  int i;
  for (i = 0; i < DATA1; i++) {
    buf[i] = rand() % 256;
  }
  buf[DATA1] = '\0';
  for (i = 0; i < (DATA2 + 100); ++i) {
    // shuffle
    for (int j = 0; j < DATA3; j++) {
      int a = rand() % DATA1;
      int b = rand() % DATA1;
      int tmp = buf[a];
      buf[a] = buf[b];
      buf[b] = tmp;
    }
    le.set_value((char*)buf, DATA1 + 1);
    log->appendEntry(le);
  }
  easy_warn_log("log last index %d\n", log->getLastLogIndex());
  log->truncateForward(DATA2);
  easy_warn_log("finish truncate last log index %d, first index %d\n",
                log->getLastLogIndex(), log->getFirstLogIndex());
  delete log;
  deleteDir(dir.c_str());
}

// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include "../unittest/ut_helper.h"
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

using namespace alisql;

TEST(consensus, forceFixMatchIndex) {
  std::vector<std::string> strConfig = {"127.0.0.1:11001", "127.0.0.1:11002",
                                        "127.0.0.1:11003"};

  uint64_t timeout = 10000;
  std::shared_ptr<PaxosLog> rlog;

  rlog =
      std::make_shared<RDPaxosLog>("paxosLogTestDir1", true, 4 * 1024 * 1024);
  Paxos* node1 = new Paxos(timeout, rlog);
  node1->init(strConfig, 1, NULL);

  rlog =
      std::make_shared<RDPaxosLog>("paxosLogTestDir2", true, 4 * 1024 * 1024);
  Paxos* node2 = new Paxos(timeout, rlog);
  node2->init(strConfig, 2, NULL);

  rlog =
      std::make_shared<RDPaxosLog>("paxosLogTestDir3", true, 4 * 1024 * 1024);
  Paxos* node3 = new Paxos(timeout, rlog);
  node3->init(strConfig, 3, NULL);

  sleep(1);
  node1->requestVote();
  sleep(2);

  EXPECT_EQ(node1->getState(), Paxos::LEADER);
  /* replicate a log */
  LogEntry le;
  le.set_index(0);
  le.set_optype(1);
  le.set_value("12345");
  node1->replicateLog(le);

  std::atomic<bool> shutdown(false);
  auto worker_main = [&shutdown](Paxos* p, uint64_t time) {
    while (shutdown.load() == false) {
      LogEntry le;
      le.set_index(0);
      le.set_optype(1);
      le.set_value("12345");
      if (p->getState() == Paxos::LEADER) p->replicateLog(le);
      usleep(time * 1000);
    }
  };
  std::thread writer1(worker_main, node1, 100);
  std::thread writer2(worker_main, node2, 100);
  std::thread writer3(worker_main, node3, 100);

  // node2 is not leader, so it will not update matchIndex
  node2->forceFixMatchIndex(2, node3->getLastLogIndex() - 2);

  std::thread op1([node1, node2, node3, &shutdown]() {
    sleep(1);
    while (shutdown.load() == false) {
      // node1 is leader, so it will update node2 & node3's matchIndex
      node1->forceFixMatchIndex(2, node2->getLastLogIndex() - 2);
      sleep(2);
      node1->forceFixMatchIndex(3, node3->getLastLogIndex() - 2);
      sleep(2);
    }
  });
  /* how long to test */
  sleep(10);

  std::cout << "node1 lastLogIndex " << node1->getLastLogIndex() << std::endl;
  std::cout << "node2 lastLogIndex " << node2->getLastLogIndex() << std::endl;
  std::cout << "node3 lastLogIndex " << node3->getLastLogIndex() << std::endl;

  shutdown.store(true);
  writer1.join();
  writer2.join();
  writer3.join();
  op1.join();

  node1->replicateLog(le);
  node2->replicateLog(le);
  node3->replicateLog(le);
  EXPECT_EQ_EVENTUALLY(node1->getLastLogIndex(), node2->getLastLogIndex(), 100, 3000);
  EXPECT_EQ_EVENTUALLY(node2->getLastLogIndex(), node3->getLastLogIndex(), 100, 3000);

  delete node1;
  delete node2;
  delete node3;

  deleteDir("paxosLogTestDir1");
  deleteDir("paxosLogTestDir2");
  deleteDir("paxosLogTestDir3");
}



TEST(consensus, stability) {
  std::vector<std::string> strConfig = {"127.0.0.1:11001", "127.0.0.1:11002",
                                        "127.0.0.1:11003"};
  uint64_t timeout = 12500;
  std::shared_ptr<PaxosLog> rlog;
  rlog =
      std::make_shared<RDPaxosLog>("paxosLogTestDir1", true, 4 * 1024 * 1024);
  Paxos* paxos1 = new Paxos(timeout, rlog);
  paxos1->init(strConfig, 1, NULL);
  rlog =
      std::make_shared<RDPaxosLog>("paxosLogTestDir2", true, 4 * 1024 * 1024);
  Paxos* paxos2 = new Paxos(timeout, rlog);
  paxos2->init(strConfig, 2, NULL);
  rlog =
      std::make_shared<RDPaxosLog>("paxosLogTestDir3", true, 4 * 1024 * 1024);
  Paxos* paxos3 = new Paxos(timeout, rlog);
  paxos3->init(strConfig, 3, NULL);

  sleep(1);
  paxos1->requestVote();

  EXPECT_EQ_EVENTUALLY(paxos1->getState(), Paxos::LEADER, 100, 3000);
  /* replicate a log */
  LogEntry le;
  le.set_index(0);
  le.set_optype(1);
  le.set_value("12345");
  paxos1->replicateLog(le);

  std::atomic<bool> shutdown(false);
  auto worker_main = [&shutdown](Paxos* p, uint64_t time) {
    while (shutdown.load() == false) {
      LogEntry le;
      le.set_index(0);
      le.set_optype(1);
      le.set_value("12345");
      if (p->getState() == Paxos::LEADER) p->replicateLog(le);
      usleep(time * 1000);
    }
  };
  std::thread writer1(worker_main, paxos1, 100);
  std::thread writer2(worker_main, paxos2, 100);
  std::thread writer3(worker_main, paxos3, 100);

  /* leader transfer */
  std::thread op1([paxos1, paxos2, paxos3, &shutdown]() {
    while (shutdown.load() == false) {
      sleep(2);
      paxos1->leaderTransfer(2);
      paxos2->leaderTransfer(3);
      paxos3->leaderTransfer(1);
    }
  });
  /* config change (sometimes config change will cause leaderTransfer check
   * fail) */
  std::thread op2([paxos1, paxos2, paxos3, &shutdown]() {
    std::vector<std::string> strConfig;
    strConfig.emplace_back("127.0.0.1:12000");
    while (shutdown.load() == false) {
      sleep(3);
      paxos1->changeLearners(Paxos::CCAddNode, strConfig);
      paxos2->changeLearners(Paxos::CCAddNode, strConfig);
      paxos3->changeLearners(Paxos::CCAddNode, strConfig);
      sleep(3);
      paxos1->changeLearners(Paxos::CCDelNode, strConfig);
      paxos2->changeLearners(Paxos::CCDelNode, strConfig);
      paxos3->changeLearners(Paxos::CCDelNode, strConfig);
      sleep(3);
    }
  });
  /* set cluster id (randomly kick a node out) */
  std::thread op3([paxos1, paxos2, paxos3, &shutdown]() {
    while (shutdown.load() == false) {
      sleep(1);
      switch (rand() % 3) {
        case 0:
          paxos1->setClusterId(1);
          sleep(1);
          paxos1->setClusterId(0);
          break;
        case 1:
          paxos2->setClusterId(1);
          sleep(1);
          paxos2->setClusterId(0);
          break;
        case 2:
          paxos3->setClusterId(1);
          sleep(1);
          paxos3->setClusterId(0);
          break;
      }
    }
  });
  /* how long to test */
  sleep(20);

  shutdown.store(true);
  writer1.join();
  writer2.join();
  writer3.join();
  op1.join();
  op2.join();
  op3.join();
  paxos1->replicateLog(le);
  paxos2->replicateLog(le);
  paxos3->replicateLog(le);
  EXPECT_EQ_EVENTUALLY(paxos1->getLastLogIndex(), paxos2->getLastLogIndex(), 100, 30000);
  EXPECT_EQ_EVENTUALLY(paxos2->getLastLogIndex(), paxos3->getLastLogIndex(), 100, 30000);

  delete paxos1;
  delete paxos2;
  delete paxos3;

  deleteDir("paxosLogTestDir1");
  deleteDir("paxosLogTestDir2");
  deleteDir("paxosLogTestDir3");
}

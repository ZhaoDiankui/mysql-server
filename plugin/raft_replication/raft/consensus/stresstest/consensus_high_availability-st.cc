// Copyright (c) 2023 ApeCloud, Inc.

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

const int intervalMS = 1000;
const int timeoutMS = 20000;

TEST(HighAvailability, change_members) {
  // 0. Clear the dirs
  uint64_t peers_num = 200;
  // peers_num is the number the test is going to add.
  // 2 is the Paxos group's initial peer number.
  for (uint64_t i = 1; i <= (peers_num + 2); i++) {
    std::string dir = "paxosLogTestDir" + std::to_string(i);
    deleteDir(dir.c_str());
  }

  // 1. Prepare to initailize the Paxos group with 2 peers
  // although '2' is not recommended.
  std::vector<std::string> strConfig;
  strConfig.emplace_back("127.0.0.1:11001");
  strConfig.emplace_back("127.0.0.1:11002");
  std::shared_ptr<PaxosLog> rlog;

  // 2. Initailize 2 peers.
  const uint64_t timeout = 5000;
  int current_seq = 1;
  std::string log_dir =
      std::string("paxosLogTestDir") + std::to_string(current_seq);
  rlog = std::make_shared<RDPaxosLog>(log_dir, true /*compress*/,
                                      RDPaxosLog::DEFAULT_WRITE_BUFFER_SIZE);
  Paxos* paxos1 = new Paxos(timeout, rlog);
  paxos1->init(strConfig, current_seq++);

  log_dir = std::string("paxosLogTestDir") + std::to_string(current_seq);
  rlog = std::make_shared<RDPaxosLog>(log_dir, true /*compress*/,
                                      RDPaxosLog::DEFAULT_WRITE_BUFFER_SIZE);
  Paxos* paxos2 = new Paxos(timeout, rlog);
  paxos2->init(strConfig, current_seq++);

  // 3. Let paxos1 become leader.
  paxos1->requestVote();
  EXPECT_TRUE_EVENTUALLY_WITH_CUSTOMIZED_ACTION(
      paxos1->getState() == Paxos::LEADER, intervalMS, timeoutMS, [paxos1]() {
        std::cout << "Waiting for paxos1 becoming leader..."
                  << " current state: " << paxos1->getState() << std::endl;
      });

  // Try to add some peers, including learners and followers
  enum RoleType {
    FOLLOWER,
    LEARNER,
    TYPE_END,
  };

  // 4. start another thread to keep writing data.
  std::atomic<bool> shutdown(false);
  auto writer = [&shutdown](Paxos* p, uint64_t wait_time) {
    while (shutdown.load() == false) {
      LogEntry le;
      le.set_index(0);
      le.set_optype(1);
      le.set_value("12345");
      if (p->getState() == Paxos::LEADER) p->replicateLog(le);
      usleep(wait_time * 1000);
    }
  };
  std::thread write_worker(writer, paxos1, 100);

  // 5. add peers
  int current_port = 11003;
  std::vector<Paxos*> peers_to_add(peers_num, nullptr);
  std::string addr;
  std::vector<std::string> strLearnerConfig;
  std::vector<int> followers_added, learners_added;
  for (uint64_t i = 0; i < peers_num; i++) {
    addr = "127.0.0.1:" + std::to_string(current_port++);
    auto my_rand = rand();
    RoleType type = static_cast<RoleType>(my_rand % TYPE_END);

    if (FOLLOWER == type) {
      log_dir = std::string("paxosLogTestDir") + std::to_string(current_seq++);
      rlog = std::make_shared<RDPaxosLog>(
          log_dir, true /*compress*/, RDPaxosLog::DEFAULT_WRITE_BUFFER_SIZE);
      peers_to_add[i] = new Paxos(timeout, rlog);
      // should initialize as a learner first before adding it to the paxos
      // group as a follower.
      peers_to_add[i]->initAsLearner(addr);

      EXPECT_EQ(paxos1->getState(), Paxos::LEADER);
      // 'CCAddLearnerAutoChange' means the peer should upgrade to a follower
      // once it become a learner of the paxos group.
      int ret = paxos1->changeMember(Paxos::CCAddLearnerAutoChange, addr);
      // This may fail because last add operation is not finished yet.
      EXPECT_TRUE(ret == 0);
      EXPECT_TRUE_EVENTUALLY_WITH_CUSTOMIZED_ACTION(
          peers_to_add[i]->getState() == Paxos::FOLLOWER, intervalMS, timeoutMS,
          [&]() {
            std::cout << "Waiting for peer " << i << " becoming follower..."
                      << " current state: " << peers_to_add[i]->getState()
                      << std::endl;
          });
      followers_added.emplace_back(i);
    } else {
      EXPECT_EQ(type, LEARNER);
      log_dir = std::string("paxosLogTestDir") + std::to_string(current_seq++);
      rlog = std::make_shared<RDPaxosLog>(
          log_dir, true /*compress*/, RDPaxosLog::DEFAULT_WRITE_BUFFER_SIZE);
      peers_to_add[i] = new Paxos(timeout, rlog);
      peers_to_add[i]->initAsLearner(addr);

      EXPECT_EQ(paxos1->getState(), Paxos::LEADER);
      strLearnerConfig.clear();
      strLearnerConfig.push_back(addr);
      int ret = paxos1->changeLearners(Paxos::CCAddNode, strLearnerConfig);
      // This may fail because last add operation is not finished yet.
      EXPECT_TRUE(ret == 0);
      EXPECT_TRUE_EVENTUALLY_WITH_CUSTOMIZED_ACTION(
          peers_to_add[i]->getState() == Paxos::LEARNER, intervalMS, timeoutMS,
          [&]() {
            std::cout << "Waiting for peer " << i << " becoming learner..."
                      << " current state: " << peers_to_add[i]->getState()
                      << std::endl;
          });
      learners_added.emplace_back(i);
    }
  }

  // 6. Now we should have (2 + peers_num) peers in the paxos group, check it.
  EXPECT_EQ(paxos1->getClusterSize(), 2 + peers_num);
  // pick a follower to check
  if (followers_added.size() > 0) {
    int chosen_follower = followers_added[rand() % followers_added.size()];
    EXPECT_TRUE_EVENTUALLY_WITH_CUSTOMIZED_ACTION(
        peers_to_add[chosen_follower]->getClusterSize() == (2 + peers_num),
        intervalMS, timeoutMS, [&]() {
          std::cout << "Waiting for that the follower has the same meta data "
                       "with the leader."
                    << " target size: " << (2 + peers_num) << ", my size: "
                    << peers_to_add[chosen_follower]->getClusterSize()
                    << ", the chosen follower is: " << chosen_follower
                    << ", follow size:" << followers_added.size() << std::endl;
          std::vector<Paxos::ClusterInfoType> cis;
          peers_to_add[chosen_follower]->getClusterInfo(cis);
          peers_to_add[chosen_follower]->printClusterInfo(cis);

          std::cout << "now look leader's cluster info..." << std::endl;
          cis.clear();
          paxos1->getClusterInfo(cis);
          paxos1->printClusterInfo(cis);
        });
  }

  // pick a learner to check
  if (learners_added.size() > 0) {
    int chosen_learner = learners_added[rand() % learners_added.size()];
    EXPECT_TRUE_EVENTUALLY_WITH_CUSTOMIZED_ACTION(
        peers_to_add[chosen_learner]->getClusterSize() ==
            (learners_added.size() + 1),
        intervalMS, timeoutMS, [&]() {
          std::cout << "Waiting for that the learner has the same meta data "
                       "with the leader."
                    << " target size: " << (learners_added.size() + 1)
                    << ", my size: "
                    << peers_to_add[chosen_learner]->getClusterSize()
                    << ", the chosen learner is: " << chosen_learner
                    << "learner num:" << learners_added.size() << std::endl;
          std::vector<Paxos::ClusterInfoType> cis;
          peers_to_add[chosen_learner]->getClusterInfo(cis);
          peers_to_add[chosen_learner]->printClusterInfo(cis);

          std::cout << "now look leader's cluster info..." << std::endl;
          cis.clear();
          paxos1->getClusterInfo(cis);
          paxos1->printClusterInfo(cis);
        });
  }

  // 7. Check peers in followers_added become followers
  for (auto i : followers_added) {
    EXPECT_EQ(peers_to_add[i]->getState(), Paxos::FOLLOWER);
  }

  // 8. check those left, should be learner.
  for (auto i : learners_added) {
    EXPECT_EQ(peers_to_add[i]->getState(), Paxos::LEARNER);
  }

  // 9. remove all peers except one follower to leave a classic three-peer paxos
  // group.
  int do_not_delete = true;
  Paxos* paxos3 = nullptr;
  for (auto i : followers_added) {
    EXPECT_EQ(peers_to_add[i]->getState(), Paxos::FOLLOWER);
    if (do_not_delete) {
      // only keep the first one
      do_not_delete = false;
      paxos3 = peers_to_add[i];
    } else {
      std::string to_be_deleted = peers_to_add[i]->getHost() + ":" +
                                  std::to_string(peers_to_add[i]->getPort());
      EXPECT_EQ(paxos1->changeMember(Paxos::CCDelNode, to_be_deleted), 0);
    }
  }

  strConfig.clear();
  for (auto i : learners_added) {
    EXPECT_EQ(peers_to_add[i]->getState(), Paxos::LEARNER);
    strConfig.push_back(peers_to_add[i]->getHost() + ":" +
                        std::to_string(peers_to_add[i]->getPort()));
  }
  paxos1->changeLearners(Paxos::CCDelNode, strConfig);

  // 10. Now we should have 3 peers in the paxos group, check it.
  // There is a very tiny possibility that all added peers are
  // learners, which will lead to paxos3 == nullptr.
  if (paxos3) {
    EXPECT_EQ(paxos1->getClusterSize(), 3);
    EXPECT_TRUE_EVENTUALLY_WITH_CUSTOMIZED_ACTION(
        paxos2->getClusterSize() == 3, intervalMS, timeoutMS, [&]() {
          std::cout << "paxos2 is waiting for that the followers has the same "
                       "meta data with the leader."
                    << " target size: " << 3
                    << ", my size: " << paxos2->getClusterSize() << std::endl;
        });
    EXPECT_TRUE_EVENTUALLY_WITH_CUSTOMIZED_ACTION(
        paxos3->getClusterSize() == 3, intervalMS, timeoutMS, [&]() {
          std::cout << "paxos3 is waiting for that the followers has the same "
                       "meta data with the leader."
                    << " target size: " << 3
                    << ", my size: " << paxos3->getClusterSize() << std::endl;
        });
    EXPECT_EQ(paxos1->getState(), Paxos::LEADER);
    EXPECT_EQ(paxos2->getState(), Paxos::FOLLOWER);
    EXPECT_EQ(paxos3->getState(), Paxos::FOLLOWER);
  } else {
    EXPECT_EQ(paxos1->getClusterSize(), 2);
    EXPECT_TRUE_EVENTUALLY_WITH_CUSTOMIZED_ACTION(
        paxos2->getClusterSize() == 2, intervalMS, timeoutMS, [&]() {
          std::cout << "paxos2 is waiting for that the followers has the same "
                       "meta data with the leader."
                    << " target size: " << 2
                    << ", my size: " << paxos2->getClusterSize() << std::endl;
        });
    EXPECT_EQ(paxos1->getState(), Paxos::LEADER);
    EXPECT_EQ(paxos2->getState(), Paxos::FOLLOWER);
  }

  // 11. let the writing continue...
  sleep(2);

  // 12. stop the writer
  shutdown.store(true);
  write_worker.join();

  // 13. check three peers, they should have same data.
  if (paxos3) {
    EXPECT_TRUE_EVENTUALLY_WITH_CUSTOMIZED_ACTION(
        paxos3->getClusterSize() == 3, intervalMS, timeoutMS, [&]() {
          std::cout << "paxos3 is waiting for that the followers has the same "
                       "meta data with the leader."
                    << " target size: " << 3
                    << ", my size: " << paxos3->getClusterSize() << std::endl;
        });
    EXPECT_TRUE_EVENTUALLY_WITH_CUSTOMIZED_ACTION(
        (paxos1->getCommitIndex() == paxos2->getCommitIndex() &&
         paxos1->getCommitIndex() == paxos3->getCommitIndex()),
        intervalMS, timeoutMS, [&]() {
          std::cout << "Waiting for that three peers have the same data."
                    << "paxos1 : " << paxos1->getCommitIndex()
                    << "paxos2 : " << paxos2->getCommitIndex()
                    << "paxos3 : " << paxos3->getCommitIndex() << std::endl;
        });
  }

  // 14. tear down the test env.
  delete paxos1;
  delete paxos2;
  for (auto i : followers_added) {
    delete peers_to_add[i];
  }
  for (auto i : learners_added) {
    delete peers_to_add[i];
  }

  for (int i = 1; i < current_seq; i++) {
    std::string dir = "paxosLogTestDir" + std::to_string(i);
    deleteDir(dir.c_str());
  }
}

TEST(HighAvailability, leader_transfer) {
  // 0. Clear the dirs
  int peers_num = 5;
  for (int i = 1; i <= peers_num; i++) {
    std::string dir = "paxosLogTestDir" + std::to_string(i);
    deleteDir(dir.c_str());
  }

  // 1. Create 5 peers
  std::vector<Paxos*> peers;
  std::vector<std::string> strConfig;
  peers.reserve(peers_num);
  strConfig.reserve(peers_num);
  std::string ip = "127.0.0.1";

  // generate the cluster's config
  for (int i = 1; i <= peers_num; i++) {
    std::string port = std::to_string(11000 + i);
    std::string ip_port = ip + ":" + port;
    strConfig.emplace_back(ip_port);
  }

  // create and initialize the peers
  std::shared_ptr<PaxosLog> rlog;
  const uint64_t timeout = 2000;
  int current_seq = 1;
  for (; current_seq <= peers_num; current_seq++) {
    std::string dir = "paxosLogTestDir" + std::to_string(current_seq);
    std::string log_dir =
        std::string("paxosLogTestDir") + std::to_string(current_seq);
    rlog = std::make_shared<RDPaxosLog>(log_dir, true /*compress*/,
                                        RDPaxosLog::DEFAULT_WRITE_BUFFER_SIZE);
    Paxos* paxos = new Paxos(timeout, rlog);
    peers.emplace_back(paxos);
    peers[current_seq - 1]->init(strConfig, current_seq);
  }

  sleep(3);

  // 2. Let the first peer be the leader
  peers[0]->requestVote();
  std::atomic<Paxos*> current_leader = peers[0];
  EXPECT_TRUE_EVENTUALLY(peers[0]->getState() == Paxos::LEADER, intervalMS,
                         timeoutMS);

  // 3. Start the writing thread
  std::atomic<bool> shutdown(false);
  auto writer = [&shutdown](std::atomic<Paxos*>* p, uint64_t wait_time) {
    while (shutdown.load() == false) {
      LogEntry le;
      le.set_index(0);
      le.set_optype(1);
      le.set_value("12345");
      if (p->load()->getState() == Paxos::LEADER) p->load()->replicateLog(le);
      usleep(wait_time * 1000);
    }
  };
  std::thread write_worker(writer, &current_leader, 10);

  // 4. Let the leader transfer to the other peers
  int leader_transfer_count = 20;
  int last_leader = 1;
  for (int i = 0; i < leader_transfer_count; i++) {
    int next_leader = (i + 1) % peers_num;
    // The peer sequence starts from 1.
    current_leader.load()->leaderTransfer(next_leader + 1);
    EXPECT_TRUE_EVENTUALLY(peers[next_leader]->getState() == Paxos::LEADER,
                           intervalMS, timeoutMS);
    // The peer vector starts from 0.
    current_leader.store(peers[next_leader]);
    EXPECT_TRUE(current_leader.load()->getState() == Paxos::LEADER);
    std::cout << "Leader transfer from " << last_leader << " to "
              << next_leader + 1 << std::endl;
    last_leader = next_leader + 1;
  }

  // Let the writing continue...
  sleep(2);

  // 5. stop the writer
  shutdown.store(true);
  write_worker.join();

  // 6. check five peers, they should have same data.
  for (int i = 0; i < peers_num; i++) {
    for (int j = 0; j < peers_num; j++) {
      if (i == j) continue;
      EXPECT_TRUE_EVENTUALLY_WITH_CUSTOMIZED_ACTION(
          peers[i]->getCommitIndex() == peers[j]->getCommitIndex(), intervalMS,
          timeoutMS, [&]() {
            std::cout << "Waiting for that three peers have the same data."
                      << "peers[" << i << "] : " << peers[i]->getCommitIndex()
                      << "peers[" << j << "] : " << peers[j]->getCommitIndex()
                      << std::endl;
          });
    }
  }

  // 7. tear down the test env.
  for (int i = 1; i <= peers_num; i++) {
    delete peers[i - 1];
    std::string dir = "paxosLogTestDir" + std::to_string(i);
    deleteDir(dir.c_str());
  }
}

TEST(HighAvailability, crash_recovery) {
  // 0. Clear the dirs
  int peers_num = 5;
  for (int i = 1; i <= peers_num; i++) {
    std::string dir = "paxosLogTestDir" + std::to_string(i);
    deleteDir(dir.c_str());
  }

  // 1. Create 5 peers
  std::vector<Paxos*> peers;
  std::vector<std::string> strConfig;
  peers.reserve(peers_num);
  strConfig.reserve(peers_num);
  std::string ip = "127.0.0.1";

  // generate the cluster's config
  for (int i = 1; i <= peers_num; i++) {
    std::string port = std::to_string(11000 + i);
    std::string ip_port = ip + ":" + port;
    strConfig.emplace_back(ip_port);
  }

  // create and initialize the peers
  std::shared_ptr<PaxosLog> rlog;
  const uint64_t timeout = 2000;
  int current_seq = 1;
  for (; current_seq <= peers_num; current_seq++) {
    std::string log_dir =
        std::string("paxosLogTestDir") + std::to_string(current_seq);
    rlog = std::make_shared<RDPaxosLog>(log_dir, true /*compress*/,
                                        RDPaxosLog::DEFAULT_WRITE_BUFFER_SIZE);
    Paxos* paxos = new Paxos(timeout, rlog);
    peers.emplace_back(paxos);
    peers[current_seq - 1]->init(strConfig, current_seq);
  }

  sleep(3);

  // 2. Let the first peer be the leader
  peers[0]->requestVote();
  std::atomic<Paxos*> current_leader = peers[0];
  EXPECT_TRUE_EVENTUALLY(peers[0]->getState() == Paxos::LEADER, intervalMS,
                         timeoutMS);

  // 3. Start the writing thread
  std::mutex leader_mtx;
  std::atomic<bool> shutdown(false);
  auto writer = [&shutdown](std::atomic<Paxos*>* p, uint64_t wait_time,
                            std::mutex* leader_mtx_ptr) {
    while (shutdown.load() == false) {
      {
        std::lock_guard<std::mutex> lock(*leader_mtx_ptr);
        LogEntry le;
        le.set_index(0);
        le.set_optype(1);
        le.set_value("12345");
        if ((*p != nullptr) && (p->load()->getState() == Paxos::LEADER))
          p->load()->replicateLog(le);
      }
      // usleep(wait_time * 1000);
    }
  };
  std::thread write_worker(writer, &current_leader, 10, &leader_mtx);

  // 4. Let the leader and a follower crash, the majority should still be able
  // to elect a new leader.
  int crash_times = 20;
  int curr_leader = 1;
  for (int i = 0; i < crash_times; i++) {
    // choose a follower to crash
    int crash_peer = (i + 1) % peers_num;
    if ((crash_peer + 1) == curr_leader) {
      crash_peer = (crash_peer + 1) % peers_num;
    }
    delete peers[crash_peer];
    peers[crash_peer] = nullptr;

    int old_leader = curr_leader;
    // Let the leader crash
    {
      std::lock_guard<std::mutex> lock(leader_mtx);
      delete current_leader.load();
      current_leader = nullptr;
    }

    // find the newly elected leader
    while (!current_leader) {
      for (int j = 0; j < peers_num; j++) {
        // these two peers are crashed
        if (j == crash_peer || (j + 1) == curr_leader) continue;
        if (peers[j]->getState() == Paxos::LEADER) {
          current_leader = peers[j];
          curr_leader = j + 1;
          break;
        }
        sleep(1);
      }
    }

    EXPECT_TRUE(current_leader.load()->getState() == Paxos::LEADER);
    std::cout << "The old leader " << old_leader << " and a follower "
              << crash_peer + 1 << " crashed. "
              << "The new leader is " << curr_leader << std::endl;

    // restart the crashed peer
    std::string log_dir =
        std::string("paxosLogTestDir") + std::to_string(old_leader);
    rlog = std::make_shared<RDPaxosLog>(log_dir, true /*compress*/,
                                        RDPaxosLog::DEFAULT_WRITE_BUFFER_SIZE);
    peers[old_leader - 1] = new Paxos(timeout, rlog);
    peers[old_leader - 1]->init(strConfig, old_leader);

    log_dir = std::string("paxosLogTestDir") + std::to_string(crash_peer + 1);
    rlog = std::make_shared<RDPaxosLog>(log_dir, true /*compress*/,
                                        RDPaxosLog::DEFAULT_WRITE_BUFFER_SIZE);
    peers[crash_peer] = new Paxos(timeout, rlog);
    peers[crash_peer]->init(strConfig, crash_peer + 1);

    EXPECT_TRUE_EVENTUALLY(peers[old_leader - 1]->getState() == Paxos::FOLLOWER,
                           intervalMS, timeoutMS);
    EXPECT_TRUE_EVENTUALLY(peers[crash_peer]->getState() == Paxos::FOLLOWER,
                           intervalMS, timeoutMS);
  }

  // Let the writing continue...
  sleep(2);

  // 5. stop the writer
  shutdown.store(true);
  write_worker.join();

  // 6. check five peers, they should have same data.
  for (int i = 0; i < peers_num; i++) {
    for (int j = 0; j < peers_num; j++) {
      if (i == j) continue;
      EXPECT_TRUE_EVENTUALLY(
          peers[i]->getCommitIndex() == peers[j]->getCommitIndex(), intervalMS,
          timeoutMS);
      EXPECT_TRUE_EVENTUALLY_WITH_CUSTOMIZED_ACTION(
          peers[i]->getCommitIndex() == peers[j]->getCommitIndex(), intervalMS,
          timeoutMS, [&]() {
            std::cout << "Waiting for that three peers have the same data."
                      << "peers[" << i << "] : " << peers[i]->getCommitIndex()
                      << "peers[" << j << "] : " << peers[j]->getCommitIndex()
                      << std::endl;
          });
    }
  }

  // 7. tear down the test env.
  for (int i = 1; i <= peers_num; i++) {
    delete peers[i - 1];
    std::string dir = "paxosLogTestDir" + std::to_string(i);
    deleteDir(dir.c_str());
  }
}

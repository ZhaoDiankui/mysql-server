// Copyright (c) 2023 ApeCloud, Inc.

#pragma once
#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "easyNet.h"
#include "file_paxos_log.h"
#include "files.h"
#include "paxos.h"
#include "rd_paxos_log.h"

// Default interval (in ms) for checking condition in EXPECT_TRUE_EVENTUALLY and
// EXPECT_EQ_EVENTUALLY
const int DefaultInterval = 10;

// Checks if condition(cond) becomes true at some point within the timeout
// (timeout in ms), checking every interval (interval in ms). It would be useful
// for cases where you expect the condition to be false initially, but
// eventually become true as a result of some event or change in the system.
#define EXPECT_TRUE_EVENTUALLY(cond, timeout) \
  EXPECT_TRUE_EVENTUALLY_WITH_INTERVAL(cond, DefaultInterval, timeout)

#define EXPECT_TRUE_EVENTUALLY_WITH_INTERVAL(cond, interval, timeout)   \
  {                                                                     \
    auto start = std::chrono::system_clock::now();                      \
    while (!(cond)) {                                                   \
      std::this_thread::sleep_for(std::chrono::milliseconds(interval)); \
      if (std::chrono::duration_cast<std::chrono::milliseconds>(        \
              std::chrono::system_clock::now() - start)                 \
              .count() > timeout) {                                     \
        EXPECT_TRUE(cond);                                              \
        break;                                                          \
      }                                                                 \
    }                                                                   \
  }

// Checks if leftvalue equals to rightvalue at some point within the timeout
// (timeout in ms), checking every interval (interval in ms). It would be useful
// for cases where you expect the condition to be false initially, but
// eventually become true as a result of some event or change in the system.
#define EXPECT_EQ_EVENTUALLY(leftvalue, rightvalue, timeout)                 \
  EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(leftvalue, rightvalue, DefaultInterval, \
                                     timeout)

#define EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(leftvalue, rightvalue, interval, \
                                           timeout)                         \
  {                                                                         \
    auto start = std::chrono::system_clock::now();                          \
    while ((leftvalue) != (rightvalue)) {                                   \
      std::this_thread::sleep_for(std::chrono::milliseconds(interval));     \
      if (std::chrono::duration_cast<std::chrono::milliseconds>(            \
              std::chrono::system_clock::now() - start)                     \
              .count() > timeout) {                                         \
        EXPECT_EQ(leftvalue, rightvalue);                                   \
        break;                                                              \
      }                                                                     \
    }                                                                       \
  }

// Expect the return value of lambda to be true eventually, after timeout (in
// ms), checked every interval (in ms).
#define EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(lambda, timeout)                  \
  {                                                                            \
    auto start = std::chrono::system_clock::now();                             \
    while (!(lambda())) {                                                      \
      std::this_thread::sleep_for(std::chrono::milliseconds(DefaultInterval)); \
      if (std::chrono::duration_cast<std::chrono::milliseconds>(               \
              std::chrono::system_clock::now() - start)                        \
              .count() > timeout) {                                            \
        EXPECT_TRUE(lambda());                                                 \
        break;                                                                 \
      }                                                                        \
    }                                                                          \
  }

#define EXPECT_LOG_GET_METADATA_EQ(node, key, expectedValue, timeout) \
  {                                                                   \
    std::string str;                                                  \
    EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(                             \
        [&]() {                                                       \
          node->getLog()->getMetaData(key, str);                      \
          return str == expectedValue;                                \
        },                                                            \
        timeout);                                                     \
    EXPECT_EQ(str, expectedValue);                                    \
  }

#define EXPECT_TRUE_EVENTUALLY_WITH_CUSTOMIZED_ACTION(cond, timeout, lambda)   \
  {                                                                            \
    auto start = std::chrono::system_clock::now();                             \
    while (!(cond)) {                                                          \
      std::this_thread::sleep_for(std::chrono::milliseconds(DefaultInterval)); \
      lambda();                                                                \
      if (std::chrono::duration_cast<std::chrono::milliseconds>(               \
              std::chrono::system_clock::now() - start)                        \
              .count() > timeout) {                                            \
        EXPECT_TRUE(cond);                                                     \
        break;                                                                 \
      }                                                                        \
    }                                                                          \
  }

// Check the condition (cond) remains true for some time (duration in ms),
// checking every interval (interval in ms). It would be useful for cases
// where you expect the condition to be true initially and remain true,
// regardless of any events or changes in the system.
#define EXPECT_TRUE_CONSISTENTLY(cond, duration, interval)              \
  {                                                                     \
    auto start = std::chrono::system_clock::now();                      \
    while (std::chrono::duration_cast<std::chrono::milliseconds>(       \
               std::chrono::system_clock::now() - start)                \
               .count() < duration) {                                   \
      EXPECT_TRUE(cond);                                                \
      std::this_thread::sleep_for(std::chrono::milliseconds(interval)); \
    }                                                                   \
  }

#define EXPECT_EQ_CONSISTENTLY(leftvalue, rightvalue, duration, interval) \
  {                                                                       \
    auto start = std::chrono::system_clock::now();                        \
    while (std::chrono::duration_cast<std::chrono::milliseconds>(         \
               std::chrono::system_clock::now() - start)                  \
               .count() < duration) {                                     \
      EXPECT_EQ(leftvalue, rightvalue);                                   \
      std::this_thread::sleep_for(std::chrono::milliseconds(interval));   \
    }                                                                     \
  }

inline void msleep(uint64_t milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

class PaxosTestbed {
 public:
  PaxosTestbed() : enableMemoryUsageCount_(false), heartbeatThreadCnt_(0) {
    cleanEnvironment();
    nodes_.push_back(NULL);
  }
  ~PaxosTestbed() {
    for (auto& node : nodes_) {
      delete node;
    }
    cleanEnvironment();
  }

  void setEnableMemoryUsageCount(bool flag) { enableMemoryUsageCount_ = flag; }

  void setHeartbeatThreadCnt(int cnt) { heartbeatThreadCnt_ = cnt; }

  std::vector<std::string> getNodesIpAddrVectorRef() {
    std::vector<std::string> vec;
    return vec;
  }

  std::vector<std::string> getNodesIpAddrVector1(int nodeIdx,
                                                 int basePort = 11000) {
    std::vector<std::string> vec;
    vec.emplace_back(getNodeIpAddr(nodeIdx));
    return vec;
  }

  std::vector<std::string> getNodesIpAddrVector2(int nodeIdx1, int nodeIdx2,
                                                 int basePort = 11000) {
    std::vector<std::string> vec;
    vec.emplace_back(getNodeIpAddr(nodeIdx1));
    vec.emplace_back(getNodeIpAddr(nodeIdx2));
    return vec;
  }

  std::vector<std::string> getNodesIpAddrVector3(int nodeIdx1, int nodeIdx2,
                                                 int nodeIdx3,
                                                 int basePort = 11000) {
    std::vector<std::string> vec;
    vec.emplace_back(getNodeIpAddr(nodeIdx1));
    vec.emplace_back(getNodeIpAddr(nodeIdx2));
    vec.emplace_back(getNodeIpAddr(nodeIdx3));
    return vec;
  }

  std::vector<std::string> getNodesIpAddrVector4(int nodeIdx1, int nodeIdx2,
                                                 int nodeIdx3, int nodeIdx4,
                                                 int basePort = 11000) {
    std::vector<std::string> vec;
    vec.emplace_back(getNodeIpAddr(nodeIdx1));
    vec.emplace_back(getNodeIpAddr(nodeIdx2));
    vec.emplace_back(getNodeIpAddr(nodeIdx3));
    vec.emplace_back(getNodeIpAddr(nodeIdx4));
    return vec;
  }

  std::vector<std::string> getNodesIpAddrVector5(int nodeIdx1, int nodeIdx2,
                                                 int nodeIdx3, int nodeIdx4,
                                                 int nodeIdx5,
                                                 int basePort = 11000) {
    std::vector<std::string> vec;
    vec.emplace_back(getNodeIpAddr(nodeIdx1));
    vec.emplace_back(getNodeIpAddr(nodeIdx2));
    vec.emplace_back(getNodeIpAddr(nodeIdx3));
    vec.emplace_back(getNodeIpAddr(nodeIdx4));
    vec.emplace_back(getNodeIpAddr(nodeIdx5));
    return vec;
  }

  std::vector<std::string>& getNodesIpAddrVectorRef0() {
    tmpStrVec_.clear();
    return tmpStrVec_;
  }

  std::vector<std::string>& getNodesIpAddrVectorRef1(int nodeIdx,
                                                     int basePort = 11000) {
    tmpStrVec_.clear();
    tmpStrVec_.emplace_back(getNodeIpAddr(nodeIdx));
    return tmpStrVec_;
  }

  std::vector<std::string>& getNodesIpAddrVectorRef2(int nodeIdx1, int nodeIdx2,
                                                     int basePort = 11000) {
    tmpStrVec_.clear();
    tmpStrVec_.emplace_back(getNodeIpAddr(nodeIdx1));
    tmpStrVec_.emplace_back(getNodeIpAddr(nodeIdx2));
    return tmpStrVec_;
  }

  std::vector<std::string>& getNodesIpAddrVectorRef3(int nodeIdx1, int nodeIdx2,
                                                     int nodeIdx3,
                                                     int basePort = 11000) {
    tmpStrVec_.clear();
    tmpStrVec_.emplace_back(getNodeIpAddr(nodeIdx1));
    tmpStrVec_.emplace_back(getNodeIpAddr(nodeIdx2));
    tmpStrVec_.emplace_back(getNodeIpAddr(nodeIdx3));
    return tmpStrVec_;
  }

  std::vector<std::string>& getNodesIpAddrVectorRef4(int nodeIdx1, int nodeIdx2,
                                                     int nodeIdx3, int nodeIdx4,
                                                     int basePort = 11000) {
    tmpStrVec_.clear();
    tmpStrVec_.emplace_back(getNodeIpAddr(nodeIdx1));
    tmpStrVec_.emplace_back(getNodeIpAddr(nodeIdx2));
    tmpStrVec_.emplace_back(getNodeIpAddr(nodeIdx3));
    tmpStrVec_.emplace_back(getNodeIpAddr(nodeIdx4));
    return tmpStrVec_;
  }

  std::vector<std::string>& getNodesIpAddrVectorRef5(int nodeIdx1, int nodeIdx2,
                                                     int nodeIdx3, int nodeIdx4,
                                                     int nodeIdx5,
                                                     int basePort = 11000) {
    tmpStrVec_.clear();
    tmpStrVec_.emplace_back(getNodeIpAddr(nodeIdx1));
    tmpStrVec_.emplace_back(getNodeIpAddr(nodeIdx2));
    tmpStrVec_.emplace_back(getNodeIpAddr(nodeIdx3));
    tmpStrVec_.emplace_back(getNodeIpAddr(nodeIdx4));
    tmpStrVec_.emplace_back(getNodeIpAddr(nodeIdx5));
    return tmpStrVec_;
  }

  std::vector<std::string>& getNodesIpAddrVectorRefRange(int nodeIdxStart,
                                                         int nodeIdxEnd,
                                                         int basePort = 11000) {
    tmpStrVec_.clear();
    for (int i = nodeIdxStart; i <= nodeIdxEnd; i++) {
      tmpStrVec_.emplace_back(getNodeIpAddr(i, basePort));
    }
    return tmpStrVec_;
  }

  std::string& getEmptyNodeIpAddrRef() {
    tmpStr_ = "";
    return tmpStr_;
  }

  std::string& getNodeIpAddrRef(int nodeIdx, int basePort = 11000) {
    tmpStr_ = getNodeIpAddr(nodeIdx, basePort);
    return tmpStr_;
  }

  std::string getNodeIpAddr(int nodeIdx, int basePort = 11000) {
    return "127.0.0.1:" + std::to_string(basePort + nodeIdx);
  }

  void initNodes(
      int num,
      std::function<void(const std::vector<std::string>&, int)>&& initNode,
      int basePort = 11000, alisql::ClientService* cs = NULL) {
    std::vector<std::string> strConfig;
    for (int i = 0; i < num; i++) {
      strConfig.emplace_back(getNodeIpAddr(i + 1, basePort));
    }
    for (int i = 0; i < num; i++) {
      initNode(strConfig, i + 1);
      initAsMember(i + 1, strConfig, cs);
    }
  }

  void initRDLogNodes(int num, int basePort = 11000,
                      alisql::ClientService* cs = NULL) {
    initNodes(num,
              std::forward<
                  std::function<void(const std::vector<std::string>&, int)>&&>(
                  [&](const std::vector<std::string>& strConfig,
                      int index) {  // index starts from 1
                    createRDLogNode();
                  }),
              basePort, cs);
  }

  void initFileLogNodes(int num, int basePort = 11000,
                        alisql::ClientService* cs = NULL) {
    initNodes(num,
              std::forward<
                  std::function<void(const std::vector<std::string>&, int)>&&>(
                  [&](const std::vector<std::string>& strConfig,
                      int index) {  // index starts from 1
                    createFileLogNode();
                  }),
              basePort, cs);
  }

  PaxosTestbed& createRDLogNode(bool compress = true,
                                size_t writeBufferSize = 4 * 1024 * 1024,
                                bool sync = true,
                                uint64_t electionTimeout = 2000,
                                uint64_t purgeLogTimeout = 3000) {
    nodes_.push_back(NULL);
    return createRDLogNodeAtIndex(nodes_.size() - 1);
  }

  PaxosTestbed& createRDLogNodeAtIndex(int nodeIdx, bool compress = true,
                                       size_t writeBufferSize = 4 * 1024 * 1024,
                                       bool sync = true,
                                       uint64_t electionTimeout = 2000,
                                       uint64_t purgeLogTimeout = 3000) {
    assert(nodeIdx >= 1 && (size_t)nodeIdx < nodes_.size());

    std::string dir = kPaxosLogTestDirPrefix + std::to_string(nodeIdx);
    std::shared_ptr<alisql::PaxosLog> rlog =
        std::make_shared<alisql::RDPaxosLog>(dir, compress, writeBufferSize,
                                             sync);
    alisql::Paxos* paxos =
        new alisql::Paxos(electionTimeout, rlog, purgeLogTimeout);
    return addNodeAtIndex(nodeIdx, paxos);
  }

  PaxosTestbed& createFileLogNode(uint64_t electionTimeout = 2000,
                                  uint64_t purgeLogTimeout = 3000) {
    nodes_.push_back(NULL);
    return createFileLogNodeAtIndex(nodes_.size() - 1);
  }

  PaxosTestbed& createFileLogNodeAtIndex(int nodeIdx,
                                         uint64_t electionTimeout = 2000,
                                         uint64_t purgeLogTimeout = 3000) {
    assert(nodeIdx >= 1 && (size_t)nodeIdx < nodes_.size());

    std::string dir = kPaxosLogTestDirPrefix + std::to_string(nodeIdx);
    std::shared_ptr<alisql::PaxosLog> rlog =
        std::make_shared<alisql::FilePaxosLog>(dir);
    alisql::Paxos* paxos =
        new alisql::Paxos(electionTimeout, rlog, purgeLogTimeout);
    return addNodeAtIndex(nodeIdx, paxos);
  }

  PaxosTestbed& addNode(alisql::Paxos* node) {
    nodes_.push_back(node);
    return *this;
  }

  PaxosTestbed& addNodeAtIndex(int nodeIdx, alisql::Paxos* node) {
    assert(nodeIdx >= 1 && (size_t)nodeIdx < nodes_.size());

    nodes_[nodeIdx] = node;
    return *this;
  }

  alisql::Paxos* initAsMember(const std::vector<std::string>& strConfig,
                              alisql::ClientService* cs = NULL) {
    return initAsMember(nodes_.size() - 1, strConfig, cs);
  }

  alisql::Paxos* initAsMember(int nodeIdx,
                              const std::vector<std::string>& strConfig,
                              alisql::ClientService* cs = NULL) {
    alisql::Paxos* node = getNode(nodeIdx);
    node->init(strConfig, nodeIdx, cs, 4, 4, nullptr, enableMemoryUsageCount_,
               heartbeatThreadCnt_);
    return node;
  }

  alisql::Paxos* initAsLearner() { return initAsLearner(nodes_.size() - 1); }

  alisql::Paxos* initAsLearner(int nodeIdx) {
    alisql::Paxos* learner = getNode(nodeIdx);
    learner->initAsLearner(getNodeIpAddrRef(nodeIdx), NULL, 4, 4, nullptr,
                           enableMemoryUsageCount_, heartbeatThreadCnt_);
    return learner;
  }

  alisql::Paxos* getLastNode() { return getNode(nodes_.size() - 1); }

  // nodeIdx starts from 1
  alisql::Paxos* getNode(int nodeIdx) {
    assert(nodeIdx >= 1 && (size_t)nodeIdx < nodes_.size());

    return nodes_[nodeIdx];
  }

  void ensureFirstNodeElected() { ensureGivenNodeElected(1); }

  // nodeIdx starts from 1
  void ensureGivenNodeElected(int nodeIdx) {
    assert(nodeIdx >= 1 && (size_t)nodeIdx < nodes_.size());

    int leaderIdx = getLeaderIndex();
    EXPECT_NE(leaderIdx, -1);
    if (leaderIdx == nodeIdx) {
      return;
    }
    EXPECT_EQ_EVENTUALLY(nodes_[nodeIdx]->getLastLogIndex(),
                         nodes_[leaderIdx]->getLastLogIndex(), 2000);
    nodes_[nodeIdx]->requestVote();
    EXPECT_LAMBDA_RETURN_TRUE_EVENTUALLY(
        [&]() { return nodes_[nodeIdx]->getState() == alisql::Paxos::LEADER; },
        electionCompleteTimeoutMS);
  }

  int getLeaderIndex() {
    for (size_t i = 1; i < nodes_.size(); i++) {
      if (nodes_[i] && nodes_[i]->getState() == alisql::Paxos::LEADER) {
        return i;
      }
    }
    return -1;
  }

  alisql::Paxos* getLeader() {
    int leaderIdx = getLeaderIndex();
    if (leaderIdx > 0) {
      return nodes_[leaderIdx];
    }
    return NULL;
  }

  bool leaderExists() { return getLeaderIndex() > 0; }

  bool isLeaderOrFollower(int nodeIdx) {
    assert(nodeIdx >= 1 && (size_t)nodeIdx < nodes_.size());

    alisql::Paxos* node = getNode(nodeIdx);
    assert(node);
    return node->getState() == alisql::Paxos::LEADER ||
           node->getState() == alisql::Paxos::FOLLOWER;
  }

  // nodeIdx starts from 1
  void destroyNode(int nodeIdx) {
    assert(nodeIdx >= 1 && (size_t)nodeIdx < nodes_.size());

    delete nodes_[nodeIdx];
    nodes_[nodeIdx] = NULL;
  }

  void replicateLogEntry(int nodeIdx, int count, const std::string& value,
                         alisql::LogOperation logOperation = alisql::kNormal) {
    assert(nodeIdx >= 1 && (size_t)nodeIdx < nodes_.size());

    alisql::LogEntry le;
    le.set_index(0);
    le.set_optype(logOperation);
    le.set_value(value);
    for (int i = 0; i < count; ++i) {
      nodes_[nodeIdx]->replicateLog(le);
    }
  }

  void appendLogEntryToLocalRDLog(int nodeIdx, int count,
                                  const std::string& value) {
    appendLogEntryToLocalRDLog(nodeIdx, -1, count, value);
  }

  void appendLogEntryToLocalRDLog(int nodeIdx, int term, int count,
                                  const std::string& value) {
    appendLogEntryToLocalRDLog(nodeIdx, term, alisql::kNormal, count, value);
  }

  void appendLogEntryToLocalRDLog(int nodeIdx, int term,
                                  alisql::LogOperation opType, int count,
                                  const std::string& value) {
    assert(nodeIdx >= 1 && (size_t)nodeIdx < nodes_.size());

    alisql::Paxos* node = nodes_[nodeIdx];

    alisql::LogEntry le;
    le.set_term(term < 0 ? node->getTerm() : term);
    le.set_optype(opType);
    le.set_value(value);
    for (int i = 0; i < count; ++i) {
      le.set_index(node->getLastLogIndex() + 1);
      std::dynamic_pointer_cast<alisql::RDPaxosLog>(node->getLog())
          ->appendEntry(le);
    }
  }

  alisql::LogEntry constructTestEntry(uint64_t term, alisql::LogOperation type,
                                      uint64_t index = 0,
                                      std::string key = "tKey",
                                      std::string value = "tValue") {
    alisql::LogEntry ret;
    ret.set_term(term);
    ret.set_index(index);
    ret.set_optype(type);
    ret.set_ikey(key);
    ret.set_value(value);
    return ret;
  }

  uint64_t getNodeLogEntryTerm(int nodeIdx, int index) {
    assert(nodeIdx >= 1 && (size_t)nodeIdx < nodes_.size());

    alisql::LogEntry le;
    assert(nodes_[nodeIdx]->getLog()->getEntry(index, le) == 0);
    return le.term();
  }

 private:
  const std::string kPaxosLogTestDirPrefix = "paxosLogTestDir";

  void cleanEnvironment() {
    std::vector<std::string> dirs;
    listDir(".", dirs);
    for (auto& dir : dirs) {
      // if dir has the prefix of kPaxosLogTestDirPrefix, delete it
      if (dir.find(kPaxosLogTestDirPrefix) == 0) {
        alisql::deleteDir(dir.c_str());
      }
    }
  }

  void listDir(std::string dir, std::vector<std::string>& dirs) {
    DIR* dp;
    struct dirent* dirp;
    if ((dp = opendir(dir.c_str())) == NULL) {
      std::cout << "Error(" << errno << ") opening " << dir << std::endl;
      return;
    }

    while ((dirp = readdir(dp)) != NULL) {
      dirs.push_back(std::string(dirp->d_name));
    }
    closedir(dp);
  }

  const int electionCompleteTimeoutMS = 10000;

  std::vector<alisql::Paxos*> nodes_;

  std::string tmpStr_;
  std::vector<std::string> tmpStrVec_;

  bool enableMemoryUsageCount_;
  bool heartbeatThreadCnt_;
};

class CustomizedCluster : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}

  PaxosTestbed t;
};

class SingleNodeCluster : public ::testing::Test {
 protected:
  void SetUp() override {
    t.initRDLogNodes(1);
    node1 = t.getNode(1);

    /* only has one node in cluster */
    EXPECT_EQ_EVENTUALLY(node1->getState(), alisql::Paxos::FOLLOWER, 2000);

    /* trigger election manually */
    node1->requestVote();
    EXPECT_EQ_EVENTUALLY(node1->getState(), alisql::Paxos::LEADER, 3000);

    EXPECT_EQ_EVENTUALLY(node1->getLog()->getLastLogTerm(), node1->getTerm(),
                         1000);
  }

  void TearDown() override {}

  PaxosTestbed t;
  alisql::Paxos* node1;
};

class TwoNodesCluster : public ::testing::Test {
 protected:
  void SetUp() override {
    node1 =
        t.createRDLogNode().initAsMember(1, t.getNodesIpAddrVectorRef2(1, 2));
    node2 =
        t.createRDLogNode().initAsMember(2, t.getNodesIpAddrVectorRef2(1, 2));

    EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(2)), true, 2000);

    node1->requestVote();
    EXPECT_EQ_EVENTUALLY(node1->getState(), alisql::Paxos::LEADER, 3000);

    EXPECT_EQ_EVENTUALLY(node1->getLog()->getLastLogTerm(), node1->getTerm(),
                         1000);
    EXPECT_EQ_EVENTUALLY(node2->getLog()->getLastLogTerm(), node1->getTerm(),
                         1000);
  }

  void TearDown() override {}

  PaxosTestbed t;
  alisql::Paxos* node1;
  alisql::Paxos* node2;
};

class CustomizedThreeNodesCluster : public ::testing::Test {
 protected:
  void SetUp() override {}

  void TearDown() override {}

  void createNodes123() {
    node1 = t.createRDLogNode().getLastNode();
    node2 = t.createRDLogNode().getLastNode();
    node3 = t.createRDLogNode().getLastNode();
  }

  void initNodes123() {
    t.initAsMember(1, t.getNodesIpAddrVectorRef3(1, 2, 3));
    t.initAsMember(2, t.getNodesIpAddrVectorRef3(1, 2, 3));
    t.initAsMember(3, t.getNodesIpAddrVectorRef3(1, 2, 3));
  }

  void ensureNode1Elected() {
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
  }

  PaxosTestbed t;
  alisql::Paxos* node1;
  alisql::Paxos* node2;
  alisql::Paxos* node3;
};

class ThreeNodesCluster : public ::testing::Test {
 protected:
  void SetUp() override {
    node1 = t.createRDLogNode().initAsMember(
        1, t.getNodesIpAddrVectorRef3(1, 2, 3));
    node2 = t.createRDLogNode().initAsMember(
        2, t.getNodesIpAddrVectorRef3(1, 2, 3));
    node3 = t.createRDLogNode().initAsMember(
        3, t.getNodesIpAddrVectorRef3(1, 2, 3));

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
  }

  void TearDown() override {}

  PaxosTestbed t;
  alisql::Paxos* node1;
  alisql::Paxos* node2;
  alisql::Paxos* node3;
};

class FiveNodesCluster : public ::testing::Test {
 protected:
  void SetUp() override {
    node1 = t.createRDLogNode().initAsMember(
        1, t.getNodesIpAddrVectorRef5(1, 2, 3, 4, 5));
    node2 = t.createRDLogNode().initAsMember(
        2, t.getNodesIpAddrVectorRef5(1, 2, 3, 4, 5));
    node3 = t.createRDLogNode().initAsMember(
        3, t.getNodesIpAddrVectorRef5(1, 2, 3, 4, 5));
    node4 = t.createRDLogNode().initAsMember(
        4, t.getNodesIpAddrVectorRef5(1, 2, 3, 4, 5));
    node5 = t.createRDLogNode().initAsMember(
        5, t.getNodesIpAddrVectorRef5(1, 2, 3, 4, 5));

    EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(2)), true, 2000);
    EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(3)), true, 2000);
    EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(4)), true, 2000);
    EXPECT_EQ_EVENTUALLY(node1->isConnected(t.getNodeIpAddr(5)), true, 2000);

    node1->requestVote();
    EXPECT_EQ_EVENTUALLY(node1->getState(), alisql::Paxos::LEADER, 3000);

    EXPECT_EQ_EVENTUALLY(node1->getLog()->getLastLogTerm(), node1->getTerm(),
                         1000);
    EXPECT_EQ_EVENTUALLY(node2->getLog()->getLastLogTerm(), node1->getTerm(),
                         1000);
    EXPECT_EQ_EVENTUALLY(node3->getLog()->getLastLogTerm(), node1->getTerm(),
                         1000);
    EXPECT_EQ_EVENTUALLY(node4->getLog()->getLastLogTerm(), node1->getTerm(),
                         1000);
    EXPECT_EQ_EVENTUALLY(node5->getLog()->getLastLogTerm(), node1->getTerm(),
                         1000);
  }

  void TearDown() override {}

  PaxosTestbed t;
  alisql::Paxos* node1;
  alisql::Paxos* node2;
  alisql::Paxos* node3;
  alisql::Paxos* node4;
  alisql::Paxos* node5;
};
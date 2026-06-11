// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include "files.h"
#include "paxos.h"
#include "paxos_configuration.h"
#include "paxos_server.h"
#include "rd_paxos_log.h"

using namespace alisql;

TEST(configuration, ConfigurationStatistic) {
  std::shared_ptr<StableConfiguration> config =
      std::make_shared<StableConfiguration>();
  EXPECT_TRUE(static_cast<bool>(config));

  // Empty configuration, all quorum should be true and all min should be 0.
  EXPECT_TRUE(config->quorumAll(&Server::haveVoted));
  EXPECT_EQ(config->quorumMin(&Server::getMatchIndex), 0);
  EXPECT_EQ(config->forceMin(&Server::getMatchIndex), 0);
  EXPECT_EQ(config->allMin(&Server::getMatchIndex), 0);

  // Add 5 servers, a local one and 4 remote ones.
  std::shared_ptr<RemoteServer> rptr1, rptr2, rptr3, rptr4;
  std::shared_ptr<LocalServer> lptr;

  config->servers.push_back(lptr = std::make_shared<LocalServer>(1));
  lptr->lastSyncedIndex = 7;
  lptr->strAddr = std::string("127.0.0.1:10001");

  config->servers.push_back(rptr1 = std::make_shared<RemoteServer>(2));
  rptr1->matchIndex = 5;
  rptr1->strAddr = std::string("127.0.0.1:10002");

  config->servers.push_back(rptr2 = std::make_shared<RemoteServer>(3));
  rptr2->matchIndex = 6;
  rptr2->strAddr = std::string("127.0.0.1:10003");

  config->servers.push_back(rptr3 = std::make_shared<RemoteServer>(4));
  rptr3->matchIndex = 7;
  rptr3->strAddr = std::string("127.0.0.1:10004");

  config->servers.push_back(rptr4 = std::make_shared<RemoteServer>(5));
  rptr4->matchIndex = 4;
  rptr4->strAddr = std::string("127.0.0.1:10005");

  // No one has voted yet, so quorumAll should be false.
  EXPECT_FALSE(config->quorumAll(&Server::haveVoted));
  rptr1->voted = true;
  rptr2->voted = true;
  // Only 2 servers have voted, so quorumAll should be false.
  EXPECT_FALSE(config->quorumAll(&Server::haveVoted));
  rptr3->voted = true;
  // 3 servers have voted, so quorumAll should be true.
  EXPECT_TRUE(config->quorumAll(&Server::haveVoted));

  // 4, 5, 6, 7, 7, quorumMin should be 6, allMin should be 4.
  EXPECT_EQ(config->quorumMin(&Server::getMatchIndex), 6);
  EXPECT_EQ(config->allMin(&Server::getMatchIndex), 4);
  // No server is set to forceSync, so forceMin should be UINT64_MAX.
  EXPECT_EQ(config->forceMin(&Server::getMatchIndex), UINT64_MAX);
  rptr3->forceSync = true;
  EXPECT_EQ(config->forceMin(&Server::getMatchIndex), 7);
}

TEST(configuration, MemberString) {
  std::shared_ptr<StableConfiguration> config =
      std::make_shared<StableConfiguration>();
  EXPECT_TRUE(static_cast<bool>(config));

  std::shared_ptr<RemoteServer> rptr1, rptr2;
  std::shared_ptr<LocalServer> lptr;

  config->servers.push_back(lptr = std::make_shared<LocalServer>(1));
  lptr->strAddr = std::string("127.0.0.1:10001");

  config->servers.push_back(rptr1 = std::make_shared<RemoteServer>(2));
  rptr1->strAddr = std::string("127.0.0.1:10002");

  config->servers.push_back(rptr2 = std::make_shared<RemoteServer>(3));
  rptr2->strAddr = std::string("127.0.0.1:10003");

  // static tool functions

  // a member's address string
  EXPECT_EQ(StableConfiguration::memberToString(lptr),
            std::string("127.0.0.1:10001#5"));
  EXPECT_EQ(StableConfiguration::memberToString(rptr1),
            std::string("127.0.0.1:10002#5"));
  EXPECT_EQ(StableConfiguration::memberToString(rptr2),
            std::string("127.0.0.1:10003#5"));
  rptr2->forceSync = true;
  rptr2->electionWeight = 9;
  EXPECT_EQ(StableConfiguration::memberToString(rptr2),
            std::string("127.0.0.1:10003#9S"));

  // a learner's address string
  std::shared_ptr<RemoteServer> rptr3;
  config->learners.push_back(rptr3 = std::make_shared<RemoteServer>(101));
  rptr3->strAddr = std::string("127.0.0.1:10004");
  EXPECT_EQ(StableConfiguration::learnerToString(rptr3),
            std::string("127.0.0.1:10004$0"));

  // all members' address string
  EXPECT_EQ(
      StableConfiguration::configToString(config->servers, lptr->strAddr),
      std::string("127.0.0.1:10001#5;127.0.0.1:10002#5;127.0.0.1:10003#9S@1"));
  EXPECT_EQ(
      StableConfiguration::configToString(config->servers, rptr1->strAddr),
      std::string("127.0.0.1:10001#5;127.0.0.1:10002#5;127.0.0.1:10003#9S@2"));
  EXPECT_EQ(
      StableConfiguration::configToString(config->servers, rptr2->strAddr),
      std::string("127.0.0.1:10001#5;127.0.0.1:10002#5;127.0.0.1:10003#9S@3"));
  EXPECT_EQ(
      StableConfiguration::configToString(config->learners, rptr3->strAddr),
      std::string("127.0.0.1:10004#5@1"));

  // convert a address string to a vector of seperate address strings
  std::string strConfig = "127.0.0.1:10001;127.0.0.1:10002;127.0.0.1:10003@1";
  uint64_t index = 0;
  std::vector<std::string> v =
      StableConfiguration::stringToVector(strConfig, index);
  EXPECT_EQ(v[0], "127.0.0.1:10001");
  EXPECT_EQ(v[1], "127.0.0.1:10002");
  EXPECT_EQ(v[2], "127.0.0.1:10003");
  EXPECT_EQ(index, 1);

  index = 0;
  strConfig = "127.0.0.1:10001;127.0.0.1:10002";
  v = StableConfiguration::stringToVector(strConfig, index);
  EXPECT_EQ(v[0], "127.0.0.1:10001");
  EXPECT_EQ(v[1], "127.0.0.1:10002");
  EXPECT_EQ(index, 0);

  // get the address string from a member string
  EXPECT_EQ(std::string("127.0.0.1:10001"),
            StableConfiguration::getAddr(std::string("127.0.0.1:10001#5")));
  EXPECT_EQ(std::string("127.0.0.1:10001"),
            StableConfiguration::getAddr(std::string("127.0.0.1:10001$:2")));

  // judge if a address string is in a vector of address strings
  strConfig = "127.0.0.1:11004$0;127.0.0.1:11005$0;0;127.0.0.1:11006$0";
  v = StableConfiguration::stringToVector(strConfig, index);
  EXPECT_TRUE(
      StableConfiguration::isServerInVector(std::string("127.0.0.1:11005"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11005$02"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11005#5"), v));
  EXPECT_FALSE(
      StableConfiguration::isServerInVector(std::string("127.0.0.1:11001"), v));
  EXPECT_FALSE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11001#5"), v));

  strConfig =
      "127.0.0.1:11004$:1;127.0.0.1:11005$;2;0;127.0.0.1:11006$0;127.0.0.1:"
      "11007$@3";
  index = 0;
  v = StableConfiguration::stringToVector(strConfig, index);
  EXPECT_EQ(index, 0);
  EXPECT_EQ(v.size(), 5);
  EXPECT_TRUE(
      StableConfiguration::isServerInVector(std::string("127.0.0.1:11005"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11005$;2"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11007$@3"), v));

  strConfig =
      "127.0.0.1:11004$0;127.0.0.1:11005$0;0;127.0.0.1:11006$0;127.0.0.1:"
      "11007$;0";
  index = 0;
  v = StableConfiguration::stringToVector(strConfig, index);
  EXPECT_EQ(index, 0);
  EXPECT_EQ(v.size(), 5);
  EXPECT_TRUE(
      StableConfiguration::isServerInVector(std::string("127.0.0.1:11007"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11007$;0"), v));
  EXPECT_TRUE(
      StableConfiguration::isServerInVector(std::string("127.0.0.1:11004"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11004$0"), v));
  EXPECT_EQ(v[0], "127.0.0.1:11004$0");
  EXPECT_EQ(v[4], "127.0.0.1:11007$;0");

  strConfig =
      "0;127.0.0.1:11004$0;127.0.0.1:11005$0;0;127.0.0.1:11006$0;127.0.0.1:"
      "11007$;0";
  index = 0;
  v = StableConfiguration::stringToVector(strConfig, index);
  EXPECT_EQ(index, 0);
  EXPECT_EQ(v.size(), 6);
  EXPECT_TRUE(
      StableConfiguration::isServerInVector(std::string("127.0.0.1:11007"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11007$;0"), v));
  EXPECT_TRUE(
      StableConfiguration::isServerInVector(std::string("127.0.0.1:11004"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11004$0"), v));
  EXPECT_EQ(v[0], "0");
  EXPECT_EQ(v[5], "127.0.0.1:11007$;0");

  // members and learners' address, they use above tool function
  EXPECT_EQ(
      config->membersToString(lptr->strAddr),
      std::string("127.0.0.1:10001#5;127.0.0.1:10002#5;127.0.0.1:10003#9S@1"));
  EXPECT_EQ(
      config->membersToString(rptr1->strAddr),
      std::string("127.0.0.1:10001#5;127.0.0.1:10002#5;127.0.0.1:10003#9S@2"));
  EXPECT_EQ(
      config->membersToString(rptr2->strAddr),
      std::string("127.0.0.1:10001#5;127.0.0.1:10002#5;127.0.0.1:10003#9S@3"));
  EXPECT_EQ(
      config->membersToString(),
      std::string("127.0.0.1:10001#5;127.0.0.1:10002#5;127.0.0.1:10003#9S"));
  EXPECT_EQ(config->learnersToString(), std::string("127.0.0.1:10004$0"));
}

TEST(configuration, GetServers) {
  std::shared_ptr<StableConfiguration> config =
      std::make_shared<StableConfiguration>();
  EXPECT_TRUE(static_cast<bool>(config));

  std::shared_ptr<RemoteServer> rptr1, rptr2, rptr3, rptr4;
  std::shared_ptr<LocalServer> lptr;

  config->servers.push_back(lptr = std::make_shared<LocalServer>(1));
  lptr->strAddr = std::string("127.0.0.1:10001");

  config->servers.push_back(rptr1 = std::make_shared<RemoteServer>(2));
  rptr1->strAddr = std::string("127.0.0.1:10002");

  config->servers.push_back(rptr2 = std::make_shared<RemoteServer>(3));
  rptr2->strAddr = std::string("127.0.0.1:10003");

  config->learners.push_back(rptr3 = std::make_shared<RemoteServer>(101));
  rptr3->strAddr = std::string("127.0.0.1:10004");

  config->learners.push_back(rptr4 = std::make_shared<RemoteServer>(102));
  rptr4->strAddr = std::string("127.0.0.1:10005");

  // Get server and learner number
  EXPECT_EQ(config->getServerNum(), 3);
  // only works when using installConfig, addMember and delMember
  EXPECT_EQ(config->getServerNumLockFree(), 0);
  EXPECT_EQ(config->getLearnerNum(), 2);

  // Get server and learner list
  std::vector<Configuration::ServerRef> members = config->getServers();
  EXPECT_EQ(members.size(), 3);
  EXPECT_EQ(members[0]->serverId, 1);
  EXPECT_EQ(members[1]->serverId, 2);
  EXPECT_EQ(members[2]->serverId, 3);

  std::vector<Configuration::ServerRef> learners = config->getLearners();
  EXPECT_EQ(learners.size(), 2);
  EXPECT_EQ(learners[0]->serverId, 101);
  EXPECT_EQ(learners[1]->serverId, 102);

  // Get server and learner by id
  EXPECT_EQ(config->getServer(1)->serverId, 1);
  EXPECT_EQ(config->getServer(2)->serverId, 2);
  EXPECT_EQ(config->getServer(3)->serverId, 3);
  EXPECT_EQ(config->getServer(101)->serverId, 101);
  EXPECT_EQ(config->getServer(102)->serverId, 102);
  EXPECT_EQ(config->getServer(4), nullptr);
  EXPECT_EQ(config->getServer(103), nullptr);

  // Get server and learner by address
  EXPECT_EQ(config->getServerIdFromAddr(std::string("127.0.0.1:10001")), 1);
  EXPECT_EQ(config->getServerIdFromAddr(std::string("127.0.0.1:10002")), 2);
  EXPECT_EQ(config->getServerIdFromAddr(std::string("127.0.0.1:10003")), 3);
  EXPECT_EQ(config->getLearnerByAddr(std::string("127.0.0.1:10004"))->serverId,
            101);
  EXPECT_EQ(config->getLearnerByAddr(std::string("127.0.0.1:10005"))->serverId,
            102);
}

TEST(configuration, AddAndDeleteMember) {
  std::shared_ptr<StableConfiguration> config =
      std::make_shared<StableConfiguration>();
  EXPECT_TRUE(static_cast<bool>(config));

  std::shared_ptr<RemoteServer> rptr1, rptr2, rptr3, rptr4;
  std::shared_ptr<LocalServer> lptr;

  config->servers.push_back(lptr = std::make_shared<LocalServer>(1));
  lptr->strAddr = std::string("127.0.0.1:10001");

  config->servers.push_back(rptr1 = std::make_shared<RemoteServer>(2));
  rptr1->strAddr = std::string("127.0.0.1:10002");

  config->servers.push_back(rptr2 = std::make_shared<RemoteServer>(3));
  rptr2->strAddr = std::string("127.0.0.1:10003");

  // delete a member
  config->delMember(std::string("127.0.0.1:10002"), NULL);
  EXPECT_EQ(config->getServerNum(), 2);
  EXPECT_EQ(config->membersToString(lptr->strAddr),
            std::string("127.0.0.1:10001#5;0;127.0.0.1:10003#5@1"));

  // add it back
  std::vector<std::string> strConfig;
  strConfig.emplace_back("127.0.0.1:10001");
  strConfig.emplace_back("127.0.0.1:10003");
  std::string log_dir = std::string("paxosLogTestDir") + std::to_string(1);

  // delete log directory in case it already exists
  deleteDir(log_dir.c_str());

  std::shared_ptr<PaxosLog> rlog = std::make_shared<RDPaxosLog>(
      log_dir, true /*compress*/, RDPaxosLog::DEFAULT_WRITE_BUFFER_SIZE);
  Paxos* dummy_paxos = new Paxos(1000, rlog);
  dummy_paxos->init(strConfig, 1);

  // will fail because only a learner can be added
  EXPECT_EQ(config->addMember(std::string("127.0.0.1:10002"), dummy_paxos), -1);
  EXPECT_EQ(config->getServerNum(), 2);

  // add server
  config->addServer(config->servers, rptr1);
  EXPECT_EQ(
      config->membersToString(std::string("127.0.0.1:10002")),
      std::string("127.0.0.1:10001#5;127.0.0.1:10002#5;127.0.0.1:10003#5@2"));

  // add learners
  strConfig.clear();
  strConfig.push_back(std::string("127.0.0.1:10004"));
  strConfig.push_back(std::string("127.0.0.1:10005"));
  strConfig.push_back(std::string("127.0.0.1:10006"));
  config->addLearners(strConfig, dummy_paxos);
  EXPECT_EQ(config->getLearnerNum(), 3);
  EXPECT_EQ(
      config->learnersToString(),
      std::string("127.0.0.1:10004$0;127.0.0.1:10005$0;127.0.0.1:10006$0"));

  // add a member from a learner
  EXPECT_EQ(config->addMember(std::string("127.0.0.1:10005"), dummy_paxos), 0);
  EXPECT_EQ(config->getServerNum(), 4);

  // delete a learner
  strConfig.clear();
  strConfig.push_back(std::string("127.0.0.1:10004"));
  config->delLearners(strConfig, dummy_paxos);
  EXPECT_EQ(config->getLearnerNum(), 1);
  EXPECT_EQ(config->learnersToString(), std::string("0;0;127.0.0.1:10006$0"));

  // delete all learners
  config->delAllLearners();
  EXPECT_EQ(config->getLearnerNum(), 0);
  EXPECT_EQ(config->learnersToString(), std::string(""));

  // delete all remote servers
  config->serversNum = 4;
  config->delAllRemoteServer(lptr->strAddr, dummy_paxos);
  EXPECT_EQ(config->getServerNum(), 1);
  delete dummy_paxos;
  deleteDir(log_dir.c_str());
}

TEST(configuration, ConfigureMember) {
  std::shared_ptr<StableConfiguration> config =
      std::make_shared<StableConfiguration>();
  EXPECT_TRUE(static_cast<bool>(config));

  std::shared_ptr<RemoteServer> rptr1, rptr2;
  std::shared_ptr<LocalServer> lptr;

  config->servers.push_back(lptr = std::make_shared<LocalServer>(1));
  lptr->strAddr = std::string("127.0.0.1:10001");

  config->servers.push_back(rptr1 = std::make_shared<RemoteServer>(2));
  rptr1->strAddr = std::string("127.0.0.1:10002");

  config->servers.push_back(rptr2 = std::make_shared<RemoteServer>(3));
  rptr2->strAddr = std::string("127.0.0.1:10003");

  config->configureMember(1, true, 9, NULL);
  EXPECT_EQ(
      config->membersToString(lptr->strAddr),
      std::string("127.0.0.1:10001#9S;127.0.0.1:10002#5;127.0.0.1:10003#5@1"));
}

TEST(configuration, InitServerFromString) {
  std::shared_ptr<StableConfiguration> config =
      std::make_shared<StableConfiguration>();
  EXPECT_TRUE(static_cast<bool>(config));

  std::shared_ptr<LocalServer> lptr = std::make_shared<LocalServer>(1);

  // valid
  std::string strConfig = "127.0.0.1:11001#9S";
  config->initServerFromString(lptr, strConfig);
  EXPECT_EQ(lptr->strAddr, std::string("127.0.0.1:11001"));
  EXPECT_EQ(lptr->electionWeight, 9);
  EXPECT_TRUE(lptr->forceSync);

  // valid
  strConfig = "127.0.0.1:11002#6";
  config->initServerFromString(lptr, strConfig);
  EXPECT_EQ(lptr->strAddr, std::string("127.0.0.1:11002"));
  EXPECT_EQ(lptr->electionWeight, 6);
  EXPECT_FALSE(lptr->forceSync);

  // valid
  strConfig = "127.0.0.1:11003#S";
  config->initServerFromString(lptr, strConfig);
  EXPECT_EQ(lptr->strAddr, std::string("127.0.0.1:11003"));
  EXPECT_EQ(lptr->electionWeight, 5);
  EXPECT_TRUE(lptr->forceSync);

  // invalid
  strConfig = "127.0.0.1:11004#S8";
  config->initServerFromString(lptr, strConfig);
  EXPECT_EQ(lptr->strAddr, std::string("127.0.0.1:11004"));
  EXPECT_EQ(lptr->electionWeight, 5);
  EXPECT_FALSE(lptr->forceSync);

  // invalid
  strConfig = "127.0.0.1:11005#10S";
  config->initServerFromString(lptr, strConfig);
  EXPECT_EQ(lptr->strAddr, std::string("127.0.0.1:11005"));
  EXPECT_EQ(lptr->electionWeight, 5);
  EXPECT_FALSE(lptr->forceSync);

  // valid
  strConfig = "127.0.0.1:11006";
  config->initServerFromString(lptr, strConfig);
  EXPECT_EQ(lptr->strAddr, std::string("127.0.0.1:11006"));
  EXPECT_EQ(lptr->electionWeight, 5);
  EXPECT_FALSE(lptr->forceSync);

  // invalid
  strConfig = "127.0.0.1:11007$";
  config->initServerFromString(lptr, strConfig);
  EXPECT_EQ(lptr->strAddr, std::string("127.0.0.1:11007"));
  EXPECT_EQ(lptr->learnerSource, 0);
  EXPECT_EQ(lptr->electionWeight, 5);
  EXPECT_FALSE(lptr->forceSync);

  // valid
  strConfig = "127.0.0.1:11008$123";
  config->initServerFromString(lptr, strConfig);
  EXPECT_EQ(lptr->strAddr, std::string("127.0.0.1:11008"));
  EXPECT_EQ(lptr->learnerSource, 123);
  EXPECT_EQ(lptr->electionWeight, 5);
  EXPECT_FALSE(lptr->forceSync);

  // invalid
  strConfig = "127.0.0.1:11009$N";
  config->initServerFromString(lptr, strConfig);
  EXPECT_EQ(lptr->strAddr, std::string("127.0.0.1:11009"));
  EXPECT_EQ(lptr->learnerSource, 0);
  EXPECT_EQ(lptr->electionWeight, 5);
  EXPECT_FALSE(lptr->forceSync);

  // valid
  strConfig = "127.0.0.1:11010$7";
  config->initServerFromString(lptr, strConfig);
  EXPECT_EQ(lptr->strAddr, std::string("127.0.0.1:11010"));
  EXPECT_EQ(lptr->learnerSource, 7);
  EXPECT_EQ(lptr->electionWeight, 5);
  EXPECT_FALSE(lptr->forceSync);

  // valid
  strConfig = "127.0.0.1:11011$87";
  config->initServerFromString(lptr, strConfig);
  EXPECT_EQ(lptr->strAddr, std::string("127.0.0.1:11011"));
  EXPECT_EQ(lptr->learnerSource, 87);
  EXPECT_EQ(lptr->electionWeight, 5);
  EXPECT_FALSE(lptr->forceSync);

  // invalid
  strConfig = "127.0.0.1:11011$1000";
  config->initServerFromString(lptr, strConfig);
  EXPECT_EQ(lptr->strAddr, std::string("127.0.0.1:11011"));
  EXPECT_EQ(lptr->learnerSource, 0);
  EXPECT_EQ(lptr->electionWeight, 5);
  EXPECT_FALSE(lptr->forceSync);

  // invalid
  strConfig = "127.0.0.1:11011$x00";
  config->initServerFromString(lptr, strConfig);
  EXPECT_EQ(lptr->strAddr, std::string("127.0.0.1:11011"));
  EXPECT_EQ(lptr->learnerSource, 0);
  EXPECT_EQ(lptr->electionWeight, 5);
  EXPECT_FALSE(lptr->forceSync);
}


TEST(StableConfiguration, membersToString) {
  std::shared_ptr<StableConfiguration> config =
      std::shared_ptr<StableConfiguration>(new StableConfiguration());
  EXPECT_TRUE(config.get() != nullptr);

  std::shared_ptr<RemoteServer> ptr;
  std::shared_ptr<LocalServer> lptr;

  config->servers.push_back(lptr = std::make_shared<LocalServer>(1));
  lptr->strAddr = std::string("127.0.0.1:10001");

  config->servers.push_back(ptr = std::make_shared<RemoteServer>(2));
  ptr->matchIndex = 5;
  ptr->strAddr = std::string("127.0.0.1:10002");

  config->servers.push_back(ptr = std::make_shared<RemoteServer>(3));
  ptr->strAddr = std::string("127.0.0.1:10003");
  ptr->matchIndex = 6;

  config->delMember(std::string("127.0.0.1:10002"), NULL);
  EXPECT_EQ(config->getServerNum(), 2);
  EXPECT_EQ(config->servers[0]->serverId, 1);
  EXPECT_EQ(config->servers[0]->strAddr, std::string("127.0.0.1:10001"));
  EXPECT_EQ(config->servers[2]->serverId, 3);
  EXPECT_EQ(config->servers[2]->strAddr, std::string("127.0.0.1:10003"));
  EXPECT_EQ(config->membersToString(lptr->strAddr),
            std::string("127.0.0.1:10001#5;0;127.0.0.1:10003#5@1"));
  config->configureMember(1, true, 9, NULL);
  EXPECT_EQ(config->membersToString(lptr->strAddr),
            std::string("127.0.0.1:10001#9S;0;127.0.0.1:10003#5@1"));
}

TEST(StableConfiguration, stringToVector) {
  std::string strConfig = "127.0.0.1:10001;127.0.0.1:10002;127.0.0.1:10003@1";
  uint64_t index = 0;
  std::vector<std::string> v =
      StableConfiguration::stringToVector(strConfig, index);
  EXPECT_EQ(v[0], "127.0.0.1:10001");
  EXPECT_EQ(v[1], "127.0.0.1:10002");
  EXPECT_EQ(v[2], "127.0.0.1:10003");
  EXPECT_EQ(index, 1);

  // for Learner string
  strConfig = "127.0.0.1:10001;127.0.0.1:10002";
  v = StableConfiguration::stringToVector(strConfig, index);
  EXPECT_EQ(v[0], "127.0.0.1:10001");
  EXPECT_EQ(v[1], "127.0.0.1:10002");

  EXPECT_EQ(std::string("127.0.0.1:10001"),
            StableConfiguration::getAddr(std::string("127.0.0.1:10001#5")));
  EXPECT_EQ(std::string("127.0.0.1:10001"),
            StableConfiguration::getAddr(std::string("127.0.0.1:10001$:2")));

  strConfig = "127.0.0.1:11004$0;127.0.0.1:11005$0;0;127.0.0.1:11006$0";
  v = StableConfiguration::stringToVector(strConfig, index);
  EXPECT_TRUE(
      StableConfiguration::isServerInVector(std::string("127.0.0.1:11005"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11005$02"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11005#5"), v));
  EXPECT_FALSE(
      StableConfiguration::isServerInVector(std::string("127.0.0.1:11001"), v));
  EXPECT_FALSE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11001#5"), v));

  // test learnerSource larger than 99 and 109 and 159
  strConfig =
      "127.0.0.1:11004$:1;127.0.0.1:11005$;2;0;127.0.0.1:11006$0;127.0.0.1:"
      "11007$@3";
  index = 0;
  v = StableConfiguration::stringToVector(strConfig, index);
  EXPECT_TRUE(index == 0);
  EXPECT_TRUE(v.size() == 5);
  EXPECT_TRUE(
      StableConfiguration::isServerInVector(std::string("127.0.0.1:11005"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11005$;2"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11007$@3"), v));

  strConfig =
      "127.0.0.1:11004$0;127.0.0.1:11005$0;0;127.0.0.1:11006$0;127.0.0.1:"
      "11007$;0";
  index = 0;
  v = StableConfiguration::stringToVector(strConfig, index);
  EXPECT_TRUE(index == 0);
  EXPECT_EQ(v.size(), 5);
  EXPECT_TRUE(
      StableConfiguration::isServerInVector(std::string("127.0.0.1:11007"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11007$;0"), v));
  EXPECT_TRUE(
      StableConfiguration::isServerInVector(std::string("127.0.0.1:11004"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11004$0"), v));
  EXPECT_EQ(v[0], "127.0.0.1:11004$0");
  EXPECT_EQ(v[4], "127.0.0.1:11007$;0");

  strConfig =
      "0;127.0.0.1:11004$0;127.0.0.1:11005$0;0;127.0.0.1:11006$0;127.0.0.1:"
      "11007$;0";
  index = 0;
  v = StableConfiguration::stringToVector(strConfig, index);
  EXPECT_TRUE(index == 0);
  EXPECT_EQ(v.size(), 6);
  EXPECT_TRUE(
      StableConfiguration::isServerInVector(std::string("127.0.0.1:11007"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11007$;0"), v));
  EXPECT_TRUE(
      StableConfiguration::isServerInVector(std::string("127.0.0.1:11004"), v));
  EXPECT_TRUE(StableConfiguration::isServerInVector(
      std::string("127.0.0.1:11004$0"), v));
  EXPECT_EQ(v[0], "0");
  EXPECT_EQ(v[5], "127.0.0.1:11007$;0");

  strConfig = "127.0.0.1:10001;0;127.0.0.1:10002";
  auto v1 = StableConfiguration::stringToVector(strConfig, index);
  EXPECT_EQ(v1[0], "127.0.0.1:10001");
  EXPECT_EQ(v1[1], "0");
  EXPECT_EQ(v1[2], "127.0.0.1:10002");
}

TEST(StableConfiguration, uncontinuous_serverid) {
  std::string strConfig;
  std::shared_ptr<StableConfiguration> config =
      std::shared_ptr<StableConfiguration>(new StableConfiguration());
  EXPECT_TRUE(config.get() != nullptr);

  std::shared_ptr<RemoteServer> ptr;
  std::shared_ptr<LocalServer> lptr;

  config->servers.push_back(lptr = std::make_shared<LocalServer>(1));
  lptr->strAddr = std::string("127.0.0.1:10001");

  config->servers.push_back(nullptr);

  config->servers.push_back(ptr = std::make_shared<RemoteServer>(3));
  ptr->matchIndex = 5;
  ptr->strAddr = std::string("127.0.0.1:10002");

  config->servers.push_back(ptr = std::make_shared<RemoteServer>(4));
  ptr->strAddr = std::string("127.0.0.1:10003");
  ptr->matchIndex = 6;

  uint64_t qmin = config->quorumMin(&Server::getMatchIndex);
  EXPECT_EQ(qmin, 5);

  config->delMember(std::string("127.0.0.1:10002"), NULL);
  EXPECT_EQ(config->getServerNum(), 2);
  EXPECT_EQ(config->servers[0]->serverId, 1);
  EXPECT_EQ(config->servers[0]->strAddr, std::string("127.0.0.1:10001"));
  EXPECT_EQ(config->servers[3]->serverId, 4);
  EXPECT_EQ(config->servers[3]->strAddr, std::string("127.0.0.1:10003"));
  EXPECT_EQ(config->membersToString(lptr->strAddr),
            std::string("127.0.0.1:10001#5;0;0;127.0.0.1:10003#5@1"));

  config->configureMember(1, true, 9, NULL);
  EXPECT_EQ(config->membersToString(lptr->strAddr),
            std::string("127.0.0.1:10001#9S;0;0;127.0.0.1:10003#5@1"));
}
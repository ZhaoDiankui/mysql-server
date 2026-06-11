// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>

#include "consensus.h"
#include "service.h"
#include "ut_helper.h"

using namespace alisql;

const uint64_t serverServerId = 1;
const uint64_t clientServerId = 2;

const int timeoutMS = 2000;
const int durationMS = 300;

TEST(Service, init) {
  Service* srv = new Service(NULL);
  EXPECT_TRUE(srv);

  EXPECT_EQ(srv->init(2, 4, 300ULL, false, 1), 0);
  EXPECT_EQ(srv->start(11000), 0);

  EXPECT_TRUE(srv->workPoolIsRunning());
  EXPECT_TRUE(srv->getNet() != NULL);
  EXPECT_TRUE(srv->getWorkPool() != NULL);
  EXPECT_EQ(srv->getWorkPool()->thread_count, 4);
  EXPECT_TRUE(srv->getHeartbeatPool() != NULL);
  EXPECT_EQ(srv->getHeartbeatPool()->thread_count, 1);
  EXPECT_TRUE(srv->getThreadTimerService() != NULL);

  EXPECT_EQ(srv->shutdown(), 0);
  delete srv;
}

TEST(Service, sendAsyncEvent) {
  Service* srv = new Service(NULL);
  EXPECT_TRUE(srv);

  EXPECT_EQ(srv->init(), 0);
  EXPECT_EQ(srv->start(11000), 0);

  int cb1cnt = 0;
  auto cb1 = [&cb1cnt]() { cb1cnt++; };
  EXPECT_EQ(srv->sendAsyncEvent(cb1), 0);
  EXPECT_EQ_EVENTUALLY(cb1cnt, 1, timeoutMS);
  EXPECT_EQ(srv->sendAsyncEvent(cb1), 0);
  EXPECT_EQ_EVENTUALLY(cb1cnt, 2, timeoutMS);

  int cb2cnt = 0;
  auto cb2 = [&cb2cnt](int delta) { cb2cnt += delta; };
  EXPECT_EQ(srv->sendAsyncEvent(cb2, 2), 0);
  EXPECT_EQ_EVENTUALLY(cb2cnt, 2, timeoutMS);
  EXPECT_EQ(srv->sendAsyncEvent(cb2, 3), 0);
  EXPECT_EQ_EVENTUALLY(cb2cnt, 5, timeoutMS);

  int cb3cnt = 0;
  auto cb3 = [&cb3cnt](int* pDelta) { cb3cnt += *pDelta; };
  int delta = 2;
  EXPECT_EQ(srv->sendAsyncEvent(cb3, &delta), 0);
  EXPECT_EQ_EVENTUALLY(cb3cnt, 2, timeoutMS);
  delta = 3;
  EXPECT_EQ(srv->sendAsyncEvent(cb3, &delta), 0);
  EXPECT_EQ_EVENTUALLY(cb3cnt, 5, timeoutMS);

  EXPECT_EQ(Service::running, 0);
  EXPECT_EQ(srv->shutdown(), 0);
  delete srv;
}

TEST(Service, timer) {
  auto srv = std::shared_ptr<Service>(new Service(NULL));
  EXPECT_EQ(srv->init(), 0);
  EXPECT_EQ(srv->start(11000), 0);

  auto tts = srv->getThreadTimerService();

  EXPECT_EQ(srv->getAsyncEventCnt(), 0);

  int cnt = 0;
  // ThreadTimer *tt=
  new ThreadTimer(tts, srv, 0.2, ThreadTimer::Oneshot, [&cnt] { cnt++; });

  EXPECT_EQ_EVENTUALLY(cnt, 1, timeoutMS);
  EXPECT_EQ(srv->getAsyncEventCnt(), 1);

  srv->shutdown();
}

class BaseMockConsensus : public Consensus {
 public:
  BaseMockConsensus() : cluster_id_(0), shutdown_(false) {}
  virtual ~BaseMockConsensus() {}
  virtual int onRequestVote(PaxosMsg* msg, PaxosMsg* rsp) { return 0; }
  virtual int onRequestVoteResponce(PaxosMsg* msg) { return 0; }
  virtual int onAppendLog(PaxosMsg* msg, PaxosMsg* rsp) { return 0; }
  virtual int onAppendLogSendFail(PaxosMsg* msg, uint64_t* newId) { return 0; }
  virtual int onAppendLogResponce(PaxosMsg* msg) { return 0; }
  virtual int requestVote(bool force) { return 0; }
  virtual uint64_t replicateLog(LogEntry& entry) { return 0; }
  virtual int appendLog(const bool needLock) { return 0; }
  virtual int onLeaderCommand(PaxosMsg* msg, PaxosMsg* rsp) { return 0; }
  virtual int onLeaderCommandResponce(PaxosMsg* msg) { return 0; }
  virtual int onClusterIdNotMatch(PaxosMsg* msg) { return 0; }
  virtual int onMsgPreCheck(PaxosMsg* msg, PaxosMsg* rsp) { return 0; }
  virtual int onMsgPreCheckFailed(PaxosMsg* msg) { return 0; }
  virtual uint64_t getClusterId() { return cluster_id_; }
  virtual int setClusterId(uint64_t ci) {
    cluster_id_ = ci;
    return 0;
  }
  virtual bool isShutdown() { return shutdown_; }
  uint64_t cluster_id_;
  bool shutdown_;
};

// the purpose of this test is to check memory leak with asan
TEST(Service, empty_consensus) {
  Service* server = new Service(NULL);
  EXPECT_TRUE(server);

  EXPECT_EQ(server->init(), 0);
  EXPECT_EQ(server->start(11000), 0);

  Service* client = new Service(NULL);
  EXPECT_TRUE(client);

  EXPECT_EQ(client->init(), 0);
  EXPECT_EQ(client->start(11001), 0);
  NetAddressPtr addr = client->createConnection("127.0.0.1:11000", NULL, 300UL);
  easy_addr_t easyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(addr)->getAddr();
  EXPECT_TRUE(easyAddr.port != 0);

  PaxosMsg msg;
  msg.set_clusterid(0);
  msg.set_serverid(clientServerId);
  msg.set_msgtype(Consensus::RequestVote);
  msg.set_msgid(100);
  msg.set_term(1);
  std::string buf;
  msg.SerializeToString(&buf);
  EXPECT_EQ(client->sendPacket(addr, buf, 100), 0);

  EXPECT_EQ_EVENTUALLY(server->getNet()->getDecodeCnt(), 1, timeoutMS);
  EXPECT_EQ_EVENTUALLY(server->getNet()->getReciveCnt(), 1, timeoutMS);
  // no replies received
  EXPECT_EQ_CONSISTENTLY(client->getNet()->getDecodeCnt(), 0, durationMS, 100);
  // client-side should receive a timeout event
  EXPECT_EQ_CONSISTENTLY(client->getNet()->getReciveCnt(), 1, durationMS, 100);

  EXPECT_EQ(Service::running, 0);

  EXPECT_EQ(server->shutdown(), 0);
  EXPECT_EQ(client->shutdown(), 0);

  delete server;
  delete client;
}

class MockConsensusRequestVote : public BaseMockConsensus {
 public:
  MockConsensusRequestVote() {
    onRequestVoteCnt = 0;
    onRequestVoteResponseCnt = 0;
  }
  virtual ~MockConsensusRequestVote() {}
  virtual int onRequestVote(PaxosMsg* msg, PaxosMsg* rsp) {
    std::cout << "onRequestVote" << std::endl;
    onRequestVoteCnt++;
    rsp->set_clusterid(0);
    rsp->set_serverid(serverServerId);
    rsp->set_msgtype(Consensus::RequestVoteResponce);
    rsp->set_msgid(msg->msgid());
    rsp->set_term(1);
    return 0;
  }
  virtual int onRequestVoteResponce(PaxosMsg* msg) {
    std::cout << "onRequestVoteResponce" << std::endl;
    onRequestVoteResponseCnt++;
    return 0;
  }
  int onRequestVoteCnt;
  int onRequestVoteResponseCnt;
};

TEST(Service, RequestVote) {
  MockConsensusRequestVote* serverConsensus = new MockConsensusRequestVote();
  Service* server = new Service(serverConsensus);
  EXPECT_TRUE(server);

  EXPECT_EQ(server->init(), 0);
  EXPECT_EQ(server->start(11000), 0);

  MockConsensusRequestVote* clientConsensus = new MockConsensusRequestVote();
  Service* client = new Service(clientConsensus);
  EXPECT_TRUE(client);

  EXPECT_EQ(client->init(), 0);
  EXPECT_EQ(client->start(11001), 0);
  NetAddressPtr addr = client->createConnection("127.0.0.1:11000", NULL, 300UL);
  easy_addr_t easyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(addr)->getAddr();
  EXPECT_TRUE(easyAddr.port != 0);

  PaxosMsg msg;
  msg.set_clusterid(0);
  msg.set_serverid(clientServerId);
  msg.set_msgtype(Consensus::RequestVote);
  msg.set_msgid(100);
  msg.set_term(1);
  std::string buf;
  msg.SerializeToString(&buf);
  EXPECT_EQ(client->sendPacket(addr, buf, msg.msgid()), 0);

  EXPECT_EQ_EVENTUALLY(serverConsensus->onRequestVoteCnt, 1, timeoutMS);
  EXPECT_EQ_EVENTUALLY(clientConsensus->onRequestVoteResponseCnt, 1, timeoutMS);

  EXPECT_EQ(Service::running, 0);

  EXPECT_EQ(server->shutdown(), 0);
  EXPECT_EQ(client->shutdown(), 0);

  delete server;
  delete client;

  delete serverConsensus;
  delete clientConsensus;
}

class MockConsensusAppendLog : public BaseMockConsensus {
 public:
  MockConsensusAppendLog() {
    onAppendLogCnt = 0;
    onAppendLogResponseCnt = 0;
  }
  virtual ~MockConsensusAppendLog() {}
  virtual int onAppendLog(PaxosMsg* msg, PaxosMsg* rsp) {
    std::cout << "onAppendLog" << std::endl;
    onAppendLogCnt++;
    rsp->set_clusterid(0);
    rsp->set_serverid(serverServerId);
    rsp->set_msgtype(Consensus::AppendLogResponce);
    rsp->set_msgid(msg->msgid());
    rsp->set_term(1);
    return 0;
  }
  virtual int onAppendLogResponce(PaxosMsg* msg) {
    std::cout << "onAppendLogResponce" << std::endl;
    onAppendLogResponseCnt++;
    return 0;
  }
  int onAppendLogCnt;
  int onAppendLogResponseCnt;
};

TEST(Service, AppendLog_multiple_times) {
  MockConsensusAppendLog* serverConsensus = new MockConsensusAppendLog();
  Service* server = new Service(serverConsensus);
  EXPECT_TRUE(server);

  EXPECT_EQ(server->init(), 0);
  EXPECT_EQ(server->start(11000), 0);

  MockConsensusAppendLog* clientConsensus = new MockConsensusAppendLog();
  Service* client = new Service(clientConsensus);
  EXPECT_TRUE(client);

  EXPECT_EQ(client->init(), 0);
  EXPECT_EQ(client->start(11001), 0);
  NetAddressPtr addr = client->createConnection("127.0.0.1:11000", NULL, 300UL);
  easy_addr_t easyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(addr)->getAddr();
  EXPECT_TRUE(easyAddr.port != 0);

  for (int i = 0; i < 10; i++) {
    PaxosMsg msg;
    msg.set_clusterid(0);
    msg.set_serverid(clientServerId);
    msg.set_msgtype(Consensus::AppendLog);
    msg.set_msgid(100 + i);
    msg.set_term(1);
    std::string buf;
    msg.SerializeToString(&buf);
    EXPECT_EQ(client->sendPacket(addr, buf, msg.msgid()), 0);

    EXPECT_EQ_EVENTUALLY(serverConsensus->onAppendLogCnt, i + 1, timeoutMS);
    EXPECT_EQ_EVENTUALLY(clientConsensus->onAppendLogResponseCnt, i + 1,
                         timeoutMS);
  }

  EXPECT_EQ(Service::running, 0);

  EXPECT_EQ(server->shutdown(), 0);
  EXPECT_EQ(client->shutdown(), 0);

  delete server;
  delete client;

  delete serverConsensus;
  delete clientConsensus;
}

TEST(Service, AppendLog) {
  MockConsensusAppendLog* serverConsensus = new MockConsensusAppendLog();
  Service* server = new Service(serverConsensus);
  EXPECT_TRUE(server);

  EXPECT_EQ(server->init(), 0);
  EXPECT_EQ(server->start(11000), 0);

  MockConsensusAppendLog* clientConsensus = new MockConsensusAppendLog();
  Service* client = new Service(clientConsensus);
  EXPECT_TRUE(client);

  EXPECT_EQ(client->init(), 0);
  EXPECT_EQ(client->start(11001), 0);
  NetAddressPtr addr = client->createConnection("127.0.0.1:11000", NULL, 300UL);
  easy_addr_t easyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(addr)->getAddr();
  EXPECT_TRUE(easyAddr.port != 0);

  PaxosMsg msg;
  msg.set_clusterid(0);
  msg.set_serverid(clientServerId);
  msg.set_msgtype(Consensus::AppendLog);
  msg.set_msgid(100);
  msg.set_term(1);
  std::string buf;
  msg.SerializeToString(&buf);
  EXPECT_EQ(client->sendPacket(addr, buf, msg.msgid()), 0);

  EXPECT_EQ_EVENTUALLY(serverConsensus->onAppendLogCnt, 1, timeoutMS);
  EXPECT_EQ_EVENTUALLY(clientConsensus->onAppendLogResponseCnt, 1, timeoutMS);

  EXPECT_EQ(Service::running, 0);

  EXPECT_EQ(server->shutdown(), 0);
  EXPECT_EQ(client->shutdown(), 0);

  delete server;
  delete client;

  delete serverConsensus;
  delete clientConsensus;
}

class MockConsensusLeaderCommand : public BaseMockConsensus {
 public:
  MockConsensusLeaderCommand() {
    onLeaderCommandCnt = 0;
    onLeaderCommandResponceCnt = 0;
  }
  virtual ~MockConsensusLeaderCommand() {}
  virtual int onLeaderCommand(PaxosMsg* msg, PaxosMsg* rsp) {
    std::cout << "onLeaderCommand" << std::endl;
    onLeaderCommandCnt++;
    rsp->set_clusterid(0);
    rsp->set_serverid(serverServerId);
    rsp->set_msgtype(Consensus::LeaderCommandResponce);
    rsp->set_msgid(msg->msgid());
    rsp->set_term(1);
    return 0;
  }
  virtual int onLeaderCommandResponce(PaxosMsg* msg) {
    std::cout << "onLeaderCommandResponce" << std::endl;
    onLeaderCommandResponceCnt++;
    return 0;
  }
  int onLeaderCommandCnt;
  int onLeaderCommandResponceCnt;
};

TEST(Service, LeaderCommand) {
  MockConsensusLeaderCommand* serverConsensus =
      new MockConsensusLeaderCommand();
  Service* server = new Service(serverConsensus);
  EXPECT_TRUE(server);

  EXPECT_EQ(server->init(), 0);
  EXPECT_EQ(server->start(11000), 0);

  MockConsensusLeaderCommand* clientConsensus =
      new MockConsensusLeaderCommand();
  Service* client = new Service(clientConsensus);
  EXPECT_TRUE(client);

  EXPECT_EQ(client->init(), 0);
  EXPECT_EQ(client->start(11001), 0);
  NetAddressPtr addr = client->createConnection("127.0.0.1:11000", NULL, 300UL);
  easy_addr_t easyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(addr)->getAddr();
  EXPECT_TRUE(easyAddr.port != 0);

  PaxosMsg msg;
  msg.set_clusterid(0);
  msg.set_serverid(clientServerId);
  msg.set_msgtype(Consensus::LeaderCommand);
  msg.set_msgid(100);
  msg.set_term(1);
  std::string buf;
  msg.SerializeToString(&buf);
  EXPECT_EQ(client->sendPacket(addr, buf, msg.msgid()), 0);

  EXPECT_EQ_EVENTUALLY(serverConsensus->onLeaderCommandCnt, 1, timeoutMS);
  EXPECT_EQ_EVENTUALLY(clientConsensus->onLeaderCommandResponceCnt, 1,
                       timeoutMS);

  EXPECT_EQ(Service::running, 0);

  EXPECT_EQ(server->shutdown(), 0);
  EXPECT_EQ(client->shutdown(), 0);

  delete server;
  delete client;

  delete serverConsensus;
  delete clientConsensus;
}

class MockConsensusAppendLogTimeout : public BaseMockConsensus {
 public:
  MockConsensusAppendLogTimeout() {
    onAppendLogCnt = 0;
    onAppendLogResponseCnt = 0;
  }
  virtual ~MockConsensusAppendLogTimeout() {}
  virtual int onAppendLog(PaxosMsg* msg, PaxosMsg* rsp) {
    std::cout << "onAppendLog and sleep 2s, msg id:" << msg->msgid()
              << std::endl;
    onAppendLogCnt++;
    sleep(2);
    rsp->set_clusterid(0);
    rsp->set_serverid(serverServerId);
    rsp->set_msgtype(Consensus::AppendLogResponce);
    rsp->set_msgid(msg->msgid());
    rsp->set_term(1);
    return 0;
  }
  virtual int onAppendLogResponce(PaxosMsg* msg) {
    std::cout << "onAppendLogResponce" << std::endl;
    onAppendLogResponseCnt++;
    return 0;
  }
  virtual int onAppendLogSendFail(PaxosMsg* msg, uint64_t* newId) {
    return -1;  // Do not retry.
  }
  int onAppendLogCnt;
  int onAppendLogResponseCnt;
};

TEST(Service, session_timeout) {
  MockConsensusAppendLogTimeout* serverConsensus =
      new MockConsensusAppendLogTimeout();
  Service* server = new Service(serverConsensus);
  EXPECT_TRUE(server);

  EXPECT_EQ(server->init(), 0);
  EXPECT_EQ(server->start(11000), 0);

  MockConsensusAppendLogTimeout* clientConsensus =
      new MockConsensusAppendLogTimeout();
  Service* client = new Service(clientConsensus);
  EXPECT_TRUE(client);

  EXPECT_EQ(client->init(), 0);
  EXPECT_EQ(client->start(11001), 0);
  NetAddressPtr addr = client->createConnection("127.0.0.1:11000", NULL, 300UL);
  easy_addr_t easyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(addr)->getAddr();
  EXPECT_TRUE(easyAddr.port != 0);

  // this will trigger timeout, because MockConsensusAppendLogTimeout will sleep
  // for 2s
  client->setSendPacketTimeout(1000);

  PaxosMsg msg;
  msg.set_clusterid(0);
  msg.set_serverid(clientServerId);
  msg.set_msgtype(Consensus::AppendLog);
  msg.set_msgid(100);
  msg.set_term(1);
  std::string buf;
  msg.SerializeToString(&buf);
  EXPECT_EQ(client->sendPacket(addr, buf, msg.msgid()), 0);

  EXPECT_EQ_EVENTUALLY(serverConsensus->onAppendLogCnt, 1, 1000);
  // receive a timeout msg
  EXPECT_EQ_EVENTUALLY(client->getNet()->getReciveCnt(), 1, 2000);
  EXPECT_EQ_CONSISTENTLY(clientConsensus->onAppendLogResponseCnt, 0, 500, 100);

  // this won't trigger timeout
  client->setSendPacketTimeout(3000);

  msg.set_msgid(101);
  buf.clear();
  msg.SerializeToString(&buf);
  EXPECT_EQ(client->sendPacket(addr, buf, msg.msgid()), 0);

  EXPECT_EQ_EVENTUALLY(serverConsensus->onAppendLogCnt, 2, 1000);
  EXPECT_EQ_EVENTUALLY(client->getNet()->getReciveCnt(), 2, 3000);
  EXPECT_EQ_EVENTUALLY(clientConsensus->onAppendLogResponseCnt, 1, 1000);

  EXPECT_EQ(Service::running, 0);

  EXPECT_EQ(server->shutdown(), 0);
  EXPECT_EQ(client->shutdown(), 0);

  delete server;
  delete client;

  delete serverConsensus;
  delete clientConsensus;
}

class MockConsensusAppendLogResendOnTimeout : public BaseMockConsensus {
 public:
  MockConsensusAppendLogResendOnTimeout() {
    onAppendLogCnt = 0;
    onAppendLogResponseCnt = 0;
    clientService_ = NULL;
  }
  void setClientService(Service* srv) { clientService_ = srv; }

  virtual ~MockConsensusAppendLogResendOnTimeout() {}
  virtual int onAppendLog(PaxosMsg* msg, PaxosMsg* rsp) {
    std::cout << "onAppendLog and sleep 2s" << std::endl;
    onAppendLogCnt++;
    sleep(2);
    rsp->set_clusterid(0);
    rsp->set_serverid(serverServerId);
    rsp->set_msgtype(Consensus::AppendLogResponce);
    rsp->set_msgid(msg->msgid());
    rsp->set_term(1);
    return 0;
  }
  virtual int onAppendLogResponce(PaxosMsg* msg) {
    std::cout << "onAppendLogResponce" << std::endl;
    onAppendLogResponseCnt++;
    return 0;
  }
  // this callback is invoked in the client-side, after a timeout msg is
  // received
  virtual int onAppendLogSendFail(PaxosMsg* msg, uint64_t* newId) {
    // set client timeout to 3s
    if (clientService_) {
      clientService_->setSendPacketTimeout(5000);
    }

    // inc msgid by 1
    *newId = msg->msgid() + 1;
    return 0;
  }

  int onAppendLogCnt;
  int onAppendLogResponseCnt;
  Service* clientService_;
};

TEST(Service, resend_on_timeout) {
  MockConsensusAppendLogResendOnTimeout* serverConsensus =
      new MockConsensusAppendLogResendOnTimeout();
  Service* server = new Service(serverConsensus);
  EXPECT_TRUE(server);

  EXPECT_EQ(server->init(), 0);
  EXPECT_EQ(server->start(11000), 0);

  MockConsensusAppendLogResendOnTimeout* clientConsensus =
      new MockConsensusAppendLogResendOnTimeout();
  Service* client = new Service(clientConsensus);
  EXPECT_TRUE(client);

  clientConsensus->setClientService(client);

  EXPECT_EQ(client->init(), 0);
  EXPECT_EQ(client->start(11001), 0);
  NetAddressPtr addr = client->createConnection("127.0.0.1:11000", NULL, 300UL);
  easy_addr_t easyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(addr)->getAddr();
  EXPECT_TRUE(easyAddr.port != 0);

  // this will trigger timeout
  client->setSendPacketTimeout(1000);
  // resend rather than reconn
  client->getNet()->setResendOnTimeout(true);

  PaxosMsg msg;
  msg.set_clusterid(0);
  msg.set_serverid(clientServerId);
  msg.set_msgtype(Consensus::AppendLog);
  msg.set_msgid(100);
  msg.set_term(1);
  std::string buf;
  msg.SerializeToString(&buf);
  EXPECT_EQ(client->sendPacket(addr, buf, msg.msgid()), 0);

  // server-side receives a msg,
  EXPECT_EQ_EVENTUALLY(serverConsensus->onAppendLogCnt, 1, timeoutMS);
  // then client-side receives a timeout msg,
  EXPECT_EQ_EVENTUALLY(client->getNet()->getReciveCnt(), 1, timeoutMS);
  EXPECT_EQ_CONSISTENTLY(clientConsensus->onAppendLogResponseCnt, 0, durationMS,
                         100);
  // and triggers a resend,
  EXPECT_EQ_EVENTUALLY(client->getResendCnt(), 1, timeoutMS);
  // which is processed by the server-side again,
  EXPECT_EQ_EVENTUALLY(serverConsensus->onAppendLogCnt, 2, timeoutMS);
  // and finally the client-side receives a response msg,
  EXPECT_EQ_EVENTUALLY(clientConsensus->onAppendLogResponseCnt, 1, 5000);

  EXPECT_EQ(Service::running, 0);

  EXPECT_EQ(server->shutdown(), 0);
  EXPECT_EQ(client->shutdown(), 0);

  delete server;
  delete client;

  delete serverConsensus;
  delete clientConsensus;
}

TEST_F(SingleNodeCluster, sendMsg) {
  MockConsensusRequestVote* mockConsensus = new MockConsensusRequestVote();
  auto srv = std::shared_ptr<Service>(new Service(mockConsensus));
  EXPECT_TRUE(static_cast<bool>(srv));
  srv->init();
  srv->start(11002);

  auto ptr = std::make_shared<RemoteServer>(2);
  ptr->matchIndex = 5;
  ptr->strAddr = std::string("127.0.0.1:11002");
  ptr->srv = node1->getService();
  ptr->paxos = node1;

  PaxosMsg msg;
  msg.set_clusterid(0);
  msg.set_msgid(0);
  msg.set_serverid(0);
  msg.set_msgtype(Paxos::RequestVote);
  msg.set_term(2);
  msg.set_leaderid(1);
  EXPECT_TRUE(msg.IsInitialized());
  msg.clear_msgid();

  ptr->connect(NULL);
  EXPECT_EQ_EVENTUALLY(ptr->connected.load(), true, 1000);

  ptr->sendMsg(&msg);
  EXPECT_EQ_EVENTUALLY(mockConsensus->onRequestVoteCnt, 1, 2000);
  // Remote server won't be deleted automatically with ptr.reset(),
  // since a shared_ptr of it may be held by the sendMsgQueue inside it.
  // and a explict stop is necessary.
  ptr->stop(NULL);
  ptr.reset();

  srv->shutdown();
  delete mockConsensus;
}
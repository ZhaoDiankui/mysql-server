// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

// unit test for alisql::EasyNet

#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <thread>

#include "easyNet.h"
#include "paxos_server.h"
#include "service.h"
#include "ut_helper.h"

using namespace alisql;

const uint64_t serverServerId = 1;
const uint64_t clientServerId = 2;

const int timeoutMS = 2000;
const int durationMS = 300;

class MockService : public Service {
 public:
  MockService(std::shared_ptr<EasyNet> en, bool mockWorkpoolRunning)
      : Service(NULL) {
    net_ = en;  // used to get EasyNet in kinds of callbacks
    shutdown_ = !mockWorkpoolRunning;
  }
};

class MockRemoteServer : public RemoteServer {
 public:
  MockRemoteServer() : RemoteServer(serverServerId), connectCnt(0) {}
  virtual void onConnectCb() {
    std::cout << "client-side onConnect" << std::endl;
    connectCnt++;
  }
  int connectCnt;
};

int replyMockPaxosMsgResponse(easy_request_t* r) {
  PaxosMsg* req = static_cast<PaxosMsg*>(r->args);
  PaxosMsg rsp;
  rsp.set_clusterid(0);
  rsp.set_msgid(req->msgid());
  rsp.set_serverid(serverServerId);
  rsp.set_msgtype(1);
  rsp.set_term(2);
  uint64_t len = rsp.ByteSizeLong();
  uint64_t extraLen = sizeof(NetPacket);
  NetPacket* out_np = (NetPacket*)easy_pool_alloc(r->ms->pool, extraLen + len);
  if (out_np == NULL) {
    return EASY_OK;
  }
  out_np->type = NetPacketTypeNet;
  out_np->len = len;
  out_np->data = &out_np->buffer[0];
  rsp.SerializeToArray(out_np->data, out_np->len);

  if (r->ipacket) EasyNet::tryFreeMsg(static_cast<NetPacket*>(r->ipacket));
  if (r->args) {
    delete (PaxosMsg*)r->args;
    r->args = 0;
  }
  r->opacket = (void*)out_np;
  return EASY_OK;
}

TEST(EasyNet, listen_and_connect) {
  std::shared_ptr<EasyNet> en(new EasyNet());

  Service* service = new MockService(en, false);
  EXPECT_TRUE(en->init(service) == 0);
  EXPECT_TRUE(en->start(11000) == 0);

  std::shared_ptr<NetServer> remoteServer(new MockRemoteServer());
  NetAddressPtr addr = en->createConnection(std::string("127.0.0.1:11000"),
                                            remoteServer, 1000ULL);
  EXPECT_EQ(addr->getType(), NetType::NET_TYPE_EASY);
  easy_addr_t easyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(addr)->getAddr();
  EXPECT_TRUE(easyAddr.port != 0);

  EXPECT_EQ_EVENTUALLY(((MockRemoteServer*)remoteServer.get())->connectCnt, 1,
                       timeoutMS);

  auto rs = en->getConnData(easyAddr);
  EXPECT_EQ(rs, remoteServer);

  EXPECT_TRUE(en->shutdown() == 0);
  delete service;
}

TEST(EasyNet, send_and_receive_correct_paxosmsg) {
  std::shared_ptr<EasyNet> server(new EasyNet());
  std::shared_ptr<EasyNet> client(new EasyNet());

  Service* serverService = new MockService(server, true);
  Service* clientService = new MockService(client, true);

  easy_io_t* pool_eio = easy_eio_create(NULL, 2);
  pool_eio->do_signal = 0;
  int requestsProcessed = 0;
  easy_thread_pool_t* serverWorkPool = easy_thread_pool_create(
      pool_eio, 4,
      [](easy_request_t* r, void* args) {
        PaxosMsg* req = static_cast<PaxosMsg*>(r->args);
        std::cout << "server work pool request process cb called msgid "
                  << req->msgid() << " from " << req->serverid() << std::endl;
        ++*((int*)args);
        return replyMockPaxosMsgResponse(r);
      },
      &requestsProcessed);
  int repliesProcessed = 0;
  easy_thread_pool_t* clientWorkPool = easy_thread_pool_create(
      pool_eio, 4,
      [](easy_request_t* r, void* args) {
        std::cout << "client work pool request process cb called" << std::endl;
        if (r->ipacket) {  // for timeout msg r->args is NULL
          ++*((int*)args);
        }
        return EASY_OK;
      },
      &repliesProcessed);
  server->setWorkPool(serverWorkPool);
  client->setWorkPool(clientWorkPool);
  EXPECT_TRUE(easy_eio_start(pool_eio) == 0);

  EXPECT_TRUE(server->init(serverService) == 0);
  EXPECT_TRUE(server->start(11000) == 0);

  EXPECT_TRUE(client->init(clientService) == 0);
  EXPECT_TRUE(client->start(11001) == 0);

  std::shared_ptr<NetServer> remoteServer(new MockRemoteServer());
  NetAddressPtr addr = client->createConnection(std::string("127.0.0.1:11000"),
                                                remoteServer, 1000ULL);
  EXPECT_EQ(addr->getType(), NetType::NET_TYPE_EASY);
  easy_addr_t easyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(addr)->getAddr();
  EXPECT_TRUE(easyAddr.port != 0);

  PaxosMsg msg;
  msg.set_clusterid(0);
  msg.set_serverid(clientServerId);
  msg.set_msgtype(1);
  msg.set_term(2);

  for (int i = 1; i < 3; i++) {
    msg.set_msgid(100 + i);
    std::string buf;
    msg.SerializeToString(&buf);
    EXPECT_EQ(client->sendPacket(addr, buf, msg.msgid()), 0);
    // ensure the server receives request and sends response
    EXPECT_EQ_EVENTUALLY(server->getReciveCnt(), (uint64_t)i, timeoutMS);
    EXPECT_EQ_EVENTUALLY(requestsProcessed, i, timeoutMS);
    EXPECT_EQ_EVENTUALLY(client->getReciveCnt(), (uint64_t)i, timeoutMS);
    EXPECT_EQ_EVENTUALLY(repliesProcessed, i, timeoutMS);
    // no reconn
    EXPECT_EQ_EVENTUALLY(((MockRemoteServer*)remoteServer.get())->connectCnt, 1,
                         timeoutMS);
  }

  EXPECT_TRUE(client->shutdown() == 0);
  EXPECT_TRUE(server->shutdown() == 0);

  delete serverService;
  delete clientService;
}

TEST(EasyNet, send_and_receive_server_timeout) {
  std::shared_ptr<EasyNet> server(new EasyNet());
  std::shared_ptr<EasyNet> client(new EasyNet());

  Service* serverService = new MockService(server, true);
  Service* clientService = new MockService(client, true);

  easy_io_t* pool_eio = easy_eio_create(NULL, 2);
  pool_eio->do_signal = 0;
  int requestsProcessed = 0;
  easy_thread_pool_t* serverWorkPool = easy_thread_pool_create(
      pool_eio, 4,
      [](easy_request_t* r, void* args) {
        std::cout << "server work pool request process cb called" << std::endl;
        ++*((int*)args);
        // 0.4s is enough to trigger a timeout event at client-side
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        return replyMockPaxosMsgResponse(r);
      },
      &requestsProcessed);
  int repliesProcessed = 0;
  easy_thread_pool_t* clientWorkPool = easy_thread_pool_create(
      pool_eio, 4,
      [](easy_request_t* r, void* args) {
        std::cout << "client work pool request process cb called" << std::endl;
        if (r->ipacket) {  // for timeout msg r->args is NULL
          ++*((int*)args);
        }
        return EASY_OK;
      },
      &repliesProcessed);
  server->setWorkPool(serverWorkPool);
  client->setWorkPool(clientWorkPool);
  EXPECT_TRUE(easy_eio_start(pool_eio) == 0);

  EXPECT_TRUE(server->init(serverService) == 0);
  EXPECT_TRUE(server->start(11000) == 0);

  EXPECT_TRUE(client->init(clientService) == 0);
  EXPECT_TRUE(client->start(11001) == 0);

  std::shared_ptr<NetServer> remoteServer(new MockRemoteServer());
  NetAddressPtr addr = client->createConnection(std::string("127.0.0.1:11000"),
                                                remoteServer, 1000ULL);
  EXPECT_EQ(addr->getType(), NetType::NET_TYPE_EASY);
  easy_addr_t easyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(addr)->getAddr();
  EXPECT_TRUE(easyAddr.port != 0);

  // waiting for the client to connect to the server
  EXPECT_EQ_EVENTUALLY(((MockRemoteServer*)remoteServer.get())->connectCnt, 1,
                       timeoutMS);

  PaxosMsg msg;
  msg.set_clusterid(0);
  msg.set_serverid(clientServerId);
  msg.set_msgtype(1);
  msg.set_term(2);
  std::string buf;

  msg.set_msgid(100);
  msg.SerializeToString(&buf);
  EXPECT_EQ(client->sendPacket(addr, buf, 100), 0);

  EXPECT_EQ_EVENTUALLY(server->getReciveCnt(), 1, timeoutMS);
  EXPECT_EQ_EVENTUALLY(requestsProcessed, 1, timeoutMS);
  // a timeout event is triggered after 300ms (default session timeout)
  EXPECT_EQ_EVENTUALLY(client->getReciveCnt(), 1, timeoutMS);
  EXPECT_EQ(client->getDecodeCnt(), 0);
  // wait for reconn
  EXPECT_EQ_EVENTUALLY(((MockRemoteServer*)remoteServer.get())->connectCnt, 2,
                       timeoutMS);
  // after timeout, the connection between client and server is closed, so the
  // reply is not received
  EXPECT_EQ_CONSISTENTLY(client->getDecodeCnt(), 0, durationMS, 100);

  EXPECT_TRUE(client->shutdown() == 0);
  EXPECT_TRUE(server->shutdown() == 0);

  delete serverService;
  delete clientService;
}

TEST(EasyNet, send_and_receive_invalid_reply) {
  std::shared_ptr<EasyNet> server(new EasyNet());
  std::shared_ptr<EasyNet> client(new EasyNet());

  Service* serverService = new MockService(server, true);
  Service* clientService = new MockService(client, true);

  easy_io_t* pool_eio = easy_eio_create(NULL, 2);
  pool_eio->do_signal = 0;
  int requestsProcessed = 0;
  easy_thread_pool_t* serverWorkPool = easy_thread_pool_create(
      pool_eio, 4,
      [](easy_request_t* r, void* args) {
        std::cout << "server work pool request process cb called" << std::endl;
        ++*((int*)args);
        std::string msg = "aaaaaaa";  // this is a bad reply
        uint64_t len = msg.length();
        uint64_t extraLen = sizeof(NetPacket);
        NetPacket* out_np =
            (NetPacket*)easy_pool_alloc(r->ms->pool, extraLen + len);
        if (out_np == NULL) {
          return EASY_OK;
        }
        out_np->type = NetPacketTypeNet;
        out_np->len = len;
        out_np->data = &out_np->buffer[0];
        memcpy(out_np->data, msg.data(), len);

        if (r->ipacket)
          EasyNet::tryFreeMsg(static_cast<NetPacket*>(r->ipacket));
        if (r->args) {
          delete (PaxosMsg*)r->args;
          r->args = 0;
        }
        r->opacket = (void*)out_np;
        return EASY_OK;
      },
      &requestsProcessed);
  int repliesProcessed = 0;
  easy_thread_pool_t* clientWorkPool = easy_thread_pool_create(
      pool_eio, 4,
      [](easy_request_t* r, void* args) {
        std::cout << "client work pool request process cb called" << std::endl;
        if (r->ipacket) {  // for timeout msg r->args is NULL
          ++*((int*)args);
        }
        return EASY_OK;
      },
      &repliesProcessed);
  server->setWorkPool(serverWorkPool);
  client->setWorkPool(clientWorkPool);
  EXPECT_TRUE(easy_eio_start(pool_eio) == 0);

  EXPECT_TRUE(server->init(serverService) == 0);
  EXPECT_TRUE(server->start(11000) == 0);

  EXPECT_TRUE(client->init(clientService) == 0);
  EXPECT_TRUE(client->start(11001) == 0);

  std::shared_ptr<NetServer> remoteServer(new MockRemoteServer());
  NetAddressPtr addr = client->createConnection(std::string("127.0.0.1:11000"),
                                                remoteServer, 1000ULL);
  EXPECT_EQ(addr->getType(), NetType::NET_TYPE_EASY);
  easy_addr_t easyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(addr)->getAddr();
  EXPECT_TRUE(easyAddr.port != 0);

  // waiting for the client to connect to the server
  EXPECT_EQ_EVENTUALLY(((MockRemoteServer*)remoteServer.get())->connectCnt, 1,
                       timeoutMS);

  PaxosMsg msg;
  msg.set_clusterid(0);
  msg.set_serverid(clientServerId);
  msg.set_msgtype(1);
  msg.set_term(2);
  std::string buf;

  msg.set_msgid(100);
  msg.SerializeToString(&buf);
  EXPECT_EQ(client->sendPacket(addr, buf, 100), 0);

  EXPECT_EQ_EVENTUALLY(server->getDecodeCnt(), 1, timeoutMS);
  EXPECT_EQ_EVENTUALLY(server->getReciveCnt(), 1, timeoutMS);
  EXPECT_EQ_EVENTUALLY(requestsProcessed, 1, timeoutMS);

  EXPECT_EQ_EVENTUALLY(client->getDecodeCnt(), 1, timeoutMS);
  // a decode error is detected
  EXPECT_EQ_EVENTUALLY(client->getDecodeErrCnt(), 1, timeoutMS);
  // a timeout event is triggered after 300ms (default session timeout)
  EXPECT_EQ_EVENTUALLY(client->getReciveCnt(), 1, timeoutMS);
  EXPECT_EQ_CONSISTENTLY(repliesProcessed, 0, durationMS, 100);

  EXPECT_EQ_EVENTUALLY(((MockRemoteServer*)remoteServer.get())->connectCnt, 2,
                       timeoutMS);

  // after timeout, the connection between client and server is closed, so the
  // reply is not received
  EXPECT_EQ_EVENTUALLY(repliesProcessed, 0, timeoutMS);

  EXPECT_TRUE(client->shutdown() == 0);
  EXPECT_TRUE(server->shutdown() == 0);

  delete serverService;
  delete clientService;
}

TEST(EasyNet, send_and_receive_nonrunning_workpool) {
  std::shared_ptr<EasyNet> server(new EasyNet());
  std::shared_ptr<EasyNet> client(new EasyNet());

  Service* serverService =
      new MockService(server, false);  // server-side work pool is not running
  Service* clientService = new MockService(client, true);

  easy_io_t* pool_eio = easy_eio_create(NULL, 2);
  pool_eio->do_signal = 0;
  int repliesProcessed = 0;
  easy_thread_pool_t* clientWorkPool = easy_thread_pool_create(
      pool_eio, 4,
      [](easy_request_t* r, void* args) {
        std::cout << "client work pool request process cb called" << std::endl;
        if (r->ipacket) {  // for timeout msg r->args is NULL
          ++*((int*)args);
        }
        return EASY_OK;
      },
      &repliesProcessed);
  client->setWorkPool(clientWorkPool);
  EXPECT_TRUE(easy_eio_start(pool_eio) == 0);

  EXPECT_TRUE(server->init(serverService) == 0);
  EXPECT_TRUE(server->start(11000) == 0);

  EXPECT_TRUE(client->init(clientService) == 0);
  EXPECT_TRUE(client->start(11001) == 0);

  std::shared_ptr<NetServer> remoteServer(new MockRemoteServer());
  NetAddressPtr addr = client->createConnection(std::string("127.0.0.1:11000"),
                                                remoteServer, 1000ULL);
  EXPECT_EQ(addr->getType(), NetType::NET_TYPE_EASY);
  easy_addr_t easyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(addr)->getAddr();
  EXPECT_TRUE(easyAddr.port != 0);

  // waiting for the client to connect to the server
  EXPECT_EQ_EVENTUALLY(((MockRemoteServer*)remoteServer.get())->connectCnt, 1,
                       timeoutMS);

  PaxosMsg msg;
  msg.set_clusterid(0);
  msg.set_serverid(clientServerId);
  msg.set_msgtype(1);
  msg.set_msgid(100);
  msg.set_term(2);

  std::string buf;
  msg.SerializeToString(&buf);
  EXPECT_EQ(client->sendPacket(addr, buf, 100), 0);
  EXPECT_EQ_EVENTUALLY(server->getReciveCnt(), 1, timeoutMS);
  // server-side reciveProcess return ERROR since workpool is not running, which
  // triggers a timeout event immediately in client-side
  EXPECT_EQ_EVENTUALLY(client->getReciveCnt(), 1, timeoutMS);
  EXPECT_EQ(client->getDecodeCnt(), 0);
  EXPECT_EQ_CONSISTENTLY(repliesProcessed, 0, durationMS, 100);

  // wait for autoreconn (after 1s) completes.
  EXPECT_EQ_EVENTUALLY(((MockRemoteServer*)remoteServer.get())->connectCnt, 2,
                       timeoutMS);

  EXPECT_TRUE(client->shutdown() == 0);
  EXPECT_TRUE(server->shutdown() == 0);

  delete serverService;
  delete clientService;
}

TEST(EasyNet, send_and_receive_invalid_request) {
  std::shared_ptr<EasyNet> server(new EasyNet());
  std::shared_ptr<EasyNet> client(new EasyNet());

  Service* serverService = new MockService(server, true);
  Service* clientService = new MockService(client, true);

  easy_io_t* pool_eio = easy_eio_create(NULL, 2);
  pool_eio->do_signal = 0;
  int requestsProcessed = 0;
  easy_thread_pool_t* serverWorkPool = easy_thread_pool_create(
      pool_eio, 4,
      [](easy_request_t* r, void* args) {
        std::cout << "server work pool request process cb called" << std::endl;
        ++*((int*)args);
        return EASY_OK;
      },
      &requestsProcessed);
  int repliesProcessed = 0;
  easy_thread_pool_t* clientWorkPool = easy_thread_pool_create(
      pool_eio, 4,
      [](easy_request_t* r, void* args) {
        std::cout << "client work pool request process cb called" << std::endl;
        if (r->ipacket) {  // for timeout msg r->args is NULL
          ++*((int*)args);
        }
        return EASY_OK;
      },
      &repliesProcessed);
  server->setWorkPool(serverWorkPool);
  client->setWorkPool(clientWorkPool);
  EXPECT_TRUE(easy_eio_start(pool_eio) == 0);

  EXPECT_TRUE(server->init(serverService) == 0);
  EXPECT_TRUE(server->start(11000) == 0);

  EXPECT_TRUE(client->init(clientService) == 0);
  EXPECT_TRUE(client->start(11001) == 0);

  std::shared_ptr<NetServer> remoteServer(new MockRemoteServer());
  NetAddressPtr addr = client->createConnection(std::string("127.0.0.1:11000"),
                                                remoteServer, 1000ULL);
  EXPECT_EQ(addr->getType(), NetType::NET_TYPE_EASY);
  easy_addr_t easyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(addr)->getAddr();
  EXPECT_TRUE(easyAddr.port != 0);

  EXPECT_EQ_EVENTUALLY(((MockRemoteServer*)remoteServer.get())->connectCnt, 1,
                       1000);

  const std::string buf = "aaaaaaaaa";
  EXPECT_EQ(client->sendPacket(addr, buf), 0);
  // above packet can not be parsed as a valid PaxosMsg at the server side, and
  // no reply is sent, which triggers the client-side timeout event after 300ms
  // (session timeout)
  EXPECT_EQ_EVENTUALLY(server->getDecodeCnt(), 1, timeoutMS);
  EXPECT_EQ_EVENTUALLY(server->getDecodeErrCnt(), 1, timeoutMS);
  EXPECT_EQ(requestsProcessed, 0);

  EXPECT_EQ_EVENTUALLY(client->getReciveCnt(), 1,
                       timeoutMS);  // timeout event
  EXPECT_EQ(client->getDecodeCnt(), 0);

  // TODO fixme if client keeps sending packets, the reconn is always put off by
  // the timeout event
  for (int i = 1; i <= 3; i++) {
    EXPECT_EQ(client->sendPacket(addr, buf), 0);
    // reconn hasn't completed yet, so the packet is not sent out, and another
    // timeout event is received
    EXPECT_EQ_EVENTUALLY(((MockRemoteServer*)remoteServer.get())->connectCnt, 1,
                         timeoutMS);
    EXPECT_EQ_EVENTUALLY(server->getDecodeCnt(), 1, timeoutMS);

    EXPECT_EQ_EVENTUALLY(client->getReciveCnt(), i + 1UL,
                         timeoutMS);  // timeout events
    EXPECT_EQ(client->getDecodeCnt(), 0);

    // wait for less 1s (before reconn completes) to send the next packet
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  // wait for autoreconn (after 1s) completes.
  EXPECT_EQ_EVENTUALLY(((MockRemoteServer*)remoteServer.get())->connectCnt, 2,
                       timeoutMS);
  // reconnect completed, the packet is sent out
  EXPECT_EQ(client->sendPacket(addr, buf), 0);
  EXPECT_EQ_EVENTUALLY(server->getDecodeCnt(), 2, timeoutMS);
  EXPECT_EQ_EVENTUALLY(server->getDecodeErrCnt(), 2, timeoutMS);
  EXPECT_EQ_EVENTUALLY(client->getReciveCnt(), 5,
                       timeoutMS);  // timeout events
  EXPECT_EQ(client->getDecodeCnt(), 0);

  EXPECT_TRUE(client->shutdown() == 0);
  EXPECT_TRUE(server->shutdown() == 0);

  delete serverService;
  delete clientService;
}

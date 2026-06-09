// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include "easyNet.h"

#include "paxos.h"  //TODO no driect include
#include "paxos.pb.h"
#include "paxos_server.h"  //TODO no driect include
#include "service.h"

namespace alisql {

easy_atomic_t easy_pool_alloc_byte = 0;

static void* easy_count_realloc(void* ptr, size_t size) {
  char *p1, *p = (char*)ptr;
  if (p) {
    p -= 8;
    easy_atomic_add(&easy_pool_alloc_byte, -(*((int*)p)));
  }
  if (size) {
    easy_atomic_add(&easy_pool_alloc_byte, size);
    p1 = (char*)easy_realloc(p, size + 8);
    if (p1) {
      *((int*)p1) = size;
      p1 += 8;
    }
    return p1;
  } else if (p)
    easy_free(p);
  return NULL;
}

EasyNet::EasyNet(uint64_t num, const uint64_t sessionTimeout,
                 bool memory_usage_count)
    : eio_(NULL),
      workPool_(NULL),
      reciveCnt_(0),
      encodeCnt_(0),
      decodeCnt_(0),
      decodeErrCnt_(0),
      isShutdown_(false),
      sessionTimeout_(sessionTimeout),
      resendOnTimeout_(false),
      port_(-1) {
  if (unlikely(memory_usage_count)) {
    // set allocator for memory usage count
    easy_pool_set_allocator(easy_count_realloc);
  }
  eio_ = easy_eio_create(NULL, num);
  eio_->do_signal = 0;
}

int EasyNet::init(void* ptr) {
  memset(&clientHandler_, 0, sizeof(clientHandler_));
  clientHandler_.decode = EasyNet::paxosDecode;
  clientHandler_.encode = EasyNet::paxosEncode;
  clientHandler_.process = EasyNet::reciveProcess;
  clientHandler_.on_connect = EasyNet::onConnected;
  clientHandler_.on_disconnect = EasyNet::onDisconnected;
  clientHandler_.on_close = EasyNet::onClosed;
  clientHandler_.cleanup = EasyNet::onClientCleanup;
  clientHandler_.get_packet_id = EasyNet::getPacketId;
  clientHandler_.user_data = (void*)workPool_;
  clientHandler_.user_data2 = ptr;

  memset(&serverHandler_, 0, sizeof(serverHandler_));
  serverHandler_.decode = EasyNet::paxosDecode;
  serverHandler_.encode = EasyNet::paxosEncode;
  serverHandler_.process = EasyNet::reciveProcess;
  serverHandler_.user_data = (void*)workPool_;
  serverHandler_.user_data2 = ptr;

  return 0;
};

int EasyNet::start(int port) {
  if (eio_ == NULL) return -3;
  if (easy_connection_add_listen(eio_, NULL, port, &serverHandler_) == NULL)
    return -1;

  if (easy_eio_start(eio_)) return -2;

  port_ = port;

  serverPort_ = port;
  return 0;
}

int EasyNet::shutdown() {
  lock_.lock();
  isShutdown_ = true;
  lock_.unlock();
  /*
   * The follow function should not be called by hold the lock.
   * Because some callback function which try to hold lock, will be called.
   */
  easy_eio_shutdown(eio_);
  easy_eio_stop(eio_);
  easy_eio_wait(eio_);
  easy_eio_destroy(eio_);

  return 0;
}

int EasyNet::stop() {
  easy_eio_stop(eio_);

  return 0;
}

NetAddressPtr EasyNet::recreateConnectionIfNecessary(const std::string& addr,
                                                     NetAddressPtr oldAddr,
                                                     NetServerRef server,
                                                     uint64_t timeout) {
  if (isShutdown_) {
    easy_error_log("failed to reconnect %s, because the easyNet is shut down.",
                   addr.c_str());
    return oldAddr;
  }

  easy_addr_t oldEasyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(oldAddr)->getAddr();
  easy_addr_t easyAddr;

  easyAddr = easy_inet_str_to_addr(addr.c_str(), 0);
  if (easyAddr.port == 0) {
    easy_error_log("failed to reconnect %s, because IP address is invalid.",
                   addr.c_str());
    return oldAddr;
  }
  easyAddr.cidx = oldEasyAddr.cidx;

  if (memcmp(&easyAddr, &oldEasyAddr, sizeof(easy_addr_t)) != 0) {
    char buf[32];

    easy_warn_log(
        "disconnect server %s because IP address %s has changed\n",
        addr.c_str(), easy_inet_addr_to_str(&oldEasyAddr, buf, 32));
    easy_connection_disconnect(eio_, oldEasyAddr);

    if (getConnData(easyAddr, false) != nullptr) {
      easy_error_log(
          "failed to reconnect %s/%s, because the old connection is not "
          "destroyed.",
          addr.c_str(), easy_inet_addr_to_str(&easyAddr, buf, 32));
      return oldAddr;
    }

    if (server != nullptr) {
      setConnData(easyAddr, server);
    }

    easy_warn_log(
        "reconnect server %s after IP address changed to %s\n",
        addr.c_str(), easy_inet_addr_to_str(&easyAddr, buf, 32));
    int ret = easy_connection_connect(eio_, easyAddr, &clientHandler_, timeout,
                                      NULL, EASY_CONNECT_AUTOCONN);
    if (ret != EASY_OK) {
      easy_error_log(
          "failed to reconnect %s by invoking easy_connection_connect, error "
          "code: %d",
          addr.c_str(), ret);
      delConnData(easyAddr);
      easyAddr.port = 0;
      return nullptr;
    }
  }

  return std::make_shared<EasyNetAddress>(easyAddr);
}

NetAddressPtr EasyNet::createConnection(const std::string& addr,
                                        NetServerRef server, uint64_t timeout,
                                        uint64_t index) {
  easy_addr_t easyAddr;

  if (isShutdown_) {
    easy_error_log("failed to reconnect %s, because the easyNet is shut down.",
                   addr.c_str());
    easyAddr.port = 0;
    return nullptr;
  }

  easyAddr = easy_inet_str_to_addr(addr.c_str(), 0);
  easyAddr.cidx = index;

  if (getConnData(easyAddr, false) != nullptr) {
    char buffer[32];
    easy_error_log(
        "failed to reconnect %s/%s, because the old connection is not "
        "destroyed.",
        addr.c_str(), easy_inet_addr_to_str(&easyAddr, buffer, 32));
    return nullptr;
  }

  if (server != nullptr) setConnData(easyAddr, server);

  int ret = easy_connection_connect(eio_, easyAddr, &clientHandler_, timeout,
                                    NULL, EASY_CONNECT_AUTOCONN);
  if (ret != EASY_OK)
  // if (easy_connection_connect(eio_, easyAddr, &clientHandler_, timeout, NULL,
  // 0) != EASY_OK)
  {
    char buffer[32];
    easy_error_log(
        "failed to reconnect %s/%s by invoking easy_connection_connect, error "
        "code: %d",
        addr.c_str(), easy_inet_addr_to_str(&easyAddr, buffer, 32), ret);
    delConnData(easyAddr);
    easyAddr.port = 0;
    return nullptr;
  }

  /*
  easy_session_t *s= easy_connection_connect_init(NULL, &clientHandler_, 200,
  NULL, 0, NULL);

  if (iaddr.family == 0 || s == NULL)
    iaddr.port= 0;

  if (NULL == easy_client_send(eio_, iaddr, s))
    iaddr.port= 0;
  */

  return std::make_shared<EasyNetAddress>(easyAddr);
}

void EasyNet::disableConnection(NetAddressPtr addr) {
  easy_addr_t easyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(addr)->getAddr();
  easy_connection_disconnect(eio_, easyAddr);
}

int EasyNet::sendPacket(NetAddressPtr addr, const char* buf, uint64_t len,
                        uint64_t id) {
  easy_session_t* s;
  NetPacket* np;

  if ((np = easy_session_packet_create(NetPacket, s, len)) == NULL) {
    return -1;
  }

  easy_addr_t easyAddr =
      std::dynamic_pointer_cast<EasyNetAddress>(addr)->getAddr();
  auto server = std::dynamic_pointer_cast<RemoteServer>(getConnData(easyAddr));
  if (!server || !server->isLearner || !server->paxos ||
      server->paxos->getLocalServer()->learnerConnTimeout == 0)
    easy_session_set_timeout(s, sessionTimeout_);  // ms
  else
    easy_session_set_timeout(
        s, server->paxos->getLocalServer()->learnerConnTimeout * 4);

  np->data = &np->buffer[0];
  np->len = len;
  np->packetId = id;
  memcpy(np->data, buf, len);
  if (easy_client_dispatch(eio_, easyAddr, s) == EASY_ERROR) {
    easy_session_destroy(s);
    return -2;
  }

  /* TODO: onFail callback function need to be added*/

  return 0;
}

int EasyNet::sendPacket(NetAddressPtr addr, const std::string& buf,
                        uint64_t id) {
  return sendPacket(addr, buf.c_str(), buf.length(), id);
}

int EasyNet::resendPacket(NetAddressPtr addr, void* ptr, uint64_t id) {
  NetPacket* np = (NetPacket*)ptr;

  return sendPacket(addr, np->data, np->len, id);
}

int EasyNet::setRecvPacketCallback(void* handler) {
  setWorkPool((easy_thread_pool_t*)handler);
  return 0;
}

NetServerRef EasyNet::getConnData(easy_addr_t addr, bool locked) {
  std::unique_lock<std::mutex> lg(lock_, std::defer_lock);
  if (!locked) lg.lock();

  auto it = connStatus_.find(getAddrKey(addr));
  if (it != connStatus_.end())
    return it->second;
  else
    return nullptr;
}

NetServerRef EasyNet::getConnDataAndSetFail(easy_connection_t* c, bool isFail) {
  auto addr = c->addr;
  std::lock_guard<std::mutex> lg(lock_);
  auto s = getConnData(addr, true);
  auto server = std::dynamic_pointer_cast<RemoteServer>(s);
  if (server == nullptr) {
    easy_error_log(
        "EasyNet::getConnDataAndSetFail the server %s for connection %s is "
        "nullptr",
        isFail ? "onDisconnected" : "onConnected", easy_connection_str(c));
    return s;
  }
  if (isFail) {
    if (!server->netError.load()) {
      if (server->c == c || server->c == nullptr)
        easy_log("EasyNet::onDisconnected server %ld\n", server->serverId);
      else
        easy_log("EasyNet::onDisconnected server %ld, which is the old one\n",
                 server->serverId);
    }
    /* disconnect */
    if (server->c != nullptr && server->c != c) return s;
    server->waitForReply = 0;
    server->netError.store(isFail);
  } else {
    if (server->netError.load())
      easy_log("EasyNet::onConnected server %ld to port %d\n", server->serverId,
               serverPort_);
    /* connect */
    server->c = c;
    server->waitForReply = 0;
    server->netError.store(isFail);
  }
  return s;
}

void EasyNet::setConnData(easy_addr_t addr, NetServerRef server) {
  std::lock_guard<std::mutex> lg(lock_);
  connStatus_.insert(std::make_pair(getAddrKey(addr), server));
}

void EasyNet::delConnData(easy_addr_t addr) {
  std::lock_guard<std::mutex> lg(lock_);
  connStatus_.erase(getAddrKey(addr));
}

void EasyNet::delConnDataById(uint64_t id) {
  /* Protect the connStatus_ map */
  std::lock_guard<std::mutex> lg(lock_);
  for (auto it = connStatus_.begin(); it != connStatus_.end();) {
    if (std::dynamic_pointer_cast<RemoteServer>(it->second)->serverId == id)
      connStatus_.erase(it++);
    else
      ++it;
    if (connStatus_.size() == 0) break;
  }
}

uint64_t EasyNet::getAddrKey(easy_addr_t addr) {
  uint64_t ret = addr.u.addr;
  ret <<= 32;
  ret |= addr.port;

  return ret;
}

void EasyNet::tryFreeMsg(NetPacket* np) {
  if (np->msg) {
    delete static_cast<PaxosMsg*>(np->msg);
    np->msg = nullptr;
  }
}

int EasyNet::reciveProcess(easy_request_t* r) {
  if (r == NULL) return EASY_ERROR;

  /* TODO means send callback process here? called by easy_session_process */
  if (r->ms->c == NULL) return EASY_ERROR;

  auto tp = (easy_thread_pool_t*)(r->ms->c->handler->user_data);
  auto srv = (Service*)(r->ms->c->handler->user_data2);
  if (srv == NULL) return EASY_ERROR;

  assert(r->user_data == NULL);
  r->user_data = (void*)srv;
  auto en = srv->getNet();

  en->incRecived();
  if (r->ipacket) {
    NetPacket* np = (NetPacket*)(r->ipacket);
    easy_debug_log(
        "port:%d received packet length=%d, msg=%s tp=%x "
        "workpoolIsRunning=%d\n",
        en->getPort(), np->len, np->data, tp, srv->workPoolIsRunning());
  } else {
    easy_debug_log("port:%d received timeout or empty packet\n", en->getPort());
  }

  if (tp == NULL || !srv->workPoolIsRunning()) return EASY_ERROR;

  int ret = EASY_AGAIN;

  if (r->ipacket == NULL) {
    /*
     * timeout case.
     * we push the session to thread pool.
     * Service::process will handle this case.
     */
    auto easyNet = std::dynamic_pointer_cast<EasyNet>(en);
    auto server = std::dynamic_pointer_cast<RemoteServer>(
        easyNet->getConnData(r->ms->c->addr));
    /* May already be remove. */
    if (server != nullptr) {
      // tested by case sendMsg_timeout_without_optimistic_heartbeat
      NetPacket* np = (NetPacket*)(r->opacket);
      easy_warn_log(
          "EasyNet::reciveProcess port:%d serverid:%ld sendMsg timeout or "
          "receive empty reply packet_id:0x%llx(%llu)\n",
          en->getPort(), server->serverId, np->packetId, np->packetId);
    }
    // server->waitForReply= 0;

    if (easyNet->resendOnTimeout_) {
      // resend in workPool_ thread asynchronously
      ret = EASY_AGAIN;
    } else {
      // easy will then close current connection immediately and schedule
      // reconnecting
      ret = EASY_ERROR;
    }
  }

  if (r->ms->c->type == EASY_TYPE_SERVER) {
    auto tp2 = srv->getHeartbeatPool();
    NetPacket* np = (NetPacket*)r->ipacket;
    assert(np);
    PaxosMsg* msg = new PaxosMsg;
    if (msg == NULL) {
      r->opacket = (void*)NULL;
      // TODO why retcode is EASY_OK
      return EASY_OK;
    }

    if (!msg->ParseFromArray(np->data, np->len)) {
      // receive a wrong msg.
      easy_error_log("port %d A msg have %ld entries!! droped!!\n",
                     en->getPort(), msg->entries_size());
      en->incDecodeErr();
      r->opacket = (void*)NULL;
      delete msg;
      // TODO why retcode is EASY_OK
      return EASY_OK;
    }
    assert(r->args == 0);
    r->args = (void*)msg;
    if (tp2 == NULL || msg->msgtype() != Consensus::AppendLog ||
        msg->entries_size() != 0 || Service::running < Service::workThreadCnt ||
        !((Paxos*)(srv->getConsensus()))->getOptimisticHeartbeat()) {
      easy_thread_pool_push(tp, r, easy_hash_key((uint64_t)(long)r));
    } else {
      // set msg type to OptimisticHeartbeat to let service know it is in
      // heartbeat pool
      msg->set_msgtype(Consensus::OptimisticHeartbeat);
      // msg is heartbeat and work pool is full, put in heartbeat pool
      easy_thread_pool_push(tp2, r, easy_hash_key((uint64_t)(long)r));
    }
    // here the retcode is EASY_AGAIN, because the task is pushed to the thread
    // pool
  } else {
    easy_session_t* s = (easy_session_t*)r->ms;
    easy_thread_pool_push_session(tp, s, easy_hash_key((uint64_t)(long)r));
    // here the retcode is EASY_AGAIN, because the task is pushed to the thread
    // pool
  }

  return ret;
}

void* EasyNet::paxosDecode(easy_message_t* m) {
  NetPacket* np;
  uint64_t len, datalen;

  auto srv = (Service*)(m->c->handler->user_data2);
  if (srv != NULL) {
    auto en = srv->getNet();
    en->incDecoded();
  }

  if ((len = m->input->last - m->input->pos) < NetPacketHeaderSize) return NULL;

  datalen = *((uint64_t*)m->input->pos);

  if (datalen > 0x4000000) {
    easy_error_log("data_len is invalid: %llu\n", datalen);
    m->status = EASY_ERROR;
    return NULL;
  }

  len -= NetPacketHeaderSize;

  if (len < datalen) {
    m->next_read_len = datalen - len;
    easy_debug_log("Decode a net packet fail, data len expect:%llu got:%llu",
                   datalen, len);
    return NULL;
  }

  if ((np = (NetPacket*)easy_pool_calloc(m->pool, sizeof(NetPacket))) == NULL) {
    m->status = EASY_ERROR;
    return NULL;
  }

  m->input->pos += NetPacketHeaderSize;
  np->type = NetPacketTypeNet;
  np->data = (char*)m->input->pos;
  np->len = datalen;
  if (m->c->type == EASY_TYPE_CLIENT) {
    auto msg = new PaxosMsg;
    if (msg == NULL) {
      m->status = EASY_ERROR;
      return NULL;
    }

    // check the validity of received msg.
    if (!msg->ParseFromArray(np->data, np->len)) {
      easy_error_log("Decode error, ParseFromArray failed, msg len: %d",
                     np->len);
      if (srv != NULL) {
        auto en = srv->getNet();
        en->incDecodeErr();
      }

      delete msg;
      m->status = EASY_ERROR;
      return NULL;
    }
    np->packetId = msg->msgid();
    np->msg = (void*)msg;
  }
  m->input->pos += datalen;

  easy_debug_log("Decode a net packet success, total lens:%llu",
                 datalen + NetPacketHeaderSize);

  return (void*)np;
}

int EasyNet::paxosEncode(easy_request_t* r, void* data) {
  easy_buf_t* b;
  NetPacket* np = (NetPacket*)data;

  auto srv = (Service*)(r->ms->c->handler->user_data2);
  if (srv != NULL) {
    auto en = srv->getNet();
    en->incEncoded();
  }

  if ((b = easy_buf_create(r->ms->pool, NetPacketHeaderSize + np->len)) == NULL)
    return EASY_ERROR;

  *((uint64_t*)b->last) = np->len;
  memcpy(b->last + NetPacketHeaderSize, np->data, np->len);

  b->last += (NetPacketHeaderSize + np->len);

  easy_request_addbuf(r, b);

  return EASY_OK;
}

int EasyNet::onConnected(easy_connection_t* c) {
  auto srv = (Service*)(c->handler->user_data2);
  if (srv == NULL) return 0;

  std::shared_ptr<Net> net = srv->getNet();
  std::shared_ptr<EasyNet> easyNet = std::dynamic_pointer_cast<EasyNet>(net);

  auto server = std::dynamic_pointer_cast<RemoteServer>(
      easyNet->getConnDataAndSetFail(c, false));
  if (server != nullptr) {
    /* We don't start heartbeatTimer for learner, so we send heartbeat here. */
    server->onConnectCb();
  }
  return 0;
}

int EasyNet::onDisconnected(easy_connection_t* c) {
  auto srv = (Service*)(c->handler->user_data2);
  if (srv == NULL) return 0;

  std::shared_ptr<Net> net = srv->getNet();
  std::shared_ptr<EasyNet> easyNet = std::dynamic_pointer_cast<EasyNet>(net);

  auto server = std::dynamic_pointer_cast<RemoteServer>(
      easyNet->getConnDataAndSetFail(c, true));

  if (server != nullptr && server->c == c) {
    server->onDisconnectCb();
  } else {
    // Do nothing.

    // If 'c' changes, it means that the current server's connection is not the
    // connection we previously attempted to disconnect. This situation may
    // occur when the address is allocated to another RemoteServer, for example
    // when a follower is downgraded to a learner.
  }
  return 0;
}

int EasyNet::onClosed(easy_connection_t* c) {
  auto srv = (Service*)(c->handler->user_data2);
  auto addr = c->addr;

  if (srv == NULL) return 0;

  std::shared_ptr<Net> net = srv->getNet();
  std::shared_ptr<EasyNet> easyNet = std::dynamic_pointer_cast<EasyNet>(net);

  if (srv != NULL) easyNet->delConnData(addr);

  return 0;
}

int EasyNet::onClientCleanup(easy_request_t* r, void* apacket) {
  NetPacket* np = nullptr;

  // for timeouted reponse, easy_session_t and easy_request_t are already
  // deleted.
  if (r == NULL) {
    np = static_cast<NetPacket*>(apacket);
    if (np && np->msg) {
      PaxosMsg* msg = static_cast<PaxosMsg*>(np->msg);
      easy_log(
          "EasyNet::onClientCleanup: msgId(%llu) packet_id:%llu responce from "
          "server %llu which already be deleted for timeout.",
          msg->msgid(), np->packetId, msg->serverid());
      delete msg;
      np->msg = NULL;
    }
    return 0;
  }

  // invoked in easy_session_destroy, this cleanup handler is registered only at
  // the client-side, deserialized PaxosMsg should be stored in r->ipacket->msg
  // and released in upper layer Service::process code here is just a safe
  // guard, it should not be actually executed.
  np = static_cast<NetPacket*>(r->ipacket);
  if (np && np->msg) {
    PaxosMsg* msg = static_cast<PaxosMsg*>(np->msg);
    easy_log(
        "EasyNet::onClientCleanup: msgId(%llu) packet_id:%llu responce from "
        "server %llu.",
        msg->msgid(), np->packetId, msg->serverid());
    delete msg;
    np->msg = NULL;
  }
  return 0;
}

uint64_t EasyNet::getPacketId(easy_connection_t* c, void* data) {
  NetPacket* np = static_cast<NetPacket*>(data);
  return np->packetId;
}

}  // namespace alisql

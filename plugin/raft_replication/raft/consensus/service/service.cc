// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include "service.h"

#include <stdlib.h>

#include "consensus.h"
#include "io/easy_baseth_pool.h"
#include "msg_compress.h"
#include "util/easy_inet.h"
#include "utils.h"

namespace alisql {

Service::Service(Consensus* cons)
    : cs(NULL),
      pool_eio_(NULL),
      shutdown_(true),
      workPool_(NULL),
      heartbeatPool_(NULL),
      cons_(cons),
      resendCnt_(0),
      asyncEventCnt_(0) {}

std::atomic<uint64_t> Service::running(0);
uint64_t Service::workThreadCnt = 0;
WorkerStartCallback Service::workerStartCb_ = NULL;
WorkerEndCallback Service::workerEndCb_ = NULL;

static void* worker_thread_on_start_ex(void* args) {
  if (Service::workerStartCb_) Service::workerStartCb_();
  (void)easy_baseth_on_start(args);
  if (Service::workerEndCb_) Service::workerEndCb_();
  return NULL;
}

int Service::init(uint64_t ioThreadCnt, uint64_t workThreadCntArg,
                  uint64_t ConnectTimeout, bool memory_usage_count,
                  uint64_t heartbeatThreadCnt, NetType netType) {
  /* TODO here we should use factory. */
  // for now, we only support easyNet
  if (netType == NetType::NET_TYPE_EASY) {
    net_ = std::shared_ptr<EasyNet>(
        new EasyNet(ioThreadCnt, ConnectTimeout, memory_usage_count));
  } else {
    easy_error_log("Unknown net type when initializing service: %d", netType);
    return -1;
  }

  pool_eio_ = easy_eio_create(NULL, 1);
  if (pool_eio_ == NULL) {
    return -1;
  }
  pool_eio_->do_signal = 0;

  workPool_ =
      easy_thread_pool_create_ex(pool_eio_, workThreadCntArg,
                                 worker_thread_on_start_ex, Service::process, NULL);
  if (workPool_ == NULL) {
    return -1;
  }
  workThreadCnt = workThreadCntArg;

  if (heartbeatThreadCnt) {
    heartbeatPool_ = easy_thread_pool_create_ex(pool_eio_, heartbeatThreadCnt,
                                                worker_thread_on_start_ex,
                                                Service::process, NULL);
    if (heartbeatPool_ == NULL) {
      return -1;
    }
  } else {
    heartbeatPool_ = NULL;
  }

  tts_ = std::make_shared<ThreadTimerService>();

  net_->setWorkPool(workPool_);
  if (net_->init(this)) {
    return -1;
  }

  return 0;
}

int Service::start(int port) {
  if (easy_eio_start(pool_eio_)) return -4;
  if (tts_->start()) return -1;
  if (net_->start(port)) return -1;
  shutdown_ = false;
  return 0;
}

int Service::shutdown() {
  if (shutdown_) return 0;
  net_->stop();
  tts_->stop();
  easy_eio_shutdown(pool_eio_);
  easy_eio_stop(pool_eio_);
  easy_eio_wait(pool_eio_);
  net_->shutdown();
  easy_eio_destroy(pool_eio_);
  shutdown_ = true;
  return 0;
}

int Service::stop() {
  easy_eio_stop(pool_eio_);
  net_->stop();
  tts_->stop();
  return 0;
}

void Service::setSendPacketTimeout(uint64_t t) {
  /* will apply to all sendPacket */
  net_->setSessionTimeout(t);
}

int Service::sendPacket(NetAddressPtr addr, const std::string& buf,
                        uint64_t id) {
  return net_->sendPacket(addr, buf, id);
}

int Service::resendPacket(NetAddressPtr addr, void* ptr, uint64_t id) {
  return net_->resendPacket(addr, ptr, id);
}

int Service::onAsyncEvent(Callback* cb) {
  if (cb != NULL) cb->run();
  return 0;
}

int Service::pushAsyncEvent(Callback* cb) {
  easy_session_t* s;
  NetPacket* np;
  ServiceEvent* se;

  uint64_t len = sizeof(ServiceEvent);
  if ((np = easy_session_packet_create(NetPacket, s, len)) == NULL) {
    return -1;
  }

  np->type = NetPacketTypeAsync;
  np->data = &np->buffer[0];
  np->len = len;

  memset(np->data, 0, len);
  se = (ServiceEvent*)np->data;
  se->srv = this;
  se->cb = cb;

  /* easy_thread_pool_push_session will call easy_hash_key if we pass NULL. */
  return easy_thread_pool_push_session(workPool_, s, 0);
}

int Service::process(easy_request_t* r, void* args) {
  NetPacket* np = NULL;
  Service* srv = NULL;
  Consensus* cons = NULL;

  np = (NetPacket*)r->ipacket;

  // updateRunning is false only when msg is heartbeat and it is in heartbeat
  // pool
  bool updateRunning = true;
  if (np && r->ms->c->type != EASY_TYPE_CLIENT && r->args) {
    PaxosMsg* m = (PaxosMsg*)r->args;
    if (m->msgtype() == Consensus::OptimisticHeartbeat) {
      updateRunning = false;
      m->set_msgtype(Consensus::AppendLog);
    }
  }

  if (updateRunning && ++Service::running >= Service::workThreadCnt) {
    easy_warn_log("Almost out of workers total:%ld, running:%ld\n",
                  Service::workThreadCnt, Service::running.load());
  }

  auto defer = Defer([updateRunning]() {
    if (updateRunning) {
      --Service::running;
    }
  });

  /* Deal with send fail or Async Event */
  if (np == NULL) {
    np = (NetPacket*)(r->opacket);
    assert(np);
    if (np->type == NetPacketTypeAsync) {
      auto se = (ServiceEvent*)np->data;
      srv = se->srv;
      Callback* cb = se->cb;
      if (cb) {
        srv->incAsyncEvent();
        Service::onAsyncEvent(cb);
        delete cb;
        se->cb = NULL;
      }
    } else {
      /* Send fail case. */
      srv = (Service*)(r->user_data);
      cons = srv->getConsensus();
      if (cons == NULL || cons->isShutdown()) {
        return EASY_ABORT;
      }

      assert(np->type == NetPacketTypeNet);
      assert(r->ms->c->type == EASY_TYPE_CLIENT);
      assert(np->len > 0);
      PaxosMsg newReq;
      // TODO handle parse error
      newReq.ParseFromArray(np->data, np->len);

      uint64_t newId = 0;
      if (r->ms->c->status == EASY_CONN_OK &&
          0 == cons->onAppendLogSendFail(&newReq, &newId)) {
        easy_warn_log(
            "Resend msg msgId(%llu) rename to msgId(%llu) to server %ld, "
            "term:%ld, startLogIndex:%ld, entries_size:%d, pli:%ld\n",
            newReq.msgid(), newId, newReq.serverid(), newReq.term(),
            newReq.entries_size() >= 1 ? newReq.entries().begin()->index() : -1,
            newReq.entries_size(), newReq.prevlogindex());
        srv->incResend();
        newReq.set_msgid(newId);
        np->packetId = newId;
        // TODO handle parse error
        newReq.SerializeToArray(np->data, np->len);
        NetAddressPtr netAddr =
            std::make_shared<EasyNetAddress>(r->ms->c->addr);
        srv->resendPacket(netAddr, np,
                          newId);  // create new session and request
      }
    }
    return EASY_OK;
  }

  srv = (Service*)(r->user_data);
  cons = srv->getConsensus();
  if (cons == NULL || cons->isShutdown()) {
    r->opacket = NULL;
    releaseResources(r);
    return EASY_ABORT;
  }

  /* For ClientService Callback */
  if (srv->cs) {
    if (srv->cs->serviceProcess(r, (void*)cons) == EASY_OK) {
      r->opacket = NULL;
      releaseResources(r);
      return EASY_OK;
    }
  }

  PaxosMsg *msg, omsg;

  if (r->ms->c->type == EASY_TYPE_CLIENT) {
    msg = static_cast<PaxosMsg*>(np->msg);
  } else if (r->args) {
    msg = (PaxosMsg*)r->args;
  } else {
    // TODO should not happen
    msg = &omsg;
  }

  PaxosMsg rsp;
  rsp.set_clusterid(cons->getClusterId());

  if (cons->onMsgPreCheck(msg, &rsp)) {
    serializeResponse(r, msg, rsp);
    releaseResources(r);
    return EASY_OK;
  }

  switch (msg->msgtype()) {
    case Consensus::RequestVote: {
      cons->onRequestVote(msg, &rsp);
      serializeResponse(r, msg, rsp);
      releaseResources(r);
    } break;

    case Consensus::RequestVoteResponce: {
      cons->onRequestVoteResponce(msg);
      r->opacket = NULL;
      releaseResources(r);
    } break;

    case Consensus::AppendLog: {
      if (msgDecompress(*msg) == false) {
        easy_error_log(
            "msg(%llu) from leader(%ld) decompression failed, potential data "
            "corruption!",
            msg->msgid(), msg->leaderid());
        r->opacket = NULL;
        releaseResources(r);
        return EASY_OK;
      }
      cons->onAppendLog(msg, &rsp);
      serializeResponse(r, msg, rsp);
      releaseResources(r);
    } break;

    case Consensus::AppendLogResponce: {
      cons->onAppendLogResponce(msg);
      r->opacket = NULL;
      releaseResources(r);
    } break;

    case Consensus::LeaderCommand: {
      cons->onLeaderCommand(msg, &rsp);
      serializeResponse(r, msg, rsp);
      releaseResources(r);
    } break;

    case Consensus::LeaderCommandResponce: {
      cons->onLeaderCommandResponce(msg);
      r->opacket = NULL;
      releaseResources(r);
    } break;

    case Consensus::ClusterIdNotMatch:
    case Consensus::PreCheckFailedResponce: {
      cons->onMsgPreCheckFailed(msg);
      r->opacket = NULL;
      releaseResources(r);
    } break;
    default:
      easy_warn_log("msg of unknown type id:%llu type:%d serialize failed",
                    msg->msgid(), msg->msgtype());
      r->opacket = NULL;
      releaseResources(r);
      break;
  }  // endswitch

  return EASY_OK;
}

int Service::serializeResponse(easy_request_t* r, PaxosMsg* req,
                               PaxosMsg& rsp) {
  size_t len = rsp.ByteSizeLong();
  size_t extraLen = sizeof(NetPacket);
  NetPacket* np = NULL;
  r->opacket = NULL;

  if (((np = (NetPacket*)easy_pool_alloc(r->ms->pool, extraLen + len)) ==
       NULL)) {
    return -1;
  }

  np->type = NetPacketTypeNet;
  np->len = len;
  np->data = &np->buffer[0];
  try {
    rsp.SerializeToArray(np->data, np->len);
  } catch (google::protobuf::FatalException& e) {
    easy_error_log("response to msg id:%llu type:%d serialize failed",
                   req->msgid(), req->msgtype());
    return -1;
  }

  r->opacket = (void*)np;
  return 0;
}

// release deserialized PaxosMsg at both client and server side
void Service::releaseResources(easy_request_t* r) {
  // PaxosMsg is stored in r->ipacket->msg at client-side, r->args at
  // server-side
  if (r->ipacket) {
    EasyNet::tryFreeMsg(static_cast<NetPacket*>(r->ipacket));
  }
  if (r->args) {
    delete (PaxosMsg*)r->args;
    r->args = NULL;
  }
}

bool MyParseFromArray(google::protobuf::Message& msg, const void* data,
                      int size) {
  google::protobuf::io::CodedInputStream decoder((uint8_t*)data, size);
#if GOOGLE_PROTOBUF_VERSION >= 3002000
  decoder.SetTotalBytesLimit(size);
#else
  decoder.SetTotalBytesLimit(size, 64 * 1024 * 1024);
#endif
  return msg.ParseFromCodedStream(&decoder) && decoder.ConsumedEntireMessage();
}

}; /* end of namespace alisql */

// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#ifndef cluster_service_INC
#define cluster_service_INC

#include <google/protobuf/io/coded_stream.h>

#include "client_service.h"
#include "easyNet.h"
#include "paxos.pb.h"
#include "thread_timer.h"

namespace alisql {

typedef std::function<void()> WorkerStartCallback;
typedef std::function<void()> WorkerEndCallback;

class Consensus;

bool MyParseFromArray(google::protobuf::Message& msg, const void* data,
                      int size);

/**
 * @class Service
 *
 * @brief interface class for Service
 *
 **/
class Service {
 public:
  Service(Consensus* cons);
  virtual ~Service() {}

  virtual int init(uint64_t ioThreadCnt = 4, uint64_t workThreadCnt = 4,
                   uint64_t ConnectTimeout = 300,
                   bool memory_usage_count = false,
                   uint64_t heartbeatThreadCnt = 0,
                   NetType netType = NetType::NET_TYPE_EASY);
  virtual int start(int port);
  virtual int shutdown();
  virtual int stop();
  virtual void setSendPacketTimeout(uint64_t t);
  virtual int sendPacket(NetAddressPtr addr, const std::string& buf,
                         uint64_t id = 0);
  virtual int resendPacket(NetAddressPtr addr, void* ptr, uint64_t id = 0);
  virtual NetAddressPtr createConnection(const std::string& addr,
                                         NetServerRef server, uint64_t timeout,
                                         uint64_t index = 0) {
    return net_->createConnection(addr, server, timeout, index);
  }
  virtual NetAddressPtr recreateConnectionIfNecessary(const std::string& addr,
                                                      NetAddressPtr oldAddr,
                                                      NetServerRef server,
                                                      uint64_t timeout) {
    return net_->recreateConnectionIfNecessary(addr, oldAddr, server, timeout);
  }
  virtual void disableConnection(NetAddressPtr addr) {
    net_->disableConnection(addr);
  }
  // --- async stuff---
  struct Callback;
  struct ServiceEvent {
    Service* srv;
    Callback* cb;
  }; /* end of class ServiceEvent */
  class Callback {
   public:
    virtual void run() = 0;
    virtual ~Callback(){};
  };
  template <typename Callable>
  class CallbackImpl : public Callback {
   public:
    CallbackImpl(Callable&& f) : cb_(std::forward<Callable>(f)) {}
    virtual void run() { cb_(); }

   protected:
    Callable cb_;
  };
  int pushAsyncEvent(Callback* cb);
  template <typename Callable, typename... Args>
  int sendAsyncEvent(Callable&& f, Args&&... args) {
    Callback* pCb = new CallbackImpl(
        std::bind(std::forward<Callable>(f), std::forward<Args>(args)...));
    pushAsyncEvent(pCb);
    return 0;
  }
  static int onAsyncEvent(Callback* cb);

  static int process(easy_request_t* r, void* args);

  std::shared_ptr<Net> getNet() { return net_; }
  Consensus* getConsensus() { return cons_; }
  void setConsensus(Consensus* cons) { cons_ = cons; }
  easy_thread_pool_t* getWorkPool() { return workPool_; }
  easy_thread_pool_t* getHeartbeatPool() { return heartbeatPool_; }
  std::shared_ptr<ThreadTimerService> getThreadTimerService() { return tts_; }
  bool workPoolIsRunning() { return !shutdown_.load(); }

  void incResend() { __sync_fetch_and_add(&resendCnt_, 1); }
  uint64_t getResendCnt() { return resendCnt_; }
  void incAsyncEvent() { __sync_fetch_and_add(&asyncEventCnt_, 1); }
  uint64_t getAsyncEventCnt() { return asyncEventCnt_; }

  static std::atomic<uint64_t> running;
  static uint64_t workThreadCnt;
  ClientService* cs;

  static WorkerStartCallback workerStartCb_;
  static WorkerEndCallback workerEndCb_;

 protected:
  static int serializeResponse(easy_request_t* r, PaxosMsg* req, PaxosMsg& rsp);
  static void releaseResources(easy_request_t* r);

  easy_io_t* pool_eio_;
  std::atomic<bool> shutdown_;
  easy_thread_pool_t* workPool_;
  easy_thread_pool_t* heartbeatPool_;
  std::shared_ptr<Net> net_;
  std::shared_ptr<ThreadTimerService> tts_;
  Consensus* cons_;

  int64_t resendCnt_;
  int64_t asyncEventCnt_;

}; /* end of class Service */

}; /* end of namespace alisql */

#endif  //#ifndef cluster_service_INC

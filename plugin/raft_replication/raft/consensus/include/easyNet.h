// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#ifndef cluster_easynet_INC
#define cluster_easynet_INC

#include <map>
#include <memory>
#include <mutex>

#include "easy_atomic.h"
#include "io/easy_io.h"
#include "net.h"

namespace alisql {

typedef struct NetPacket {
  uint type;
  uint64_t packetId;
  void* msg;
  int len;
  char* data;
  char buffer[0];
} NetPacket;

const uint NetPacketTypeNet = 0;
const uint NetPacketTypeAsync = 1;

const uint64_t NetPacketHeaderSize = sizeof(uint64_t);

// global variable to count easy pool memory usage
extern easy_atomic_t easy_pool_alloc_byte;

class EasyNetAddress : public NetAddress {
 public:
  EasyNetAddress() : NetAddress(NET_TYPE_EASY) { clear(); }
  EasyNetAddress(easy_addr_t addr) : NetAddress(NET_TYPE_EASY), addr_(addr) {}
  virtual ~EasyNetAddress() {}
  virtual void clear() { addr_.port = 0; }
  virtual uint16_t getPort() { return addr_.port; }
  virtual bool isValid() { return addr_.port != 0; }

  easy_addr_t getAddr() const { return addr_; }

 private:
  easy_addr_t addr_;
};

/**
 * @class EasyNet
 *
 * @brief
 *
 **/
class EasyNet : public Net {
 public:
  EasyNet(uint64_t num = 2, const uint64_t sessionTimeout = 300,
          bool memory_usage_count = false);
  virtual ~EasyNet() {}

  virtual int init(void* ptr = NULL) override;
  virtual int start(int port) override;
  virtual int shutdown() override;
  virtual int stop();
  virtual int getPort() override { return port_; }

  /* TODO here we should use a general handler. */
  virtual NetAddressPtr createConnection(const std::string& addr,
                                         NetServerRef server,
                                         uint64_t timeout = 1000,
                                         uint64_t index = 0) override;
  virtual NetAddressPtr recreateConnectionIfNecessary(
      const std::string& addr, NetAddressPtr oldAddr, NetServerRef server,
      uint64_t timeout = 1000) override;
  virtual void disableConnection(NetAddressPtr addr) override;
  virtual int sendPacket(NetAddressPtr addr, const char* buf, uint64_t len,
                         uint64_t id = 0) override;
  virtual int sendPacket(NetAddressPtr addr, const std::string& buf,
                         uint64_t id = 0) override;
  virtual int resendPacket(NetAddressPtr addr, void* ptr, uint64_t id = 0);
  virtual int setRecvPacketCallback(void* handler) override;

  void setWorkPool(easy_thread_pool_t* tp) {
    std::lock_guard<std::mutex> lg(lock_);
    workPool_ = tp;
  }
  void incRecived() { __sync_fetch_and_add(&reciveCnt_, 1); }
  uint64_t getReciveCnt() { return reciveCnt_; }
  void incEncoded() { __sync_fetch_and_add(&encodeCnt_, 1); }
  uint64_t getEncodeCnt() { return encodeCnt_; }
  void incDecoded() { __sync_fetch_and_add(&decodeCnt_, 1); }
  uint64_t getDecodeCnt() { return decodeCnt_; }
  void incDecodeErr() { __sync_fetch_and_add(&decodeErrCnt_, 1); }
  uint64_t getDecodeErrCnt() { return decodeErrCnt_; }

  // bool isShutDown() {return isShutdown_;} /* not used now. */

  uint64_t getAddrKey(easy_addr_t addr);
  NetServerRef getConnData(easy_addr_t addr, bool locked = false);
  void setConnData(easy_addr_t addr, NetServerRef server);
  void delConnDataById(uint64_t id);
  void delConnData(easy_addr_t addr);
  NetServerRef getConnDataAndSetFail(easy_connection_t* c, bool isFail);
  uint64_t getConnCnt() { return connStatus_.size(); }
  void setSessionTimeout(uint64_t t) { sessionTimeout_ = t; }
  void setResendOnTimeout(bool t) { resendOnTimeout_ = t; }

  static void tryFreeMsg(NetPacket* np);

  /* Handler functions. */
  static int reciveProcess(easy_request_t* r);
  static void* paxosDecode(easy_message_t* m);
  static int paxosEncode(easy_request_t* r, void* data);
  static int onConnected(easy_connection_t* c);
  static int onDisconnected(easy_connection_t* c);
  static int onClosed(easy_connection_t* c);
  static int onClientCleanup(easy_request_t* r, void* apacket);
  static uint64_t getPacketId(easy_connection_t* c, void* data);

 protected:
  /* libeasy member. */
  easy_io_t* eio_;
  easy_io_handler_pt clientHandler_;
  easy_io_handler_pt serverHandler_;
  easy_thread_pool_t* workPool_;

  int serverPort_;

  /*TODO we shoud use shared_ptr here. */
  std::map<uint64_t, NetServerRef> connStatus_;
  std::mutex lock_;

  uint64_t reciveCnt_;
  uint64_t encodeCnt_;
  uint64_t decodeCnt_;
  uint64_t decodeErrCnt_;
  bool isShutdown_;
  uint64_t sessionTimeout_;
  // when resendOnTimeout_ is true, it will resend the packet when timeout,
  // otherwise, close current connection and reconnect, default is false
  bool resendOnTimeout_;

  int port_;

 private:
  EasyNet(const EasyNet& other);                   // copy constructor
  const EasyNet& operator=(const EasyNet& other);  // assignment operator

}; /* end of class EasyNet */

}  // namespace alisql
#endif  //#ifndef cluster_easynet_INC

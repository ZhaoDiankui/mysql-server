/************************************************************************
 *
 * Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved
 * $Id:  net.h,v 1.0 07/26/2016 04:45:41 PM
 *yingqiang.zyq(yingqiang.zyq@alibaba-inc.com) $
 *
 ************************************************************************/

/**
 * @file net.h
 * @author yingqiang.zyq(yingqiang.zyq@alibaba-inc.com)
 * @date 07/26/2016 04:45:41 PM
 * @version 1.0
 * @brief
 *
 **/

#ifndef cluster_net_INC
#define cluster_net_INC

#include <string>

#include "io/easy_io.h"

namespace alisql {

class NetServer : public std::enable_shared_from_this<NetServer> {
 public:
  NetServer() : c(nullptr) {}
  virtual ~NetServer() {}
  std::string strAddr;
  void* c;
};
typedef std::shared_ptr<NetServer> NetServerRef;

enum NetType {
  NET_TYPE_EASY = 0,
  NET_TYPE_MEMORY = 1,
  NET_TYPE_MAX = 2,
};

class NetAddress {
 public:
  NetAddress(NetType type) : type_(type) {}
  virtual ~NetAddress() {}
  NetType getType() const { return type_; }
  virtual void clear() = 0;
  virtual uint16_t getPort() = 0;
  virtual bool isValid() = 0;

 private:
  const NetType type_;
};

typedef std::shared_ptr<NetAddress> NetAddressPtr;

class Net {
 public:
  Net() {}
  virtual ~Net() {}

  virtual NetAddressPtr createConnection(const std::string& addr,
                                         NetServerRef server, uint64_t timeout,
                                         uint64_t index) = 0;
  virtual NetAddressPtr recreateConnectionIfNecessary(const std::string& addr,
                                                      NetAddressPtr oldAddr,
                                                      NetServerRef server,
                                                      uint64_t timeout) = 0;
  virtual void disableConnection(NetAddressPtr addr) = 0;
  virtual int sendPacket(NetAddressPtr addr, const char* buf, uint64_t len,
                         uint64_t id) = 0;
  virtual int sendPacket(NetAddressPtr addr, const std::string& buf,
                         uint64_t id) = 0;
  virtual int resendPacket(NetAddressPtr addr, void* ptr, uint64_t id) = 0;
  virtual int setRecvPacketCallback(void* handler) = 0;
  virtual void setSessionTimeout(uint64_t t) = 0;
  virtual void setResendOnTimeout(bool t) = 0;

  virtual int init(void* ptr) = 0;
  virtual int start(int portArg) = 0;
  virtual int stop() = 0;
  virtual int shutdown() = 0;
  virtual int getPort() = 0;

  // for now, we only support easy thread pool
  virtual void setWorkPool(easy_thread_pool_t* tp) = 0;
  virtual void delConnDataById(uint64_t id) = 0;
  virtual uint64_t getConnCnt() = 0;

  virtual void incRecived() = 0;
  virtual uint64_t getReciveCnt() = 0;
  virtual void incEncoded() = 0;
  virtual uint64_t getEncodeCnt() = 0;
  virtual void incDecoded() = 0;
  virtual uint64_t getDecodeCnt() = 0;
  virtual void incDecodeErr() = 0;
  virtual uint64_t getDecodeErrCnt() = 0;

 protected:
 private:
  Net(const Net& other);                   // copy constructor
  const Net& operator=(const Net& other);  // assignment operator

}; /* end of class Net */

}  // namespace alisql

#endif  //#ifndef cluster_net_INC

/************************************************************************
 *
 * Copyright (c) 2017 Alibaba.com, Inc. All Rights Reserved
 * $Id:  tmp-t.cc,v 1.0 02/16/2017 10:29:00 AM
 *yingqiang.zyq(yingqiang.zyq@alibaba-inc.com) $
 *
 ************************************************************************/

/**
 * @file tmp-t.cc
 * @author yingqiang.zyq(yingqiang.zyq@alibaba-inc.com)
 * @date 02/16/2017 10:29:00 AM
 * @version 1.0
 * @brief
 *
 **/

#include <gtest/gtest.h>

#include <atomic>
#include <iostream>
#include <thread>

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
#include "ut_helper.h"

using namespace alisql;

TEST(consensus, PB) {
  std::cout << "start" << std::endl << std::flush;
  {
    PaxosMsg pmsg;
    PaxosMsg* msg = &pmsg;

    ::google::protobuf::RepeatedPtrField<LogEntry>* entries;
    entries = msg->mutable_entries();

    LogEntry entry;
    entry.set_index(1);
    entry.set_value("aaaaaaaaaaaaaaa");
    *(entries->Add()) = entry;
    entry.set_index(2);
    entry.set_value("bbbbbbbbbbbbbbbbbbb");
    *(entries->Add()) = entry;
  }
  std::cout << "stop" << std::endl << std::flush;
}


TEST(consensus, PB_bin) {
  std::cout << "start" << std::endl << std::flush;

  TestMsg1 msg1;
  msg1.set_id(0x5e5e5e);

  TestMsg1 msg2;
  msg2.set_id(0xe5e5e5);
  msg2.set_c1(0xdddddd);

  TestMsg2 msg3;
  msg3.set_id(0xe5e5e5);
  msg3.set_c1(0xdddddd);

  TestMsg2 msg4;
  msg4.set_id(0xe5e5e5);
  msg4.set_c1(0xdddddd);
  msg4.add_c2(0xeeeeee);

  std::string buf1;
  msg1.SerializeToString(&buf1);
  std::string buf2;
  msg2.SerializeToString(&buf2);
  std::string buf3;
  msg3.SerializeToString(&buf3);
  std::string buf4;
  msg4.SerializeToString(&buf4);

  TestMsg2 msg5;
  msg5.ParseFromString(buf2);

  TestMsg1 msg6;
  msg6.ParseFromString(buf4);

  // uint64_t index= atoi("3572172862");
  uint64_t index = strtoull("3572172862", NULL, 10);

  std::cout << "index:" << index << std::endl << std::flush;

  std::cout << "stop" << std::endl << std::flush;
}


TEST(consensus, pb_large) {
  LogEntry le;
  le.set_index(0);
  le.set_optype(1);
  le.set_term(1);
  le.set_value(std::string(400 * 1024 * 1024, 'a'));
  std::string buf;
  le.SerializeToString(&buf);
  std::cout << buf.length() << std::endl;
  LogEntry le2;
  bool ret = le2.ParseFromString(buf);
#if GOOGLE_PROTOBUF_VERSION < 3006001
  EXPECT_EQ(ret, false);
#else
  EXPECT_EQ(ret, true);
#endif
  std::cout << le2.value().length() << std::endl;
  LogEntry le3;
  ret = MyParseFromArray(le3, buf.c_str(), buf.length());
  EXPECT_EQ(ret, true);
  std::cout << le3.value().length() << std::endl;
  EXPECT_EQ(le3.value().length(), le.value().length());
}


TEST(PaxosMsg, serialize_and_parse) {
  PaxosMsg msg;
  msg.set_clusterid(0);
  msg.set_msgid(0);
  msg.set_serverid(0);
  msg.set_msgtype(1);
  msg.set_term(2);
  msg.set_leaderid(3);

  EXPECT_TRUE(msg.IsInitialized());

  uint32_t length = uint32_t(msg.ByteSizeLong());
  length = length;

  std::string buf;
  msg.SerializeToString(&buf);

  EXPECT_GT(buf.length(), 1);

  PaxosMsg rcv;
  rcv.ParseFromString(buf);

  EXPECT_EQ(rcv.msgtype(), 1);
  EXPECT_EQ(rcv.term(), 2);
  EXPECT_TRUE(rcv.has_leaderid());
  EXPECT_EQ(rcv.leaderid(), 3);
  EXPECT_FALSE(rcv.has_prevlogindex());
}

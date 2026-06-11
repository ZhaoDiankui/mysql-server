/************************************************************************
 *
 * Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved
 * $Id:  service-t.cc,v 1.0 08/01/2016 11:14:06 AM
 *yingqiang.zyq(yingqiang.zyq@alibaba-inc.com) $
 *
 ************************************************************************/

/**
 * @file client-service-t.cc
 * @author yingqiang.zyq(yingqiang.zyq@alibaba-inc.com)
 * @date 08/01/2016 11:14:06 AM
 * @version 1.0
 * @brief unit test for alisql::Service
 *
 **/

#include <gtest/gtest.h>
#include <unistd.h>

#include <iostream>

#include "client_service.h"

using namespace alisql;

TEST(ClientService, GetAndSet) {
  ClientService* cs = new ClientService();
  EXPECT_TRUE(cs);

  EXPECT_EQ(cs->get("aaa"), "");

  cs->set("aaa", "bbb");
  EXPECT_EQ(cs->get("aaa"), "bbb");

  cs->set("aaa ccc", 7);
  EXPECT_EQ(cs->get("aaa"), "ccc");

  EXPECT_EQ(cs->set("aaa ddd", 6), "");
  EXPECT_EQ(cs->get("aaa"), "ccc");

  EXPECT_EQ(cs->set("aaabbb", 6), "");
  EXPECT_EQ(cs->get("aaabbb"), "");
}

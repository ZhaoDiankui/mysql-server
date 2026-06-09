/************************************************************************
 *
 * Copyright (c) 2017 Alibaba.com, Inc. All Rights Reserved
 * $Id:  misc-t.cc,v 1.0 01/11/2017 10:59:25 AM
 *yingqiang.zyq(yingqiang.zyq@alibaba-inc.com) $
 *
 ************************************************************************/

/**
 * @file misc-t.cc
 * @author yingqiang.zyq(yingqiang.zyq@alibaba-inc.com)
 * @date 01/11/2017 10:59:25 AM
 * @version 1.0
 * @brief
 *
 **/
#include <gtest/gtest.h>

#include <thread>

#include "files.h"
#include "paxos.h"
#include "rd_paxos_log.h"
#include "single_process_queue.h"
#include "ut_helper.h"

using namespace alisql;

uint64_t globalProcessId = 0;
void processCb(uint64_t* i) {
  std::cout << "Start process id:" << *i << std::endl;
  sleep(1);
  globalProcessId = *i;
}

TEST(misc, SingleProcessQueue) {
  SingleProcessQueue<uint64_t> testQueue;

  testQueue.push(new uint64_t(20));
  testQueue.push(new uint64_t(40));
  // testQueue.process(&processCb);
  std::thread th1(&SingleProcessQueue<uint64_t>::process, &testQueue,
                  processCb);

  testQueue.push(new uint64_t(10));
  std::thread th2(&SingleProcessQueue<uint64_t>::process, &testQueue,
                  processCb);

  th1.join();
  th2.join();

  EXPECT_EQ(globalProcessId, 10);
}

TEST(consensus, Cpp11Time) {
  std::cout << "start" << std::endl << std::flush;
  std::chrono::steady_clock::time_point tp1, tp2;

  tp1 = std::chrono::steady_clock::now();
  sleep(1);
  /*
  tp2= std::chrono::steady_clock::now();

  auto tt= tp2 - tp1;
  auto tt1= std::chrono::duration_cast<std::chrono::microseconds>(tt);
  uint64_t diff= tt1.count();
  */
  uint64_t diff = RemoteServer::diffMS(tp1);

  std::cout << "diff is:" << diff << std::endl << std::flush;

  std::cout << "stop" << std::endl << std::flush;
}

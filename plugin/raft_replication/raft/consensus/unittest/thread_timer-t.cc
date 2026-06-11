// Portions Copyright (c) 2023 ApeCloud, Inc.
// Copyright (c) 2016 Alibaba.com, Inc. All Rights Reserved

#include <gtest/gtest.h>
#include <unistd.h>

#include "service.h"
#include "thread_timer.h"
#include "ut_helper.h"

using namespace alisql;

const int timeoutMS = 2000;
const int durationMS = 300;
/*
static void
timeout_cb (EV_P_ ev_timer *w, int revents)
{
  static int cnt= 0;
  std::cout << "aaaaaaaa" << std::endl << std::flush;
  if (++cnt > 5)
    ev_break (EV_A_ EVBREAK_ONE);
}
TEST(Service, ev_timer)
{
  struct ev_loop *loop = EV_DEFAULT;
  ev_timer timeout_watcher;

  ev_timer_init (&timeout_watcher, timeout_cb, 0.0, 0.2);
  ev_timer_start (loop, &timeout_watcher);

  std::cout << "Start sleep." << std::endl << std::flush;
  ev_run (loop, 0);
  std::cout << "Stop sleep." << std::endl << std::flush;

}
*/

TEST(ThreadTimer, Oneshot) {
  std::shared_ptr<ThreadTimerService> tts =
      std::make_shared<ThreadTimerService>();
  tts->start();
  int cnt = 0;
  // ThreadTimer *tt=
  new ThreadTimer(tts, 0.2, ThreadTimer::Oneshot, [&cnt] { cnt++; });
  EXPECT_EQ_EVENTUALLY(cnt, 1, timeoutMS);
  EXPECT_EQ_CONSISTENTLY(cnt, 1, durationMS, 100);
  // don't delete tt, a Oneshot ThreadTimer is deleted after it's done,
  // otherwise a heap-use-after-free error occurs
}

TEST(ThreadTimer, Oneshot_stop) {
  std::shared_ptr<ThreadTimerService> tts =
      std::make_shared<ThreadTimerService>();
  tts->start();
  int cnt = 0;
  ThreadTimer* tt =
      new ThreadTimer(tts, 0.2, ThreadTimer::Oneshot, [&cnt] { cnt++; });
  delete tt;
  EXPECT_EQ(cnt, 0);
}

TEST(ThreadTimer, Oneshot_quit) {
  std::shared_ptr<ThreadTimerService> tts =
      std::make_shared<ThreadTimerService>();
  tts->start();
  int cnt = 0;
  ThreadTimer* tt =
      new ThreadTimer(tts, 0.2, ThreadTimer::Oneshot, [&cnt] { cnt++; });
  tts->stop();
  delete tt;
  EXPECT_EQ(cnt, 0);
}

TEST(ThreadTimer, Repeatable) {
  std::shared_ptr<ThreadTimerService> tts =
      std::make_shared<ThreadTimerService>();
  tts->start();
  int cnt = 0;
  ThreadTimer* tt =
      new ThreadTimer(tts, 0.2, ThreadTimer::Repeatable, [&cnt] { cnt++; });
  tt->start();
  EXPECT_TRUE_EVENTUALLY(cnt >= 2, timeoutMS);
  cnt = 0;
  EXPECT_TRUE_EVENTUALLY(cnt >= 2, timeoutMS);
  delete tt;
}

TEST(ThreadTimer, Repeatable_stop) {
  std::shared_ptr<ThreadTimerService> tts =
      std::make_shared<ThreadTimerService>();
  tts->start();
  int cnt = 0;
  ThreadTimer* tt =
      new ThreadTimer(tts, 0.2, ThreadTimer::Repeatable, [&cnt] { cnt++; });
  tt->start();
  EXPECT_TRUE_EVENTUALLY(cnt >= 2, timeoutMS);
  tt->stop();
  cnt = 0;
  EXPECT_TRUE_CONSISTENTLY(cnt == 0, durationMS, 100);
  tt->start();
  EXPECT_TRUE_EVENTUALLY(cnt >= 2, timeoutMS);
  delete tt;
}

TEST(ThreadTimer, Repeatable_restart) {
  std::shared_ptr<ThreadTimerService> tts =
      std::make_shared<ThreadTimerService>();
  tts->start();
  int cnt = 0;
  ThreadTimer* tt =
      new ThreadTimer(tts, 0.2, ThreadTimer::Repeatable, [&cnt] { cnt++; });
  tt->start();
  EXPECT_TRUE_EVENTUALLY(cnt >= 2, timeoutMS);
  tt->restart();
  cnt = 0;
  EXPECT_TRUE_EVENTUALLY(cnt >= 2, timeoutMS);
  delete tt;
}

TEST(ThreadTimer, Stage) {
  // use a small interval so that the timing is more accurate
  int smallIntervalMS = 1;

  std::shared_ptr<ThreadTimerService> tts =
      std::make_shared<ThreadTimerService>();
  tts->start();

  int cnt = 0;
  ThreadTimer* tt =
      new ThreadTimer(tts, 1.0, ThreadTimer::Stage, [&cnt] { cnt++; });
  // ensure stageExtraTime is not a small number close to 0
  // otherwise there is probability that currentStage_ changes from 0 to 1
  // too fast that the test fails
  tt->setRandWeight(5);
  tt->start();
  EXPECT_EQ(tt->getTime(), 1.0);
  EXPECT_GT(tt->getStageExtraTime(), 0);
  EXPECT_LT(tt->getStageExtraTime(), tt->getTime());

  int stageExtraTimeMS = static_cast<int>(tt->getStageExtraTime() * 1000);
  // stage keeps 0
  EXPECT_EQ_CONSISTENTLY(tt->getCurrentStage(), 0, 900, smallIntervalMS);

  // after 1s, timer is triggered, and stage becomes 1
  EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(tt->getCurrentStage(), 1, smallIntervalMS,
                                     200);

  // after tt->getStageExtraTime() seconds, timer is triggered, and stage
  // becomes 0, and callback is invoked
  EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(tt->getCurrentStage(), 0, smallIntervalMS,
                                     stageExtraTimeMS + 100);
  EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(cnt, 1, smallIntervalMS, 100);

  // stage keeps 0
  EXPECT_EQ_CONSISTENTLY(tt->getCurrentStage(), 0, 900, smallIntervalMS);

  // after 1s, timer is triggered, and stage becomes 1
  EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(tt->getCurrentStage(), 1, smallIntervalMS,
                                     200);

  // after tt->getStageExtraTime() seconds, timer is triggered, and stage
  // becomes 0, and callback is invoked
  EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(tt->getCurrentStage(), 0, smallIntervalMS,
                                     stageExtraTimeMS + 100);
  EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(cnt, 2, smallIntervalMS, 100);

  delete tt;
}

TEST(ThreadTimer, Stage_restart) {
  // use a small interval so that the timing is more accurate
  int smallIntervalMS = 1;

  std::shared_ptr<ThreadTimerService> tts =
      std::make_shared<ThreadTimerService>();
  tts->start();

  int cnt = 0;
  ThreadTimer* tt =
      new ThreadTimer(tts, 1.0, ThreadTimer::Stage, [&cnt] { cnt++; });
  // ensure stageExtraTime is not a small number close to 0
  // otherwise there is probability that currentStage_ changes from 0 to 1
  // too fast that the test fails
  tt->setRandWeight(5);
  tt->start();
  EXPECT_EQ(tt->getTime(), 1.0);
  EXPECT_GT(tt->getStageExtraTime(), 0);
  EXPECT_LT(tt->getStageExtraTime(), tt->getTime());

  // stage is 0
  EXPECT_EQ_CONSISTENTLY(tt->getCurrentStage(), 0, 900, smallIntervalMS);
  // after 1s, timer is triggered, and stage becomes 1
  EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(tt->getCurrentStage(), 1, smallIntervalMS,
                                     200);

  // restart will reset stage to 0
  tt->restart(0.8);
  EXPECT_EQ(tt->getTime(), 0.8);
  EXPECT_GT(tt->getStageExtraTime(), 0);
  EXPECT_LT(tt->getStageExtraTime(), tt->getTime());
  int stageExtraTimeMS = static_cast<int>(tt->getStageExtraTime() * 1000);
  // stage is 0
  EXPECT_EQ_CONSISTENTLY(tt->getCurrentStage(), 0, 700, smallIntervalMS);

  // after 1s, timer is triggered, and stage becomes 1
  EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(tt->getCurrentStage(), 1, smallIntervalMS,
                                     200);

  // after tt->getStageExtraTime() seconds, timer is triggered, and stage
  // becomes 0, and callback is invoked
  EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(tt->getCurrentStage(), 0, smallIntervalMS,
                                     stageExtraTimeMS + 100);
  EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(cnt, 1, smallIntervalMS, 100);

  delete tt;
}

TEST(ThreadTimer, Stage_restart_0) {
  // use a small interval so that the timing is more accurate
  int smallIntervalMS = 1;

  std::shared_ptr<ThreadTimerService> tts =
      std::make_shared<ThreadTimerService>();
  tts->start();

  int cnt = 0;
  ThreadTimer* tt =
      new ThreadTimer(tts, 1.0, ThreadTimer::Stage, [&cnt] { cnt++; });
  // ensure stageExtraTime is not a small number close to 0
  // otherwise there is probability that currentStage_ changes from 0 to 1
  // too fast that the test fails
  tt->setRandWeight(5);
  tt->start();
  EXPECT_EQ(tt->getTime(), 1.0);
  EXPECT_GT(tt->getStageExtraTime(), 0);
  EXPECT_LT(tt->getStageExtraTime(), tt->getTime());

  // stage is 0
  EXPECT_EQ_CONSISTENTLY(tt->getCurrentStage(), 0, 900, smallIntervalMS);
  // after 1s, timer is triggered, and stage becomes 1
  EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(tt->getCurrentStage(), 1, smallIntervalMS,
                                     200);

  // restart will reset stage to 0
  tt->restart();
  EXPECT_EQ(tt->getTime(), 1.0);
  EXPECT_GT(tt->getStageExtraTime(), 0);
  EXPECT_LT(tt->getStageExtraTime(), tt->getTime());
  int stageDurationMS = static_cast<int>(tt->getStageExtraTime() * 1000);
  // stage is 0
  EXPECT_EQ_CONSISTENTLY(tt->getCurrentStage(), 0, 900, smallIntervalMS);

  // after 1s, timer is triggered, and stage becomes 1
  EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(tt->getCurrentStage(), 1, smallIntervalMS,
                                     200);

  // after tt->getStageExtraTime() seconds, timer is triggered, and stage
  // becomes 0, and callback is invoked
  EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(tt->getCurrentStage(), 0, smallIntervalMS,
                                     stageDurationMS + 100);
  EXPECT_EQ_EVENTUALLY_WITH_INTERVAL(cnt, 1, smallIntervalMS, 100);

  delete tt;
}

TEST(ThreadTimer, setRandWeight) {
  std::shared_ptr<ThreadTimerService> tts =
      std::make_shared<ThreadTimerService>();
  tts->start();

  ThreadTimer* tt = new ThreadTimer(tts, 1.0, ThreadTimer::Stage, [] {});
  tt->start();
  EXPECT_EQ(tt->getTime(), 1.0);
  EXPECT_GT(tt->getStageExtraTime(), 0);
  EXPECT_LT(tt->getStageExtraTime(), 1.0);

  tt->setRandWeight(1);
  tt->restart();
  EXPECT_EQ(tt->getTime(), 1.0);
  EXPECT_GE(tt->getStageExtraTime(), 0.8);
  EXPECT_LT(tt->getStageExtraTime(), 0.9);

  tt->setRandWeight(9);
  tt->restart();
  EXPECT_EQ(tt->getTime(), 1.0);
  EXPECT_GT(tt->getStageExtraTime(), 0);
  EXPECT_LT(tt->getStageExtraTime(), 0.1);

  delete tt;
}

TEST(ThreadTimer, mixed_types_of_timers) {
  std::shared_ptr<ThreadTimerService> tts =
      std::make_shared<ThreadTimerService>();
  tts->start();

  int cnt1 = 0;
  ThreadTimer* tt1 = new ThreadTimer(
      tts, 0.2, ThreadTimer::Repeatable, [&cnt1](int i) { cnt1++; }, 2);
  tt1->start();

  int cnt2 = 0;
  new ThreadTimer(tts, 1.0, ThreadTimer::Oneshot, [&cnt2]() { cnt2++; });

  int cnt3 = 0;
  new ThreadTimer(
      tts, 0.5, ThreadTimer::Oneshot, [&cnt3](uint64_t i) { cnt3++; }, 123);

  int cnt4 = 0;
  ThreadTimer* tt4 =
      new ThreadTimer(tts, 1.0, ThreadTimer::Stage, [&cnt4] { cnt4++; });
  tt4->start();

  EXPECT_TRUE_EVENTUALLY(cnt1 >= 2, timeoutMS);
  cnt1 = 0;
  EXPECT_EQ_EVENTUALLY(cnt2, 1, timeoutMS);
  EXPECT_EQ_CONSISTENTLY(cnt2, 1, durationMS, 100);
  EXPECT_EQ_CONSISTENTLY(cnt3, 1, durationMS, 100);
  EXPECT_TRUE_EVENTUALLY(cnt1 >= 2, timeoutMS);
  EXPECT_TRUE_EVENTUALLY(cnt4 > 0, timeoutMS);

  delete tt1;
  delete tt4;
}
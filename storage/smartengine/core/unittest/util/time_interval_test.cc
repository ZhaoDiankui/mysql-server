/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Copyright (c) 2020, Alibaba Group Holding Limited
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 * http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "util/time_interval.h"
#include "util/testharness.h"

namespace smartengine
{
using namespace common;
using namespace util;

namespace util
{

static std::atomic<uint64_t> t_k(0);
static std::atomic<uint64_t> tl_k(0);
static std::atomic<uint64_t> tl_total_time(0);
static const uint64_t interval_time = 100 * 1000ULL; // 100ms
static const uint64_t thread_num = 10;

static void time_interval_test(void *ptr)
{
  for (int64_t i = 0; i < 1000; i++) {
    if (reach_time_interval(interval_time)) { // 100ms
      t_k++;
    }
    usleep(1000); // 1ms
  }
}

static void tl_time_interval_test(void *ptr)
{
  auto* tl_env = Env::Default();
  uint64_t begin_timestamp = tl_env->NowMicros();
  for (int64_t i = 0; i < 1000; i++) {
    if (reach_tl_time_interval(interval_time)) { // 100ms
      tl_k++;
    }
    usleep(1000); // 1ms
  }
  uint64_t end_timestamp = tl_env->NowMicros();
  tl_total_time.fetch_add(end_timestamp - begin_timestamp);
}

TEST(TimeInterval, time_interval)
{
  auto* env = Env::Default();
  uint64_t begin_timestamp = env->NowMicros();
  for (uint64_t th = 0; th < thread_num; th++) {
    env->StartThread(time_interval_test, nullptr);
  }
  env->WaitForJoin();
  uint64_t end_timestamp = env->NowMicros();
  uint64_t exec_time = end_timestamp - begin_timestamp;
  ASSERT_TRUE(t_k >= ((end_timestamp - begin_timestamp) / interval_time - 1)) << t_k;
  ASSERT_TRUE(t_k <= ((end_timestamp - begin_timestamp) / interval_time + 1)) << t_k;
}

TEST(TimeInterval, tl_time_interval)
{
  auto* env = Env::Default();
  for (uint64_t th = 0; th < thread_num; th++) {
    env->StartThread(tl_time_interval_test, nullptr);
  }
  env->WaitForJoin();
  ASSERT_TRUE(tl_k >= (tl_total_time.load() / interval_time - thread_num)) << tl_k;
  ASSERT_TRUE(tl_k <= (tl_total_time.load() / interval_time + thread_num)) << tl_k;
}

} // namespace util
} // namespace smartengine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
	smartengine::util::test::init_logger(__FILE__);
  return RUN_ALL_TESTS();
}

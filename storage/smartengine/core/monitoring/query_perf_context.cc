/*
 * Portions Copyright (c) 2023, ApeCloud Inc Holding Limited
 * Copyright (c) 2020, Alibaba Group Holding Limited
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "query_perf_context.h"

#include <string>
#include <thread>
#include <time.h>
#ifdef ROCKSDB_JEMALLOC
#include "jemalloc/jemalloc.h"
#endif
#include "env/env.h"
#include "cache/lru_cache.h"
#include "cache/row_cache.h"
#include "util/thread_local.h"
#include "logger/log_module.h"
#include "memory/alloc_mgr.h"

namespace smartengine {
using namespace util;
using namespace common;
using namespace cache;
using namespace memory;

namespace monitor {

thread_local QueryPerfContext *tls_query_perf_context = nullptr;
thread_local bool tls_enable_query_trace = false;

namespace {

#undef DECLARE_TRACE
#undef DECLARE_COUNTER
#define DECLARE_TRACE(trace_point) #trace_point,
#define DECLARE_COUNTER(trace_point)
static const char *TRACE_POINT_NAME[] = {
#include "trace_point.h"
};

#undef DECLARE_TRACE
#undef DECLARE_COUNTER

#define DECLARE_TRACE(trace_point)
#define DECLARE_COUNTER(trace_point) #trace_point,
static const char *COUNT_POINT_NAME[] = {
#include "trace_point.h"
};
#undef DECLARE_TRACE
#undef DECLARE_COUNTER

static constexpr int64_t MAX_TRACE_POINT =
    static_cast<int64_t>(TracePoint::QUERY_TIME_MAX_VALUE);
static constexpr int64_t MAX_COUNT_POINT =
    static_cast<int64_t>(CountPoint::QUERY_COUNT_MAX_VALUE);

static inline TimeType get_time() {
#if defined(__x86_64__) || defined(__amd64__)
  uint64_t hi = 0;
  uint64_t lo = 0;
  __asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return (hi << 32) | lo;
#elif defined(__aarch64__)
  // System timer of ARMv8 runs at a different frequency than the CPU's.
  // The frequency is fixed, typically in the range 1-50MHz.  It can be
  // read at CNTFRQ special register.  We assume the OS has set up
  // the virtual timer properly.
  uint64_t virtual_timer_value = 0;
  asm volatile("mrs %0, cntvct_el0" : "=r"(virtual_timer_value));
  return virtual_timer_value;
#endif
}

static int64_t get_max_trace_name_length();
static int64_t get_max_count_name_length();

static const int32_t COLUMN_SEP = 4;
static const int32_t TRACE_NAME_FORMAT_LENGTH = get_max_trace_name_length() + COLUMN_SEP;
static const int32_t COUNT_NAME_FORMAT_LENGTH = get_max_count_name_length() + COLUMN_SEP;
static const int32_t COST_FORMAT_LENGTH = 20 /* UINT64_MAX virtual size */ + COLUMN_SEP;
static const int32_t PERCENTAGE_FORMAT_LENGTH = 9 /* percentage numer with scale = 6 */ + COLUMN_SEP;
static const int32_t COUNT_FORMAT_LENGTH = 20 /* UINT64_MAX virtual size */;
static const int32_t TRACE_ROW_FORMAT_LENGTH =
    TRACE_NAME_FORMAT_LENGTH + COST_FORMAT_LENGTH + PERCENTAGE_FORMAT_LENGTH + COUNT_FORMAT_LENGTH + 2;
static const int64_t COUNT_ROW_FORMAT_LENGTH = COUNT_NAME_FORMAT_LENGTH + COUNT_FORMAT_LENGTH + 2;

// return the buffer size for QueryPerfContext::to_string.
static int64_t get_max_trace_name_length() {
  int64_t max_len = 0;
  for (int64_t i = 0; i < MAX_TRACE_POINT; ++i) {
    max_len = std::max(max_len,
      static_cast<int64_t>(strlen(TRACE_POINT_NAME[i])));
  }
  return max_len;
}

static int64_t get_max_count_name_length() {
  int64_t max_len = 0;
  for (int64_t i = 0; i < MAX_COUNT_POINT; ++i) {
    max_len = std::max(max_len,
        static_cast<int64_t>(strlen(COUNT_POINT_NAME[i])));
  }
  return max_len;
}
}  // anonymous namespace

StatisticsManager *QueryPerfContext::statistics_ = nullptr;
std::atomic_bool QueryPerfContext::shutdown_;
std::atomic_int_fast32_t QueryPerfContext::running_count_;
pthread_key_t QueryPerfContext::query_trace_tls_key_;
bool QueryPerfContext::opt_enable_count_ = false;
bool QueryPerfContext::opt_print_stats_ = false;
bool QueryPerfContext::opt_trace_sum_ = false;
bool QueryPerfContext::opt_print_slow_ = false;
double QueryPerfContext::opt_threshold_time_ = 100.0;

class StatisticsManager {
 private:
  // Holds data maintained by each thread for implementing tickers.
  struct ThreadCountInfo {
    std::atomic_uint_fast64_t value_;
    // During teardown, value will be summed into *merged_sum.
    std::atomic_uint_fast64_t *merged_sum_;

    ThreadCountInfo(uint_fast64_t value, std::atomic_uint_fast64_t *merged_sum)
        : value_(value), merged_sum_(merged_sum) {}
  };

  // Holds global data for implementing tickers.
  struct CountInfo {
    CountInfo()
        : thread_value_(&CountInfo::release_thread_resource), merged_sum_(0) {}
    // Holds thread-specific pointer to ThreadTickerInfo
    mutable util::ThreadLocalPtr thread_value_;
    // Sum of thread-specific values for tickers that have been reset due to
    // thread termination or ThreadLocalPtr destruction. Also, this is used by
    // setTickerCount() to conveniently change the global value by setting this
    // while simultaneously zeroing all thread-local values.
    std::atomic_uint_fast64_t merged_sum_;

    // This function is registered in ThreadLocalPtr and will be called when
    // the thread exits. Merge the value to a global counter and
    // release the CountInfo object here.
    static void release_thread_resource(void *ptr) {
      auto info_ptr = static_cast<ThreadCountInfo *>(ptr);
      *info_ptr->merged_sum_ +=
          info_ptr->value_.load(std::memory_order_relaxed);
      delete info_ptr;
    }
  };

  // For trace point output
  struct ThreadTraceInfo {
    std::atomic_uint_fast64_t time_value_[MAX_TRACE_POINT];
    std::atomic_uint_fast64_t count_value_[MAX_TRACE_POINT];
    // During teardown, value will be summ-ed into *merged_sum.
    std::atomic_uint_fast64_t *merged_time_sum_;
    std::atomic_uint_fast64_t *merged_count_sum_;

    ThreadTraceInfo(uint_fast64_t *time_value, uint_fast64_t *count_value,
                    std::atomic_uint_fast64_t *merged_time_sum,
                    std::atomic_uint_fast64_t *merged_count_sum) {
      for (int i = 0; i < MAX_TRACE_POINT; ++i) {
        time_value_[i].store(time_value[i]);
        count_value_[i].store(count_value[i]);
        merged_time_sum_ = merged_time_sum;
        merged_count_sum_ = merged_count_sum;
      }
    }

    ThreadTraceInfo(std::atomic_uint_fast64_t *merged_time_sum,
                    std::atomic_uint_fast64_t *merged_count_sum) {
      for (int i = 0; i < MAX_TRACE_POINT; ++i) {
        time_value_[i].store(0);
        count_value_[i].store(0);
        merged_time_sum_ = merged_time_sum;
        merged_count_sum_ = merged_count_sum;
      }
    }
  };

  // Holds global data for implementing tickers.
  struct TraceInfo {
    TraceInfo() : thread_value_(&TraceInfo::release_thread_resource) {}
    // Holds thread-specific pointer to ThreadTickerInfo
    mutable util::ThreadLocalPtr thread_value_;

    std::atomic_uint_fast64_t merged_time_sum_[MAX_TRACE_POINT];
    std::atomic_uint_fast64_t merged_count_sum_[MAX_TRACE_POINT];

    // This function is registered in ThreadLocalPtr and will be called when
    // the thread exits. Merge the value to a global counter and
    // release the CountInfo object here.
    static void release_thread_resource(void *ptr) {
      auto info_ptr = static_cast<ThreadTraceInfo *>(ptr);
      for (int i = 0; i < MAX_TRACE_POINT; ++i) {
        info_ptr->merged_time_sum_[i] += info_ptr->time_value_[i];
        info_ptr->merged_count_sum_[i] += info_ptr->count_value_[i];
      };
      delete info_ptr;
    }
  };

 public:
  StatisticsManager() {}

  int init() {
    int ret = Status::kOk;
    return ret;
  }

  ~StatisticsManager() {}

  CountInfo real_time_counter_[MAX_COUNT_POINT];
  TraceInfo real_time_trace_info_;

  ThreadCountInfo *get_thread_count_info(int64_t point) {
    auto info_ptr = static_cast<ThreadCountInfo *>(
        real_time_counter_[point].thread_value_.Get());
    if (UNLIKELY(nullptr == info_ptr)) {
      info_ptr = new (std::nothrow) ThreadCountInfo(
          0 /* value */, &real_time_counter_[point].merged_sum_);
      real_time_counter_[point].thread_value_.Reset(info_ptr);
    }
    return info_ptr;
  }

  CountType get_counter_info(int64_t point) const {
    int64_t thread_local_sum = 0;
    real_time_counter_[point].thread_value_.Fold(
        [](void *curr_ptr, void *res) {
          auto *sum_ptr = static_cast<int64_t *>(res);
          *sum_ptr += static_cast<std::atomic_uint_fast64_t *>(curr_ptr)->load(
              std::memory_order_relaxed);
        },
        &thread_local_sum);
    return thread_local_sum + real_time_counter_[point].merged_sum_.load(
                                  std::memory_order_relaxed);
  }

  ThreadTraceInfo *get_thread_trace_info() {
    auto info_ptr = static_cast<ThreadTraceInfo *>(
        real_time_trace_info_.thread_value_.Get());
    if (UNLIKELY(nullptr == info_ptr)) {
      info_ptr = new (std::nothrow)
          ThreadTraceInfo(real_time_trace_info_.merged_time_sum_,
                          real_time_trace_info_.merged_count_sum_);
      real_time_trace_info_.thread_value_.Reset(info_ptr);
    }
    return info_ptr;
  }

  void get_trace_info(TimeType *time, CountType *count) const {
    auto sum = std::pair<TimeType *, CountType *>(time, count);
    for (int i = 0; i < MAX_TRACE_POINT; ++i) {
      time[i] += real_time_trace_info_.merged_time_sum_[i];
      count[i] += real_time_trace_info_.merged_count_sum_[i];
    }
    real_time_trace_info_.thread_value_.Fold(
        [](void *curr_ptr, void *res) {
          auto *sum_ptr =
              static_cast<std::pair<TimeType *, CountType *> *>(res);
          auto *curr_trace_info = static_cast<ThreadTraceInfo *>(curr_ptr);
          for (int i = 0; i < MAX_TRACE_POINT; ++i) {
            sum_ptr->first[i] +=
                curr_trace_info->time_value_[i].load(std::memory_order_relaxed);
            sum_ptr->second[i] += curr_trace_info->count_value_[i].load(
                std::memory_order_relaxed);
          }
        },
        &sum);
  }

  void clear() {
    for (int i = 0; i < MAX_TRACE_POINT; ++i) {
      real_time_trace_info_.merged_time_sum_[i] = 0;
      real_time_trace_info_.merged_count_sum_[i] = 0;
    }
    real_time_trace_info_.thread_value_.Fold(
        [](void *curr_ptr, void *res) {
          auto *curr_trace_info = static_cast<ThreadTraceInfo *>(curr_ptr);
          for (int i = 0; i < MAX_TRACE_POINT; ++i) {
            curr_trace_info->time_value_[i].store(0, std::memory_order_relaxed);
            curr_trace_info->count_value_[i].store(0,
                                                   std::memory_order_relaxed);
          }
        },
        nullptr);

    for (int i = 0; i < MAX_COUNT_POINT; ++i) {
      real_time_counter_[i].merged_sum_ = 0;
      real_time_counter_[i].thread_value_.Fold(
          [](void *curr_ptr, void *res) {
            static_cast<std::atomic_uint_fast64_t *>(curr_ptr)->store(
                0, std::memory_order_relaxed);
          },
          nullptr);
    }
  }

  // **********************************************************************
  void add_counter(CountPoint point, CountType count) {
    int64_t counter_type = static_cast<int64_t>(point);
    ThreadCountInfo *counter_to_add = get_thread_count_info(counter_type);
    if (nullptr == counter_to_add) {
      SE_LOG(ERROR, "get_thread_count_info return nullptr",
                  K(counter_type), K(count));
    } else {
      counter_to_add->value_.fetch_add(count, std::memory_order_relaxed);
    }
  }

  void add_trace_info(TracePoint point, TimeType time, CountType count) {
    int64_t trace_type = static_cast<int64_t>(point);
    ThreadTraceInfo *info_to_add = get_thread_trace_info();
    if (nullptr == info_to_add) {
      SE_LOG(ERROR, "get_thread_trace_info return nullptr", K(trace_type),
                  K(time), K(count));
    } else {
      info_to_add->time_value_[trace_type].fetch_add(time);
      info_to_add->count_value_[trace_type].fetch_add(count);
    }
  }
};

QueryPerfContext::QueryPerfContext() {}

int QueryPerfContext::init() { return Status::kOk; }

static const int64_t kNanoSec_Sec = 1000000000L;
static int64_t GetNaNos() {
  timespec tv;
  clock_gettime(CLOCK_REALTIME, &tv);
  return static_cast<int64_t>(tv.tv_sec) * kNanoSec_Sec + tv.tv_nsec;
}

void QueryPerfContext::reset() {
  last_time_point_ = get_time();
  memset(stats_, 0, MAX_TRACE_POINT * sizeof(stats_[0]));
  memset(counters_, 0, MAX_COUNT_POINT * sizeof(counters_[0]));
  trace_stack_.clear();
  time_stack_.clear();
  begin_nanos_ = GetNaNos();
}

void QueryPerfContext::trace(TracePoint point) {
  TimeType now = get_time();
  // Step1: temporarily end last trace.
  if (!trace_stack_.empty()) {
    TracePoint last_point = *(trace_stack_.end() - 1);
    TimeType start_time = *(time_stack_.end() - 1);
    TimeType time_delta = now - start_time;
    stats_[static_cast<int64_t>(last_point)].cost_ += time_delta;
    if (opt_trace_sum_) {
      statistics_->add_trace_info(last_point, time_delta, 0);
    }
  }
  // Step2: start this trace.
  trace_stack_.push_back(point);
  time_stack_.push_back(now);
}

void QueryPerfContext::end_trace() {
  if (!trace_stack_.empty()) {
    // Step1: stop this trace.
    TimeType now = get_time();
    TracePoint point = *(trace_stack_.end() - 1);
    TimeType start_time = *(time_stack_.end() - 1);
    TimeType time_delta = now - start_time;
    stats_[static_cast<int64_t>(point)].cost_ += time_delta;
    stats_[static_cast<int64_t>(point)].count_ += 1;

    trace_stack_.pop_back();
    time_stack_.pop_back();

    if (opt_trace_sum_) {
      statistics_->add_trace_info(point, time_delta, 1);
    }

    // Step2: continue the outer trace from 'now'.
    if (!time_stack_.empty()) {
      *(time_stack_.end() - 1) = now;
    }
  }
}

void QueryPerfContext::count(CountPoint point, CountType delta) {
  counters_[static_cast<int64_t>(point)].count_ += delta;
  statistics_->add_counter(point, delta);
}

/**Dump the thread local statistics of query to internal buffer contents_
with specific format.It's used to print statistics info to log file.
@param[in]  total_time  query execute time
@param[out]  res  the specific format output str
@param[out]  size size of res buf
Note: the res point to internal buffer contents_, the caller should directly
write it out follow this function invoke.*/
void QueryPerfContext::to_string(int64_t total_time, const char *&res, int64_t &size)
{
  contents_.clear();
  if (0 != total_time) {
    dump_trace_to_str(total_time, contents_);
    dump_counter_to_str(contents_);
  }
  res = contents_.data();
  size = contents_.size();
}

/**Dump detailed trace statistics to content.
@param[in]  total_time  query execute time
@param[in/out]  content  target trace statistics buffer*/
void QueryPerfContext::dump_trace_to_str(const int64_t total_time, std::string &content)
{
  char trace_row_buffer[TRACE_ROW_FORMAT_LENGTH];
  memset(trace_row_buffer, 0, TRACE_ROW_FORMAT_LENGTH);

  TimeType total_cost = 0;
  TimeType trace_real_time = 0;
  /*coefficient of transform cpu tick to real time.*/
  double tick_coefficient = 0.0;

  /**step 1: count the total coast and calculate tick coefficient.*/
  for (int64_t i = 0; i < MAX_TRACE_POINT; ++i) {
    stats_[i].point_id_ = i;
    total_cost += stats_[i].cost_;
  }

  if (0 != total_cost) {
    tick_coefficient =
        (static_cast<double>(total_time)) / (static_cast<double>(total_cost));

    /**step 2: print attribute names for statistics.*/
    snprintf(trace_row_buffer, TRACE_ROW_FORMAT_LENGTH, "%-*s%-*s%-*s%-*s\n",
        TRACE_NAME_FORMAT_LENGTH, "TRACE_LIST",
        COST_FORMAT_LENGTH, "TIME",
        PERCENTAGE_FORMAT_LENGTH, "PERCENTAGE",
        COUNT_FORMAT_LENGTH, "TOTAL_COUNT\n");
    contents_.append(trace_row_buffer, TRACE_ROW_FORMAT_LENGTH - 1);

    /**step 3: print detailed traces order by cost descending order.*/
    std::sort(stats_, stats_ + MAX_TRACE_POINT,
        [](const TraceStats &left, const TraceStats &right) {
          return left.cost_ > right.cost_;
        });
    for (int64_t i = 0; i < MAX_TRACE_POINT; ++i) {
      if (0 != stats_[i].cost_) {
        trace_real_time = static_cast<int64_t>(stats_[i].cost_ * tick_coefficient);
        snprintf(trace_row_buffer, TRACE_ROW_FORMAT_LENGTH, "%-*s%-*ld%-*lf%-*ld\n",
                 TRACE_NAME_FORMAT_LENGTH, TRACE_POINT_NAME[stats_[i].point_id_],
                 COST_FORMAT_LENGTH, trace_real_time,
                 PERCENTAGE_FORMAT_LENGTH, (double)stats_[i].cost_ * 100.0 / (double)total_cost,
                 COUNT_FORMAT_LENGTH, stats_[i].count_);
        contents_.append(trace_row_buffer, TRACE_ROW_FORMAT_LENGTH - 1);
      }
    }
  }
}

/**Dump detailed counter statistics to content.
@param[in/out]  content target trace statistics buffer*/
void QueryPerfContext::dump_counter_to_str(std::string &content)
{
  char count_row_buffer[COUNT_ROW_FORMAT_LENGTH];
  memset(count_row_buffer, 0, COUNT_ROW_FORMAT_LENGTH);

  snprintf(count_row_buffer, COUNT_ROW_FORMAT_LENGTH, "%-*s%-*s\n",
      COUNT_NAME_FORMAT_LENGTH, "COUNTER LIST",
      COUNT_FORMAT_LENGTH, "COUNT");
  content.append(count_row_buffer, COUNT_ROW_FORMAT_LENGTH - 1);

  for (int64_t i = 0; i < MAX_COUNT_POINT; ++i) {
    counters_[i].point_id_ = i;
  }
  std::sort(counters_, counters_ + MAX_COUNT_POINT,
      [](const CountStats &left, const CountStats &right) {
        return left.count_ > right.count_;
      });
  for (int64_t i = 0; i < MAX_COUNT_POINT; ++i) {
    if (0 != counters_[i].count_) {
      snprintf(count_row_buffer, COUNT_ROW_FORMAT_LENGTH, "%-*s%-*ld\n",
          COUNT_NAME_FORMAT_LENGTH, COUNT_POINT_NAME[counters_[i].point_id_],
          COUNT_FORMAT_LENGTH, counters_[i].count_);
      content.append(count_row_buffer, COUNT_ROW_FORMAT_LENGTH - 1);
    }
  }
}

TimeType QueryPerfContext::current(util::Env *env) const {
  return env->NowMicros();
}

CountType QueryPerfContext::get_count(TracePoint point) const {
  return stats_[static_cast<int64_t>(point)].count_;
}

CountType QueryPerfContext::get_count(CountPoint point) const {
  return counters_[static_cast<int64_t>(point)].count_;
}

TimeType QueryPerfContext::get_costs(TracePoint point) const {
  return stats_[static_cast<int64_t>(point)].cost_;
}

CountType QueryPerfContext::get_global_count(CountPoint point) const {
  return statistics_->get_counter_info(static_cast<int64_t>(point));
}

void QueryPerfContext::get_global_trace_info(TimeType *time,
                                             CountType *count) const {
  statistics_->get_trace_info(time, count);
}

void QueryPerfContext::clear_stats() { statistics_->clear(); }

void QueryPerfContext::print_int64_to_buffer(char *buffer, int64_t &pos,
                                             int64_t value) {
  static const char alpha[] = "0123456789ABCDEF";
  int32_t shift_size = 56;
  while (shift_size >= 0) {
    buffer[pos++] = alpha[(value >> (shift_size + 4)) & 0xf];
    buffer[pos++] = alpha[(value >> shift_size) & 0xf];
    shift_size -= 8;
  }
}

QueryPerfContext *QueryPerfContext::new_query_context() {
  static pthread_once_t key_once = PTHREAD_ONCE_INIT;
  (void)pthread_once(&key_once, make_key);

  QueryPerfContext *ctx = new (std::nothrow) QueryPerfContext();
  int s = Status::kOk;
  if (nullptr == ctx) {
    s = Status::kMemoryLimit;
    SE_LOG(ERROR, "new QueryPerfContext failed");
  } else if ((s = ctx->init())) {
    delete ctx;
    ctx = nullptr;
    SE_LOG(ERROR, "QueryPerfContext::init failed", K(s));
  } else {
    if (UNLIKELY(nullptr == statistics_)) {
      static std::mutex stats_mutex;
      std::lock_guard<std::mutex> guard(stats_mutex);
      if (nullptr == statistics_) {
        statistics_ = new (std::nothrow) StatisticsManager();
        if (nullptr == statistics_) {
          SE_LOG(ERROR, "new StatisticsManager failed");
        } else if ((s = statistics_->init())) {
          delete statistics_;
          statistics_ = nullptr;
          SE_LOG(ERROR, "StatisticsManager init failed", K(s));
        }
      }
    }
    shutdown_.store(false);
  }
  (void)pthread_setspecific(query_trace_tls_key_, ctx);
  return ctx;
}

void QueryPerfContext::finish(const char *query, uint64_t query_length) {
  int64_t total_time = (double)(GetNaNos() - begin_nanos_);
  if (nullptr != query && query_length > 0 && opt_print_slow_ &&
      (double)total_time > opt_threshold_time_ * (double)kNanoSec_Sec) {
    __SE_LOG(WARN, "slow query: %*.*s\n", query_length, query_length,
                  query);
    const char *stats_data = nullptr;
    int64_t stats_size = 0;
    to_string(total_time, stats_data, stats_size);
    __SE_LOG(WARN, "%*.*s\n", stats_size, stats_size, stats_data);
  }
}

struct LogStatsParam {
  Env *env_;
  QueryPerfContext *ctx_;
  const std::string *path_;
  cache::Cache *block_cache_;
  cache::RowCache *row_cache_;
};

#ifdef ROCKSDB_JEMALLOC
typedef struct {
  char *cur;
  char *end;
} MallocStatus;

static void GetJemallocStatus(void *mstat_arg, const char *status) {
  MallocStatus *mstat = reinterpret_cast<MallocStatus *>(mstat_arg);
  size_t status_len = status ? strlen(status) : 0;
  size_t buf_size = (size_t)(mstat->end - mstat->cur);
  if (!status_len || status_len > buf_size) {
    return;
  }

  snprintf(mstat->cur, buf_size, "%s", status);
  mstat->cur += status_len;
}
#endif  // ROCKSDB_JEMALLOC

static void DumpMallocStats(std::string *stats) {
#ifdef ROCKSDB_JEMALLOC
  MallocStatus mstat;
  const unsigned int kMallocStatusLen = 1000000;
  char *ptr = new (std::nothrow) char[kMallocStatusLen + 1];
  if (nullptr == ptr) {  // allocated failed
  } else {
    std::unique_ptr<char[]> buf;
    buf.reset(ptr);
    mstat.cur = buf.get();
    mstat.end = buf.get() + kMallocStatusLen;
    malloc_stats_print(GetJemallocStatus, &mstat, "");
    // je_malloc_stats_print(GetJemallocStatus, &mstat, "");
    stats->append(buf.get());
  }
#endif  // ROCKSDB_JEMALLOC
}

void QueryPerfContext::schedule_log_stats(void *param) {
  running_count_.fetch_add(1);
  std::string stats;
  auto log_stats_param = static_cast<LogStatsParam *>(param);
  if (opt_print_stats_) {
    stats.clear();
    DumpMallocStats(&stats);
    if (!stats.empty()) {
      __SE_LOG(INFO,
                    "\n------- Malloc STATS -------\n"
                    "%s\n",
                    stats.c_str());
    }
    // cache info
    LRUCache *block_cache =
        static_cast<LRUCache *>(log_stats_param->block_cache_);
    if (nullptr != block_cache) {
      SE_LOG(INFO, "BLOCK CACHE INFO START");
      block_cache->print_cache_info();
      SE_LOG(INFO, "BLOCK CACHE INFO END");
    } else {
      SE_LOG(INFO, "block is null");
    }
  }

  stats.clear();
  AllocMgr::get_instance()->print_memory_usage(stats);
  __SE_LOG(INFO,
                "\n------- MOD MEMORY INFO -------"
                "%s\n------- MOD MEMORY END -------\n",
                stats.c_str());
  if (nullptr != log_stats_param->row_cache_) {
    stats.clear();
    log_stats_param->row_cache_->print_stats(stats);
    //__SE_LOG(INFO,  "\n------- ROW CACHE INFO -------\n"
    //                     "%s \n------- ROW CACHE END -------\n",
    //                     stats.c_str());
  }
  delete log_stats_param;
  running_count_.fetch_sub(1);
}

void QueryPerfContext::async_log_stats(util::Env *env, const std::string &path,
                                       Cache *block_cache,
                                       RowCache *row_cache) {
  QueryPerfContext *ctx = get_tls_query_perf_context();
  auto log_stats_param = new (std::nothrow) LogStatsParam();
  if (nullptr == log_stats_param) {
    SE_LOG(ERROR, "new log_stats_param failed");
  } else {
    log_stats_param->env_ = env;
    log_stats_param->ctx_ = ctx;
    log_stats_param->path_ = &path;
    log_stats_param->block_cache_ = block_cache;
    log_stats_param->row_cache_ = row_cache;
    env->Schedule(&schedule_log_stats, log_stats_param, util::Env::STATS);
  }
}

void QueryPerfContext::make_key() {
  (void)pthread_key_create(&query_trace_tls_key_, &delete_context);
}

void QueryPerfContext::delete_context(void *ctx) {
  auto perf_ctx = static_cast<QueryPerfContext *>(ctx);
  tls_query_perf_context = nullptr;
  pthread_setspecific(query_trace_tls_key_, nullptr);
  delete perf_ctx;
}

void QueryPerfContext::shutdown() {
  shutdown_.store(true);
  while (running_count_.load() > 0) {
    port::AsmVolatilePause();
  }
}

class A {
 public:
  ~A() { QueryPerfContext::delete_context(tls_query_perf_context); }
} a;

TraceGuard::TraceGuard(TracePoint point) : point_(point) {
  query_trace_begin(point_);
}

TraceGuard::~TraceGuard() { query_trace_end(); }

const char **get_trace_point_name() { return TRACE_POINT_NAME; }

const char **get_count_point_name() { return COUNT_POINT_NAME; }

TimeType get_trace_unit(int64_t eval_milli_sec) {
  TimeType time1 = get_time();
  std::this_thread::sleep_for(std::chrono::milliseconds(eval_milli_sec));
  TimeType time2 = get_time();
  return time2 - time1;
}

}  // namespace monitor
}  // namespace smartengine

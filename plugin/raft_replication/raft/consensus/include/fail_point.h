// Copyright (c) 2023 ApeCloud, Inc.

#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace alisql {

// store the data for a fail point's user passed predication.
struct FailPointData {
  enum DataType { kInt, kUint64, kDouble, kString };  // extendable

  FailPointData() : type_(kInt), data_{0} {}
  FailPointData(int i) : type_(kInt) { data_.i = i; }
  FailPointData(uint64_t u64) : type_(kUint64) { data_.u64 = u64; }
  FailPointData(double d) : type_(kDouble) { data_.d = d; }
  FailPointData(std::string s) : type_(kString) {
    data_.s = new std::string(s);
  }
  FailPointData(const FailPointData& fpd) : type_(fpd.type_) {
    switch (type_) {
      case kInt:
        data_.i = fpd.data_.i;
        break;
      case kUint64:
        data_.u64 = fpd.data_.u64;
        break;
      case kDouble:
        data_.d = fpd.data_.d;
        break;
      case kString:
        data_.s = new std::string(*fpd.data_.s);
        break;
    }
  }
  void destroy() const {
    if (type_ == kString) {
      delete data_.s;
    }
  }
  ~FailPointData() { destroy(); }
  FailPointData& operator=(const FailPointData& fpd) {
    if (this == &fpd) {
      return *this;
    }
    destroy();
    type_ = fpd.type_;
    switch (type_) {
      case kInt:
        data_.i = fpd.data_.i;
        break;
      case kUint64:
        data_.u64 = fpd.data_.u64;
        break;
      case kDouble:
        data_.d = fpd.data_.d;
        break;
      case kString:
        data_.s = new std::string(*fpd.data_.s);
        break;
    }
    return *this;
  }

  DataType type_;
  union {
    int i;
    uint64_t u64;
    double d;
    std::string* s;
  } data_;
};

/**
 *
 * A FailPoint allows a test to force a particular code path to be taken.
 * So we can inject some unexpected behavior to test the code's handling of it.
 *
 * If you want to use a fail point in a test, you need to:
 * 0. Add -D WITH_FAIL_POINT=ON to the compiler flags.
 * 1. Define a fail point using ’FAIL_POINT_DEFINE(failPointName)’.
 * 2. Activate a fail point using
 * ’failPointName.activate(FailPoint::FailPointType)’.
 * 3. Inject the fail point to target code snippet. We will give a example
 * below.
 *
 * 1. Define a fail point named `firstFailPoint`, outside of any local area.
 * FAIL_POINT_DEFINE(firstFailPoint);
 *
 * 2. Activate the fail point `firstFailPoint`, usually in the test code.
 * You should pass a FailPoint::FailPointType to enable the fail point.
 * There are 5 types:
 * a. FailPointType::off: the fail point is disabled.
 * b. FailPointType::alwaysOn: the fail point is always active.
 * c. FailPointType::random: the fail point is active randomly.
 * d. FailPointType::finiteTimes: the fail point is active for n times.
 * e. FailPointType::finiteSkip: the fail point is active after skipping n
 * times.
 *
 * firstFailPoint.enable(FailPoint::alwaysOn);
 *
 * if you want change the fail point's type, you can call
 * firstFailPoint.changeType(FailPoint::random);
 *
 * 3. below is an example of how to inject a fail point to target code snippet.
 *
 * bool targetFunc() {
 *   ......
 *   // The return value will be changed artificially if the firstFailPoint is
 * active. if (firstFailPoint.evaluateFail()) return false; return true;
 *   ......
 * }
 *
 * bool targetFunc() {
 *   ......
 *   // The return value will be changed artificially if the firstFailPoint is
 * active
 *   // and the input predication returns true.
 *   if (firstFailPoint.evaluateFailIf()(
 *     [&](FailPointData &data) {
 *       if (data.type_ == FailPointData::kInt
 *         && data.data_.i == 1)
 *           return true;
 *         else
 *           return false;})) {
 *     // targetFunc unexpacted early return here.
 *     return false;
 *   }
 *   ......
 *   return true;
 * }
 *
 *
 * or to inject more complex fault, use inject/injectFirstIfSecond
 *
 * bool targetFunc() {
 *   ......
 *   firstFailPoint.inject([&]() {
 *     // The bad things can happen here, like crash/hung.
 *   });
 *   ......
 *   return true;
 * }
 *
 * bool targetFunc() {
 *   ......
 *   firstFailPoint.injectFirstIfSecond([&]() {
 *     // The bad things can happen here, like crash/hung.
 *   }, [&](FailPointData &data) {
 *   if (data.type_ == FailPointData::kInt
 *       && data.data_.i == 1)
 *     return true;
 *   else
 *     return false;});
 *   ......
 *   return true;
 * }
 *
 * See more in `consenesus/unittest/fail_point-t.cpp`.
 *
 */
class FailPoint {
 public:
  using CountType = uint32_t;
  using AtomicCountType = std::atomic<CountType>;
  using InputConditionFunc = std::function<bool(FailPointData&)>;
  using InputActionFunc = std::function<void(FailPointData&)>;
  enum FailPointType { off, alwaysOn, random, finiteTimes, finiteSkip };
  // this bit of fpRef_ tells whether this fail point is active.
  static constexpr auto kActiveBit_ =
      CountType(CountType{1} << (sizeof(CountType) * 8 - 1));

  explicit FailPoint(std::string name);
  FailPoint(const FailPoint&) = delete;
  FailPoint& operator=(const FailPoint&) = delete;
  ~FailPoint(){};

 private:
  // RAII class to guard the reference count of a fail point.
  class RefGuard {
   public:
    RefGuard(AtomicCountType* fpRefPtr, bool hit)
        : fpRefPtr_(fpRefPtr), hit_(hit) {
      if (fpRefPtr_) {
        fpRefPtr_->fetch_add(1);
      }
    }
    ~RefGuard() {
      if (fpRefPtr_) {
        fpRefPtr_->fetch_sub(1);
      }
    }

    RefGuard(RefGuard&& other)
        : fpRefPtr_(std::exchange(other.fpRefPtr_, nullptr)),
          hit_(std::exchange(other.hit_, false)) {}
    RefGuard& operator=(const RefGuard&) = delete;
    RefGuard& operator=(RefGuard&&) = delete;

    bool hit() const { return hit_; }
    void setHit(bool hit) { hit_ = hit; }

   private:
    AtomicCountType* fpRefPtr_;
    bool hit_;
  };

 public:
  // This function activate a fail point.
  void activate(FailPointType type, CountType typeValue = 0,
                FailPointData data = FailPointData(), double probability = 0.0);
  // This function change the fail point's type.
  void changeType(FailPointType type, CountType typeValue = 0,
                  FailPointData data = FailPointData(),
                  double probability = 0.0);
  // This function deactivate a fail point.
  void deactivate();

  template <typename Condition>
  bool evaluateFailIf(Condition&& condition) {
    return tryDetectHit(std::forward<Condition>(condition)).hit();
  }

  template <typename F, typename Condition>
  void injectFirstIfSecond(F&& f, Condition&& condition) {
    RefGuard rg = tryDetectHit(std::forward<Condition>(condition));
    if (rg.hit()) {
      std::forward<F>(f)();
    }
  }

  bool evaluateFail() { return evaluateFailIf(nullptr); }

  template <typename F>
  void inject(F&& f) {
    injectFirstIfSecond(std::forward<F>(f), nullptr);
  }

  template <typename F>
  void injectWithData(F&& f) {
    RefGuard rg = tryDetectHit(nullptr);
    if (rg.hit()) {
      auto wrapFunc = InputActionFunc(std::move(f));
      wrapFunc(data_);
    }
  }

  std::string name() const;

 private:
  /**
   * This function returns true only if
   * 1. the fail point is enabled.
   * 2. the input condition returns true or nullptr.
   * 3. the type is not "off".
   */
  template <typename Condition>
  RefGuard tryDetectHit(Condition&& condition) {
    if ((fpRef_.load() & kActiveBit_) == 0) {
      // fail point is disabled.
      return RefGuard{nullptr, false};
    }

    // some other thread may disable this failpoint after
    // above check. so we need to check again after
    // increasing the reference count of this failpoint.
    RefGuard refGuard{&fpRef_, false};

    // can not disable this fail point since now,
    // because we have increased the reference count.
    if ((fpRef_.load() & kActiveBit_) == 0) {
      // fail point is disabled before we increased the reference count.
      return refGuard;
    }

    // Wrap the input condition function to a function that returns bool
    // and takes a FailPointData as input.
    auto wrapFunc = InputConditionFunc(std::move(condition));

    // If the condition is false, we don't need to take the type into
    // consideration.
    bool hit = (wrapFunc && !wrapFunc(data_)) ? false : estimateByType();

    refGuard.setHit(hit);

    // fpRef_.fetch_sub(1) is called in the destructor of RefGuard;
    // The RefGuard is needed because we need to prevent other threads
    // trying to change the type.
    return refGuard;
  }

  void enable();
  void disable();
  void waitUntilNoOutstandingReferences();
  bool estimateByType();

  std::string name_;

  // layout:
  // 31: active or not. 0~30: ref counter of this failpoint.
  AtomicCountType fpRef_;

  // mutex_ is used to protect below variables when 2 threads call
  // changeType() parallelly.
  FailPointType type_;
  // only for finiteTimes and finiteSkip type.
  std::atomic<int> typeValue_;
  // only for random type.
  double probability_;
  // store some data for the condition.
  FailPointData data_;
  std::mutex mutex_;
};

// A global registry to store all fail points.
class FailPointRegistry {
 public:
  using FailPointMap = std::unordered_map<std::string, FailPoint*>;

  FailPointRegistry(){};
  ~FailPointRegistry(){};
  bool add(FailPoint* failPoint);
  FailPoint* find(const std::string& name) const;
  void disableAll();

  static FailPointRegistry& getGlobalFailPointRegistry() {
    static FailPointRegistry globalRegistry;
    return globalRegistry;
  }

 private:
  FailPointMap fpMap_;
};

/**
 * define and register a fail point. A global FailPoint variable is created.
 * So do not use this macro in a header file or inside a function or class
 * definition.
 */
#define FAIL_POINT_DEFINE(fp) \
  alisql::FailPoint fp(#fp);  \
  bool dummyRet##fp =         \
      alisql::FailPointRegistry::getGlobalFailPointRegistry().add(&(fp));

}  // namespace alisql

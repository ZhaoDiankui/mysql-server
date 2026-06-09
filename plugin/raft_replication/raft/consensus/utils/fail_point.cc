// Copyright (c) 2023 ApeCloud, Inc.

#include "fail_point.h"

#include <assert.h>

#include <string>
#include <thread>

namespace alisql {

FailPoint::FailPoint(std::string name)
    : name_(name),
      fpRef_(0),
      type_(off),
      typeValue_(0),
      probability_(0.0),
      data_() {
  // for random type
  srand(time(NULL));
};

void FailPoint::changeType(FailPointType type, CountType typeValue,
                           FailPointData data, double probability) {
  std::lock_guard<std::mutex> lock(mutex_);

  // 1. disable this fail point if it is already enabled.
  disable();

  // 2. wait until all outstanding references are released.
  waitUntilNoOutstandingReferences();

  // 3. set the type to a new value, never change until next changeType,
  // so it is safe to read it without any lock.
  type_ = type;
  typeValue_ = typeValue;
  data_ = data;
  probability_ = probability;

  if (type_ != off) {
    enable();
  }
}

void FailPoint::activate(FailPointType type, CountType typeValue,
                         FailPointData data, double probability) {
  changeType(type, typeValue, data, probability);
}

void FailPoint::deactivate() { changeType(FailPointType::off); }

std::string FailPoint::name() const { return name_; }

void FailPoint::enable() { fpRef_.fetch_or(kActiveBit_); }

void FailPoint::disable() { fpRef_.fetch_and(~kActiveBit_); }

void FailPoint::waitUntilNoOutstandingReferences() {
  int times = 100;
  for (int i = 0; i < times; i++) {
    if (fpRef_.load() == 0) {
      return;
    }
    std::this_thread::yield();
  }
  while (fpRef_.load() != 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

bool FailPoint::estimateByType() {
  // Do not have to lock mutex_ here, because reseting type_ will
  // wait refenrence count decrementing to 1.
  switch (type_) {
    case FailPointType::alwaysOn:
      return true;
    case FailPointType::random: {
      double random_number = static_cast<double>(rand()) / RAND_MAX;
      return random_number < probability_;
    }
    case FailPointType::finiteTimes: {
      if (typeValue_.fetch_sub(1) <= 0) {
        disable();
        return false;
      }
      return true;
    }
    case FailPointType::finiteSkip:
      // Ensure that once the finiteSkip counter reaches within some delta from
      // 0 we don't continue decrementing it unboundedly because at some point
      // it will roll over and become positive again
      return typeValue_.load() <= 0 || typeValue_.fetch_sub(1) < 0;
    case FailPointType::off: {
      return false;
    }
    default: {
      assert(false);
    }
  }
}

bool FailPointRegistry::add(FailPoint* failPoint) {
  bool ok = fpMap_.insert({failPoint->name(), failPoint}).second;
  return ok;
}

FailPoint* FailPointRegistry::find(const std::string& name) const {
  auto iter = fpMap_.find(name);
  return (iter == fpMap_.end()) ? nullptr : iter->second;
}

void FailPointRegistry::disableAll() {
  for (auto& [_, fp] : fpMap_) {
    if (fp) {
      fp->deactivate();
    }
  }
}

}  // namespace alisql

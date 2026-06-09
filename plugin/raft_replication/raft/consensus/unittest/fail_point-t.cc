// Portions Copyright (c) 2023 ApeCloud, Inc.

#include <gtest/gtest.h>

#include "fail_point.h"

using namespace alisql;

TEST(FailPoint, InitialState) {
  FailPoint failPoint("test");
  ASSERT_FALSE(failPoint.evaluateFail());
}

TEST(FailPoint, AlwaysOn) {
  FailPoint failPoint("alwaysOn");
  failPoint.activate(FailPoint::alwaysOn);
  for (size_t x = 0; x < 50; x++) {
    ASSERT_TRUE(failPoint.evaluateFail());
  }
}

TEST(FailPoint, FiniteTimes) {
  FailPoint failPoint("finiteTimes");
  failPoint.activate(FailPoint::finiteTimes, 4);
  ASSERT_TRUE(failPoint.evaluateFail());
  ASSERT_TRUE(failPoint.evaluateFail());
  ASSERT_TRUE(failPoint.evaluateFail());
  ASSERT_TRUE(failPoint.evaluateFail());

  for (size_t x = 0; x < 50; x++) {
    ASSERT_FALSE(failPoint.evaluateFail());
  }
}

TEST(FailPoint, FiniteSkip) {
  FailPoint failPoint("Skip");
  failPoint.activate(FailPoint::finiteSkip, 4);
  ASSERT_FALSE(failPoint.evaluateFail());
  ASSERT_FALSE(failPoint.evaluateFail());
  ASSERT_FALSE(failPoint.evaluateFail());
  ASSERT_FALSE(failPoint.evaluateFail());

  for (size_t x = 0; x < 50; x++) {
    ASSERT_TRUE(failPoint.evaluateFail());
  }
}

TEST(FailPoint, ChangeType) {
  FailPoint failPoint("ChangeType");
  failPoint.activate(FailPoint::alwaysOn);
  for (size_t x = 0; x < 50; x++) {
    ASSERT_TRUE(failPoint.evaluateFail());
  }

  failPoint.changeType(FailPoint::finiteTimes, 5);
  ASSERT_TRUE(failPoint.evaluateFail());
  ASSERT_TRUE(failPoint.evaluateFail());
  ASSERT_TRUE(failPoint.evaluateFail());
  ASSERT_TRUE(failPoint.evaluateFail());
  ASSERT_TRUE(failPoint.evaluateFail());
  for (size_t x = 0; x < 50; x++) {
    ASSERT_FALSE(failPoint.evaluateFail());
  }

  failPoint.changeType(FailPoint::finiteSkip, 5);
  ASSERT_FALSE(failPoint.evaluateFail());
  ASSERT_FALSE(failPoint.evaluateFail());
  ASSERT_FALSE(failPoint.evaluateFail());
  ASSERT_FALSE(failPoint.evaluateFail());
  ASSERT_FALSE(failPoint.evaluateFail());

  for (size_t x = 0; x < 50; x++) {
    ASSERT_TRUE(failPoint.evaluateFail());
  }

  failPoint.deactivate();
  for (size_t x = 0; x < 50; x++) {
    ASSERT_FALSE(failPoint.evaluateFail());
  }
}

TEST(FailPoint, DefaultOff) {
  FailPoint failPoint("BlockOff");
  bool called = false;
  // the default type is off, so the callback should not be called.
  failPoint.inject([&]() { called = true; });
  ASSERT_FALSE(called);
}

TEST(FailPoint, AlwaysOnInject) {
  FailPoint failPoint("AlwaysOn");
  failPoint.activate(FailPoint::alwaysOn);
  bool called = false;
  failPoint.inject([&]() { called = true; });
  ASSERT_TRUE(called);
}

TEST(FailPoint, FiniteTimesInject) {
  FailPoint failPoint("NTimes");
  failPoint.activate(FailPoint::finiteTimes, 1);
  size_t counter = 0;
  for (size_t x = 0; x < 10; x++) {
    failPoint.inject([&]() { counter++; });
  }
  ASSERT_EQ(1U, counter);
}

TEST(FailPoint, FiniteSkipInject) {
  FailPoint failPoint("Skip");
  failPoint.activate(FailPoint::finiteSkip, 1);
  size_t counter = 0;
  for (size_t x = 0; x < 10; x++) {
    failPoint.inject([&]() { counter++; });
  }
  ASSERT_EQ(9U, counter);
}

TEST(FailPoint, ChangeTypeInject) {
  FailPoint failPoint("ChangeTypeInject");
  failPoint.activate(FailPoint::alwaysOn);
  size_t counter = 0;
  for (size_t x = 0; x < 10; x++) {
    failPoint.inject([&]() { counter++; });
  }
  ASSERT_EQ(10U, counter);

  failPoint.changeType(FailPoint::finiteTimes, 5);
  counter = 0;
  for (size_t x = 0; x < 10; x++) {
    failPoint.inject([&]() { counter++; });
  }
  ASSERT_EQ(5U, counter);

  failPoint.changeType(FailPoint::finiteSkip, 5);
  counter = 0;
  for (size_t x = 0; x < 20; x++) {
    failPoint.inject([&]() { counter++; });
  }
  ASSERT_EQ(15U, counter);

  failPoint.deactivate();
  counter = 0;
  for (size_t x = 0; x < 10; x++) {
    failPoint.inject([&]() { counter++; });
  }
  ASSERT_EQ(0U, counter);
}

TEST(FailPoint, failIfBasicTest) {
  FailPoint failPoint("ShouldFailIfBasicTest");
  failPoint.activate(FailPoint::finiteTimes, 1);
  {
    bool hit = false;
    // should not fail because the condition returns false.
    ASSERT_FALSE(failPoint.evaluateFailIf([&hit](FailPointData& data) {
      hit = true;
      return false;
    }));
    ASSERT_TRUE(hit);
  }
  {
    bool hit = false;
    // should fail because the condition returns true and finiteTimes is
    // satisfied.
    ASSERT_TRUE(failPoint.evaluateFailIf([&hit](FailPointData& data) {
      hit = true;
      return true;
    }));
    ASSERT_TRUE(hit);
  }
  {
    bool hit = false;
    // should not fail because it has already failed for n times
    // even the condition returns true.
    ASSERT_FALSE(failPoint.evaluateFailIf([&hit](FailPointData& data) {
      hit = true;
      return true;
    }));
    ASSERT_TRUE(hit);
  }
}

TEST(FailPoint, injectFirstIfSecond) {
  FailPoint failPoint("injectFirstIfSecond");
  failPoint.activate(FailPoint::finiteTimes, 1);
  {
    bool hit = false;
    // should not excute because the condition returns false.
    failPoint.injectFirstIfSecond([]() { ASSERT_TRUE(!"shouldn't get here"); },
                                  [&hit](FailPointData& data) {
                                    hit = true;
                                    return false;
                                  });
    ASSERT_TRUE(hit);
  }
  {
    bool hit = false;
    // should inject because the condition returns true and finiteTimes is
    // satisfied.
    failPoint.injectFirstIfSecond([&hit]() { hit = true; },
                                  [](FailPointData& data) { return true; });
    ASSERT_TRUE(hit);
  }
  {
    bool hit = false;
    // should not inject because it has already failed for n times
    // even the condition returns true.
    failPoint.injectFirstIfSecond([]() { ASSERT_TRUE(!"shouldn't get here"); },
                                  [&hit](FailPointData& data) {
                                    hit = true;
                                    return true;
                                  });
    ASSERT_TRUE(hit);
  }
}

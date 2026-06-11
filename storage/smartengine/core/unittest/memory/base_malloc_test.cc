#include "memory/base_malloc.h"
#include "util/testharness.h"
#include <vector>

namespace smartengine
{
namespace memory
{

static const int64_t PTR_COUNT = 100;
static int64_t instance_counter = 0;

class InterfaceClass
{
public:
  InterfaceClass() { ++instance_counter; }
  virtual ~InterfaceClass() { --instance_counter; }

  DEFINE_PURE_VIRTUAL_CONSTRUCTOR_SIZE()
};

class BaseClass : public InterfaceClass
{
public:
  BaseClass() : base_value_(0) { ++instance_counter; }
  BaseClass(int64_t base_value) : base_value_(base_value) { ++instance_counter; }
  virtual ~BaseClass() { --instance_counter; }

  DEFINE_VIRTUAL_CONSTRUCTOR_SIZE()

public:
  uint64_t base_value_;
};

class DerivedClass : public BaseClass
{
public:
  DerivedClass() : derived_value_(0) { ++instance_counter; }
  DerivedClass(int64_t base_value, int64_t derived_value)
      : BaseClass(base_value),
        derived_value_(derived_value)
  {
    ++instance_counter;
  }
  virtual ~DerivedClass() override { --instance_counter; }

  DEFINE_OVERRIDE_CONSTRUCTOR_SIZE()

public:
  uint64_t derived_value_;
};

TEST(NewObject, base_class)
{
  int64_t hold_size = 0;
  int64_t expected_instance_counter = 0;
  BaseClass *base_ptr = nullptr;
  std::vector<BaseClass *> base_ptr_vec;

  for (int64_t i = 0; i < PTR_COUNT; ++i) {
    base_ptr = NEW_OBJECT(ModId::kDefaultMod, BaseClass);
    ASSERT_TRUE(nullptr != base_ptr);
    hold_size += sizeof(BaseClass);
    expected_instance_counter += 2;
    ASSERT_EQ(hold_size, AllocMgr::get_instance()->get_hold_size(ModId::kDefaultMod));
    ASSERT_EQ(expected_instance_counter, instance_counter);

    base_ptr_vec.push_back(base_ptr);
  }

  for (int64_t i = 0; i < PTR_COUNT; ++i) {
    base_ptr = base_ptr_vec[i];
    DELETE_OBJECT(ModId::kDefaultMod, base_ptr);
    ASSERT_TRUE(nullptr == base_ptr);
    hold_size -= sizeof(BaseClass);
    expected_instance_counter -= 2;
    ASSERT_EQ(hold_size, AllocMgr::get_instance()->get_hold_size(ModId::kDefaultMod));
    ASSERT_EQ(expected_instance_counter, instance_counter);
  }

  ASSERT_EQ(0, expected_instance_counter);
  ASSERT_EQ(0, instance_counter);
}

TEST(NewObject, derived_class)
{
  int64_t hold_size = 0;
  int64_t expected_instance_counter = 0;
  BaseClass *base_ptr_to_derived = nullptr;
  std::vector<BaseClass *> base_ptr_to_derived_vec;

  for (int64_t i = 0; i < PTR_COUNT; ++i) {
    base_ptr_to_derived = NEW_OBJECT(ModId::kDefaultMod, DerivedClass, i, i + 1);
    ASSERT_TRUE(nullptr != base_ptr_to_derived);
    ASSERT_EQ(i, base_ptr_to_derived->base_value_);
    ASSERT_EQ(i + 1, reinterpret_cast<DerivedClass *>(base_ptr_to_derived)->derived_value_);
    hold_size += sizeof(DerivedClass);
    expected_instance_counter += 3;
    ASSERT_EQ(hold_size, AllocMgr::get_instance()->get_hold_size(ModId::kDefaultMod));
    ASSERT_EQ(expected_instance_counter, instance_counter);
  
    base_ptr_to_derived_vec.push_back(base_ptr_to_derived);
  }

  for (int64_t i = 0; i < PTR_COUNT; ++i) {
    base_ptr_to_derived = base_ptr_to_derived_vec[i];
    DELETE_OBJECT(ModId::kDefaultMod, base_ptr_to_derived);
    ASSERT_TRUE(nullptr == base_ptr_to_derived);
    hold_size -= sizeof(DerivedClass);
    expected_instance_counter -= 3;
    ASSERT_EQ(hold_size, AllocMgr::get_instance()->get_hold_size(ModId::kDefaultMod));
    ASSERT_EQ(expected_instance_counter, instance_counter);
  }

  ASSERT_EQ(0, expected_instance_counter);
  ASSERT_EQ(0, instance_counter);
}

TEST(NewObject, mixed_class)
{
  int64_t hold_size = 0;
  int64_t expected_instance_counter = 0;
  BaseClass *mixed_ptr = nullptr;
  std::vector<BaseClass *> mixed_ptr_vec;

  for (int64_t i = 0; i < PTR_COUNT; ++i) {
    if (0 == (i % 2)) {
      mixed_ptr = NEW_OBJECT(ModId::kDefaultMod, BaseClass);
      ASSERT_TRUE(nullptr != mixed_ptr);
      hold_size += sizeof(BaseClass);
      expected_instance_counter += 2;
    } else {
      mixed_ptr = NEW_OBJECT(ModId::kDefaultMod, DerivedClass, i, 2 * i);
      ASSERT_EQ(i, reinterpret_cast<DerivedClass *>(mixed_ptr)->base_value_);
      ASSERT_EQ(2 * i, reinterpret_cast<DerivedClass *>(mixed_ptr)->derived_value_);
      ASSERT_TRUE(nullptr != mixed_ptr);
      hold_size += sizeof(DerivedClass);
      expected_instance_counter += 3;
    }
    ASSERT_EQ(hold_size, AllocMgr::get_instance()->get_hold_size(ModId::kDefaultMod));
    ASSERT_EQ(expected_instance_counter, instance_counter);
  
    mixed_ptr_vec.push_back(mixed_ptr);
  }

  for (int64_t i = 0; i < PTR_COUNT; ++i) {
    mixed_ptr = mixed_ptr_vec[i];
    DELETE_OBJECT(ModId::kDefaultMod, mixed_ptr);
    ASSERT_TRUE(nullptr == mixed_ptr);
    if (0 == (i %2)) {
      hold_size -= sizeof(BaseClass);
      expected_instance_counter -= 2;
    } else {
      hold_size -= sizeof(DerivedClass);
      expected_instance_counter -= 3;
    }
    ASSERT_EQ(hold_size, AllocMgr::get_instance()->get_hold_size(ModId::kDefaultMod));
    ASSERT_EQ(expected_instance_counter, instance_counter);
  }

  ASSERT_EQ(0, instance_counter);
  ASSERT_EQ(0, expected_instance_counter);
}

} // namespace memory
} // namespace smartengine

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
	smartengine::util::test::init_logger(__FILE__);
  return RUN_ALL_TESTS();
}

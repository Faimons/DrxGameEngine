/**
 * DRX Engine - Core Unit Tests
 */

#include <gtest/gtest.h>
#include "Core/Public/CoreTypes.h"

using namespace DRX;

// ============================================================================
// Result Type Tests
// ============================================================================

TEST(ResultTest, SuccessValue) {
    Result<int> result(42);

    EXPECT_TRUE(result.IsOk());
    EXPECT_FALSE(result.IsError());
    EXPECT_EQ(result.Value(), 42);
}

TEST(ResultTest, ErrorValue) {
    Result<int> result = Result<int>::Error("Something went wrong");

    EXPECT_FALSE(result.IsOk());
    EXPECT_TRUE(result.IsError());
    EXPECT_EQ(result.GetError(), "Something went wrong");
}

TEST(ResultTest, ValueOr) {
    Result<int> success(42);
    Result<int> error = Result<int>::Error("error");

    EXPECT_EQ(success.ValueOr(0), 42);
    EXPECT_EQ(error.ValueOr(0), 0);
}

TEST(ResultTest, BoolConversion) {
    Result<int> success(42);
    Result<int> error = Result<int>::Error("error");

    EXPECT_TRUE(static_cast<bool>(success));
    EXPECT_FALSE(static_cast<bool>(error));
}

// ============================================================================
// Handle Tests
// ============================================================================

TEST(HandleTest, DefaultInvalid) {
    EntityHandle handle;
    EXPECT_FALSE(handle.IsValid());
}

TEST(HandleTest, ValidHandle) {
    EntityHandle handle(42);
    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(handle.GetId(), 42u);
}

TEST(HandleTest, Comparison) {
    EntityHandle a(1);
    EntityHandle b(1);
    EntityHandle c(2);

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}

// ============================================================================
// Type Alias Tests
// ============================================================================

TEST(TypeAliasTest, IntegerSizes) {
    EXPECT_EQ(sizeof(int8), 1);
    EXPECT_EQ(sizeof(int16), 2);
    EXPECT_EQ(sizeof(int32), 4);
    EXPECT_EQ(sizeof(int64), 8);

    EXPECT_EQ(sizeof(uint8), 1);
    EXPECT_EQ(sizeof(uint16), 2);
    EXPECT_EQ(sizeof(uint32), 4);
    EXPECT_EQ(sizeof(uint64), 8);
}

TEST(TypeAliasTest, FloatSizes) {
    EXPECT_EQ(sizeof(float32), 4);
    EXPECT_EQ(sizeof(float64), 8);
}

// ============================================================================
// Smart Pointer Tests
// ============================================================================

TEST(SmartPointerTest, MakeUnique) {
    auto ptr = MakeUnique<int>(42);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(*ptr, 42);
}

TEST(SmartPointerTest, MakeShared) {
    auto ptr = MakeShared<int>(42);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(*ptr, 42);
    EXPECT_EQ(ptr.use_count(), 1);
}

// ============================================================================
// Container Alias Tests
// ============================================================================

TEST(ContainerTest, Array) {
    Array<int> arr = {1, 2, 3, 4, 5};
    EXPECT_EQ(arr.size(), 5u);
    EXPECT_EQ(arr[0], 1);
    EXPECT_EQ(arr[4], 5);
}

TEST(ContainerTest, HashMap) {
    HashMap<String, int> map;
    map["one"] = 1;
    map["two"] = 2;

    EXPECT_EQ(map.size(), 2u);
    EXPECT_EQ(map["one"], 1);
    EXPECT_EQ(map["two"], 2);
}

TEST(ContainerTest, Optional) {
    Optional<int> empty;
    Optional<int> value = 42;

    EXPECT_FALSE(empty.has_value());
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), 42);
}

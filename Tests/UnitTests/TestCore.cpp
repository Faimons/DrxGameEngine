/**
 * DRX Engine - Core Unit Tests
 */

#include <gtest/gtest.h>
#include "Core/Public/CoreTypes.h"
#include "Core/Public/Config.h"

#include <cstdio>

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

// ============================================================================
// Config Tests
// ============================================================================


TEST(ConfigTest, ParsesNestedValuesWithCommentsAndTrailingCommas) {
    Config config;
    ASSERT_TRUE(config.LoadFromString(R"({
        // comment
        "player": { "walk_speed": 5.5, "max_health": 100, "name": "Runner", },
        "debug": true,
    })"));
    EXPECT_FLOAT_EQ(config.GetFloat("player.walk_speed", 0.0f), 5.5f);
    EXPECT_EQ(config.GetInt("player.max_health", 0), 100);
    EXPECT_EQ(config.GetString("player.name", ""), "Runner");
    EXPECT_TRUE(config.GetBool("debug", false));
    EXPECT_EQ(config.GetCount(), 4u);
}

TEST(ConfigTest, MissingOrMistypedKeysReturnDefault) {
    Config config;
    ASSERT_TRUE(config.LoadFromString(R"({"a": "text", "b": 2})"));
    EXPECT_EQ(config.GetInt("missing", 7), 7);
    EXPECT_EQ(config.GetInt("a", 3), 3);
    EXPECT_FALSE(config.GetBool("b", false));
    EXPECT_FALSE(config.Has("missing"));
}

TEST(ConfigTest, InvalidJsonKeepsPreviousValues) {
    Config config;
    ASSERT_TRUE(config.LoadFromString(R"({"x": 1})"));
    EXPECT_FALSE(config.LoadFromString(R"({"x": )"));
    EXPECT_FALSE(config.LoadFromString(R"({"x": [1, 2]})"));
    EXPECT_EQ(config.GetInt("x", 0), 1);
}

TEST(ConfigTest, OverridesSurviveReload) {
    Config config;
    config.AddOverride("speed", "9.5");
    config.AddOverride("enabled", "false");
    config.AddOverride("mode", "duel");
    ASSERT_TRUE(config.LoadFromString(R"({"speed": 1, "enabled": true, "other": 3})"));
    EXPECT_FLOAT_EQ(config.GetFloat("speed", 0.0f), 9.5f);
    EXPECT_FALSE(config.GetBool("enabled", true));
    EXPECT_EQ(config.GetString("mode", ""), "duel");
    EXPECT_EQ(config.GetInt("other", 0), 3);
}

TEST(ConfigTest, ReloadCallbackAndSaveRoundTrip) {
    Config config;
    int reloads = 0;
    config.SetReloadCallback([&](const Config&) { ++reloads; });
    ASSERT_TRUE(config.LoadFromString(R"({"a": {"b": 2.5, "c": "x"}, "d": false})"));
    EXPECT_EQ(reloads, 1);

    const String path = testing::TempDir() + "drx_config_roundtrip.json";
    ASSERT_TRUE(config.SaveToFile(path));
    Config loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path));
    EXPECT_FLOAT_EQ(loaded.GetFloat("a.b", 0.0f), 2.5f);
    EXPECT_EQ(loaded.GetString("a.c", ""), "x");
    EXPECT_FALSE(loaded.GetBool("d", true));
    std::remove(path.c_str());
}

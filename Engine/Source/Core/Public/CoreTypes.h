#pragma once

/**
 * DRX Engine - Core Types
 * Fundamental type definitions and utilities
 */

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <optional>
#include <functional>
#include <type_traits>

namespace DRX {

// ============================================================================
// Fundamental Types
// ============================================================================

using int8   = std::int8_t;
using int16  = std::int16_t;
using int32  = std::int32_t;
using int64  = std::int64_t;
using uint8  = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using float32 = float;
using float64 = double;

using byte = uint8;
using size_t = std::size_t;

// ============================================================================
// String Types
// ============================================================================

using String = std::string;
using WString = std::wstring;
using StringView = std::string_view;

// ============================================================================
// Container Types
// ============================================================================

template<typename T>
using Array = std::vector<T>;

template<typename T, size_t N>
using FixedArray = std::array<T, N>;

template<typename K, typename V>
using HashMap = std::unordered_map<K, V>;

template<typename T>
using Optional = std::optional<T>;

template<typename T>
using Function = std::function<T>;

// ============================================================================
// Smart Pointers
// ============================================================================

template<typename T>
using UniquePtr = std::unique_ptr<T>;

template<typename T>
using SharedPtr = std::shared_ptr<T>;

template<typename T>
using WeakPtr = std::weak_ptr<T>;

template<typename T, typename... Args>
inline UniquePtr<T> MakeUnique(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

template<typename T, typename... Args>
inline SharedPtr<T> MakeShared(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}

// ============================================================================
// Result Type (Error Handling)
// ============================================================================

template<typename T, typename E = String>
class Result {
public:
    // Success constructors
    Result(const T& value) : value_(value), has_value_(true) {}
    Result(T&& value) : value_(std::move(value)), has_value_(true) {}

    // Error factory
    static Result Error(const E& error) {
        Result r;
        r.error_ = error;
        r.has_value_ = false;
        return r;
    }

    static Result Error(E&& error) {
        Result r;
        r.error_ = std::move(error);
        r.has_value_ = false;
        return r;
    }

    // State checks
    bool IsOk() const { return has_value_; }
    bool IsError() const { return !has_value_; }
    explicit operator bool() const { return has_value_; }

    // Accessors
    const T& Value() const& { return value_; }
    T& Value() & { return value_; }
    T&& Value() && { return std::move(value_); }

    const E& GetError() const { return error_; }

    // Monadic operations
    template<typename F>
    auto Map(F&& f) -> Result<decltype(f(value_)), E> {
        if (has_value_) {
            return Result<decltype(f(value_)), E>(f(value_));
        }
        return Result<decltype(f(value_)), E>::Error(error_);
    }

    T ValueOr(const T& default_value) const {
        return has_value_ ? value_ : default_value;
    }

private:
    Result() : has_value_(false) {}

    T value_{};
    E error_{};
    bool has_value_;
};

// Void specialization
template<typename E>
class Result<void, E> {
public:
    Result() : has_value_(true) {}

    static Result Ok() { return Result(); }

    static Result Error(const E& error) {
        Result r;
        r.error_ = error;
        r.has_value_ = false;
        return r;
    }

    bool IsOk() const { return has_value_; }
    bool IsError() const { return !has_value_; }
    explicit operator bool() const { return has_value_; }

    const E& GetError() const { return error_; }

private:
    E error_{};
    bool has_value_ = true;
};

// ============================================================================
// Non-copyable Base Class
// ============================================================================

class NonCopyable {
protected:
    NonCopyable() = default;
    ~NonCopyable() = default;

    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;

    NonCopyable(NonCopyable&&) = default;
    NonCopyable& operator=(NonCopyable&&) = default;
};

// ============================================================================
// Type Traits Helpers
// ============================================================================

template<typename T>
struct TypeName {
    static constexpr const char* Get() { return "Unknown"; }
};

#define DRX_DECLARE_TYPE_NAME(Type) \
    template<> struct TypeName<Type> { \
        static constexpr const char* Get() { return #Type; } \
    }

DRX_DECLARE_TYPE_NAME(int8);
DRX_DECLARE_TYPE_NAME(int16);
DRX_DECLARE_TYPE_NAME(int32);
DRX_DECLARE_TYPE_NAME(int64);
DRX_DECLARE_TYPE_NAME(uint8);
DRX_DECLARE_TYPE_NAME(uint16);
DRX_DECLARE_TYPE_NAME(uint32);
DRX_DECLARE_TYPE_NAME(uint64);
DRX_DECLARE_TYPE_NAME(float32);
DRX_DECLARE_TYPE_NAME(float64);
DRX_DECLARE_TYPE_NAME(String);

// ============================================================================
// Handle Type (Type-safe ID)
// ============================================================================

template<typename Tag, typename T = uint32>
class Handle {
public:
    static constexpr T INVALID = static_cast<T>(-1);

    Handle() : id_(INVALID) {}
    explicit Handle(T id) : id_(id) {}

    bool IsValid() const { return id_ != INVALID; }
    T GetId() const { return id_; }

    bool operator==(const Handle& other) const { return id_ == other.id_; }
    bool operator!=(const Handle& other) const { return id_ != other.id_; }
    bool operator<(const Handle& other) const { return id_ < other.id_; }

    static Handle Invalid() { return Handle(); }

private:
    T id_;
};

// Common handle types
struct EntityTag {};
struct MeshTag {};
struct TextureTag {};
struct MaterialTag {};
struct ShaderTag {};

using EntityHandle = Handle<EntityTag>;
using MeshHandle = Handle<MeshTag>;
using TextureHandle = Handle<TextureTag>;
using MaterialHandle = Handle<MaterialTag>;
using ShaderHandle = Handle<ShaderTag>;

} // namespace DRX

// Hash support for handles
namespace std {
    template<typename Tag, typename T>
    struct hash<DRX::Handle<Tag, T>> {
        size_t operator()(const DRX::Handle<Tag, T>& h) const {
            return hash<T>()(h.GetId());
        }
    };
}

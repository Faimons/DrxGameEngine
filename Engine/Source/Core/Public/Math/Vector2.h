#pragma once

/**
 * DRX Engine - 2D Vector
 */

#include "../CoreTypes.h"
#include <cmath>

namespace DRX::Math {

struct Vector2 {
    float32 x, y;

    // Constructors
    Vector2() : x(0), y(0) {}
    Vector2(float32 x, float32 y) : x(x), y(y) {}
    explicit Vector2(float32 scalar) : x(scalar), y(scalar) {}

    // Operators
    Vector2 operator+(const Vector2& other) const {
        return Vector2(x + other.x, y + other.y);
    }

    Vector2 operator-(const Vector2& other) const {
        return Vector2(x - other.x, y - other.y);
    }

    Vector2 operator*(float32 scalar) const {
        return Vector2(x * scalar, y * scalar);
    }

    Vector2 operator*(const Vector2& other) const {
        return Vector2(x * other.x, y * other.y);
    }

    Vector2 operator/(float32 scalar) const {
        return Vector2(x / scalar, y / scalar);
    }

    Vector2 operator-() const {
        return Vector2(-x, -y);
    }

    Vector2& operator+=(const Vector2& other) {
        x += other.x; y += other.y;
        return *this;
    }

    Vector2& operator-=(const Vector2& other) {
        x -= other.x; y -= other.y;
        return *this;
    }

    Vector2& operator*=(float32 scalar) {
        x *= scalar; y *= scalar;
        return *this;
    }

    Vector2& operator/=(float32 scalar) {
        x /= scalar; y /= scalar;
        return *this;
    }

    bool operator==(const Vector2& other) const {
        return x == other.x && y == other.y;
    }

    bool operator!=(const Vector2& other) const {
        return !(*this == other);
    }

    float32& operator[](int index) {
        return (&x)[index];
    }

    const float32& operator[](int index) const {
        return (&x)[index];
    }

    // Static methods
    static float32 Dot(const Vector2& a, const Vector2& b) {
        return a.x * b.x + a.y * b.y;
    }

    static float32 Cross(const Vector2& a, const Vector2& b) {
        return a.x * b.y - a.y * b.x;
    }

    static Vector2 Lerp(const Vector2& a, const Vector2& b, float32 t) {
        return a + (b - a) * t;
    }

    static float32 Distance(const Vector2& a, const Vector2& b) {
        return (b - a).Length();
    }

    static float32 DistanceSquared(const Vector2& a, const Vector2& b) {
        return (b - a).LengthSquared();
    }

    // Instance methods
    float32 Length() const {
        return std::sqrt(x * x + y * y);
    }

    float32 LengthSquared() const {
        return x * x + y * y;
    }

    Vector2 Normalized() const {
        float32 len = Length();
        if (len > 0) {
            return *this / len;
        }
        return Vector2(0, 0);
    }

    void Normalize() {
        float32 len = Length();
        if (len > 0) {
            *this /= len;
        }
    }

    Vector2 Perpendicular() const {
        return Vector2(-y, x);
    }

    // Static constants
    static const Vector2 Zero;
    static const Vector2 One;
    static const Vector2 UnitX;
    static const Vector2 UnitY;
};

inline const Vector2 Vector2::Zero = Vector2(0, 0);
inline const Vector2 Vector2::One = Vector2(1, 1);
inline const Vector2 Vector2::UnitX = Vector2(1, 0);
inline const Vector2 Vector2::UnitY = Vector2(0, 1);

inline Vector2 operator*(float32 scalar, const Vector2& vec) {
    return vec * scalar;
}

} // namespace DRX::Math

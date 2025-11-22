#pragma once

/**
 * DRX Engine - 3D Vector
 */

#include "../CoreTypes.h"
#include <cmath>

namespace DRX::Math {

struct Vector3 {
    float32 x, y, z;

    // Constructors
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float32 x, float32 y, float32 z) : x(x), y(y), z(z) {}
    explicit Vector3(float32 scalar) : x(scalar), y(scalar), z(scalar) {}

    // Operators
    Vector3 operator+(const Vector3& other) const {
        return Vector3(x + other.x, y + other.y, z + other.z);
    }

    Vector3 operator-(const Vector3& other) const {
        return Vector3(x - other.x, y - other.y, z - other.z);
    }

    Vector3 operator*(float32 scalar) const {
        return Vector3(x * scalar, y * scalar, z * scalar);
    }

    Vector3 operator*(const Vector3& other) const {
        return Vector3(x * other.x, y * other.y, z * other.z);
    }

    Vector3 operator/(float32 scalar) const {
        return Vector3(x / scalar, y / scalar, z / scalar);
    }

    Vector3 operator-() const {
        return Vector3(-x, -y, -z);
    }

    Vector3& operator+=(const Vector3& other) {
        x += other.x; y += other.y; z += other.z;
        return *this;
    }

    Vector3& operator-=(const Vector3& other) {
        x -= other.x; y -= other.y; z -= other.z;
        return *this;
    }

    Vector3& operator*=(float32 scalar) {
        x *= scalar; y *= scalar; z *= scalar;
        return *this;
    }

    Vector3& operator/=(float32 scalar) {
        x /= scalar; y /= scalar; z /= scalar;
        return *this;
    }

    bool operator==(const Vector3& other) const {
        return x == other.x && y == other.y && z == other.z;
    }

    bool operator!=(const Vector3& other) const {
        return !(*this == other);
    }

    float32& operator[](int index) {
        return (&x)[index];
    }

    const float32& operator[](int index) const {
        return (&x)[index];
    }

    // Static methods
    static float32 Dot(const Vector3& a, const Vector3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static Vector3 Cross(const Vector3& a, const Vector3& b) {
        return Vector3(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        );
    }

    static Vector3 Lerp(const Vector3& a, const Vector3& b, float32 t) {
        return a + (b - a) * t;
    }

    static float32 Distance(const Vector3& a, const Vector3& b) {
        return (b - a).Length();
    }

    static float32 DistanceSquared(const Vector3& a, const Vector3& b) {
        return (b - a).LengthSquared();
    }

    static Vector3 Reflect(const Vector3& v, const Vector3& normal) {
        return v - normal * 2.0f * Dot(v, normal);
    }

    static Vector3 Min(const Vector3& a, const Vector3& b) {
        return Vector3(
            std::fmin(a.x, b.x),
            std::fmin(a.y, b.y),
            std::fmin(a.z, b.z)
        );
    }

    static Vector3 Max(const Vector3& a, const Vector3& b) {
        return Vector3(
            std::fmax(a.x, b.x),
            std::fmax(a.y, b.y),
            std::fmax(a.z, b.z)
        );
    }

    // Instance methods
    float32 Length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    float32 LengthSquared() const {
        return x * x + y * y + z * z;
    }

    Vector3 Normalized() const {
        float32 len = Length();
        if (len > 0) {
            return *this / len;
        }
        return Vector3(0, 0, 0);
    }

    void Normalize() {
        float32 len = Length();
        if (len > 0) {
            *this /= len;
        }
    }

    // Static constants
    static const Vector3 Zero;
    static const Vector3 One;
    static const Vector3 Up;
    static const Vector3 Down;
    static const Vector3 Right;
    static const Vector3 Left;
    static const Vector3 Forward;
    static const Vector3 Back;
};

inline const Vector3 Vector3::Zero = Vector3(0, 0, 0);
inline const Vector3 Vector3::One = Vector3(1, 1, 1);
inline const Vector3 Vector3::Up = Vector3(0, 1, 0);
inline const Vector3 Vector3::Down = Vector3(0, -1, 0);
inline const Vector3 Vector3::Right = Vector3(1, 0, 0);
inline const Vector3 Vector3::Left = Vector3(-1, 0, 0);
inline const Vector3 Vector3::Forward = Vector3(0, 0, -1);
inline const Vector3 Vector3::Back = Vector3(0, 0, 1);

inline Vector3 operator*(float32 scalar, const Vector3& vec) {
    return vec * scalar;
}

} // namespace DRX::Math

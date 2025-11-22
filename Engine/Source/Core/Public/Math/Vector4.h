#pragma once

/**
 * DRX Engine - 4D Vector
 */

#include "../CoreTypes.h"
#include "Vector3.h"
#include <cmath>

namespace DRX::Math {

struct Vector4 {
    float32 x, y, z, w;

    // Constructors
    Vector4() : x(0), y(0), z(0), w(0) {}
    Vector4(float32 x, float32 y, float32 z, float32 w) : x(x), y(y), z(z), w(w) {}
    explicit Vector4(float32 scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}
    Vector4(const Vector3& v, float32 w) : x(v.x), y(v.y), z(v.z), w(w) {}

    // Operators
    Vector4 operator+(const Vector4& other) const {
        return Vector4(x + other.x, y + other.y, z + other.z, w + other.w);
    }

    Vector4 operator-(const Vector4& other) const {
        return Vector4(x - other.x, y - other.y, z - other.z, w - other.w);
    }

    Vector4 operator*(float32 scalar) const {
        return Vector4(x * scalar, y * scalar, z * scalar, w * scalar);
    }

    Vector4 operator*(const Vector4& other) const {
        return Vector4(x * other.x, y * other.y, z * other.z, w * other.w);
    }

    Vector4 operator/(float32 scalar) const {
        return Vector4(x / scalar, y / scalar, z / scalar, w / scalar);
    }

    Vector4 operator-() const {
        return Vector4(-x, -y, -z, -w);
    }

    Vector4& operator+=(const Vector4& other) {
        x += other.x; y += other.y; z += other.z; w += other.w;
        return *this;
    }

    Vector4& operator-=(const Vector4& other) {
        x -= other.x; y -= other.y; z -= other.z; w -= other.w;
        return *this;
    }

    Vector4& operator*=(float32 scalar) {
        x *= scalar; y *= scalar; z *= scalar; w *= scalar;
        return *this;
    }

    Vector4& operator/=(float32 scalar) {
        x /= scalar; y /= scalar; z /= scalar; w /= scalar;
        return *this;
    }

    bool operator==(const Vector4& other) const {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }

    bool operator!=(const Vector4& other) const {
        return !(*this == other);
    }

    float32& operator[](int index) {
        return (&x)[index];
    }

    const float32& operator[](int index) const {
        return (&x)[index];
    }

    // Static methods
    static float32 Dot(const Vector4& a, const Vector4& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    static Vector4 Lerp(const Vector4& a, const Vector4& b, float32 t) {
        return a + (b - a) * t;
    }

    // Instance methods
    float32 Length() const {
        return std::sqrt(x * x + y * y + z * z + w * w);
    }

    float32 LengthSquared() const {
        return x * x + y * y + z * z + w * w;
    }

    Vector4 Normalized() const {
        float32 len = Length();
        if (len > 0) {
            return *this / len;
        }
        return Vector4(0, 0, 0, 0);
    }

    void Normalize() {
        float32 len = Length();
        if (len > 0) {
            *this /= len;
        }
    }

    Vector3 XYZ() const {
        return Vector3(x, y, z);
    }

    // Static constants
    static const Vector4 Zero;
    static const Vector4 One;
};

inline const Vector4 Vector4::Zero = Vector4(0, 0, 0, 0);
inline const Vector4 Vector4::One = Vector4(1, 1, 1, 1);

inline Vector4 operator*(float32 scalar, const Vector4& vec) {
    return vec * scalar;
}

} // namespace DRX::Math

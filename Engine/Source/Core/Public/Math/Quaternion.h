#pragma once

/**
 * DRX Engine - Quaternion
 * For rotation representation
 */

#include "../CoreTypes.h"
#include "Vector3.h"
#include "Matrix4.h"
#include <cmath>

namespace DRX::Math {

struct Quaternion {
    float32 x, y, z, w;

    // Constructors
    Quaternion() : x(0), y(0), z(0), w(1) {}
    Quaternion(float32 x, float32 y, float32 z, float32 w) : x(x), y(y), z(z), w(w) {}

    // Operators
    Quaternion operator*(const Quaternion& other) const {
        return Quaternion(
            w * other.x + x * other.w + y * other.z - z * other.y,
            w * other.y - x * other.z + y * other.w + z * other.x,
            w * other.z + x * other.y - y * other.x + z * other.w,
            w * other.w - x * other.x - y * other.y - z * other.z
        );
    }

    Quaternion operator*(float32 scalar) const {
        return Quaternion(x * scalar, y * scalar, z * scalar, w * scalar);
    }

    Quaternion operator+(const Quaternion& other) const {
        return Quaternion(x + other.x, y + other.y, z + other.z, w + other.w);
    }

    Quaternion operator-() const {
        return Quaternion(-x, -y, -z, -w);
    }

    bool operator==(const Quaternion& other) const {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }

    bool operator!=(const Quaternion& other) const {
        return !(*this == other);
    }

    // Rotate vector by quaternion
    Vector3 operator*(const Vector3& v) const {
        Vector3 qv(x, y, z);
        Vector3 uv = Vector3::Cross(qv, v);
        Vector3 uuv = Vector3::Cross(qv, uv);
        return v + (uv * w + uuv) * 2.0f;
    }

    // Static factory methods
    static Quaternion Identity() {
        return Quaternion(0, 0, 0, 1);
    }

    static Quaternion FromAxisAngle(const Vector3& axis, float32 radians) {
        float32 halfAngle = radians * 0.5f;
        float32 s = std::sin(halfAngle);
        Vector3 n = axis.Normalized();
        return Quaternion(n.x * s, n.y * s, n.z * s, std::cos(halfAngle));
    }

    static Quaternion FromEuler(float32 pitch, float32 yaw, float32 roll) {
        float32 cp = std::cos(pitch * 0.5f);
        float32 sp = std::sin(pitch * 0.5f);
        float32 cy = std::cos(yaw * 0.5f);
        float32 sy = std::sin(yaw * 0.5f);
        float32 cr = std::cos(roll * 0.5f);
        float32 sr = std::sin(roll * 0.5f);

        return Quaternion(
            sr * cp * cy - cr * sp * sy,
            cr * sp * cy + sr * cp * sy,
            cr * cp * sy - sr * sp * cy,
            cr * cp * cy + sr * sp * sy
        );
    }

    static Quaternion FromEuler(const Vector3& euler) {
        return FromEuler(euler.x, euler.y, euler.z);
    }

    static float32 Dot(const Quaternion& a, const Quaternion& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float32 t) {
        float32 dot = Dot(a, b);

        Quaternion b2 = b;
        if (dot < 0) {
            dot = -dot;
            b2 = -b;
        }

        if (dot > 0.9995f) {
            // Linear interpolation for very close quaternions
            Quaternion result = a + (b2 + (-a)) * t;
            return result.Normalized();
        }

        float32 theta0 = std::acos(dot);
        float32 theta = theta0 * t;
        float32 sinTheta = std::sin(theta);
        float32 sinTheta0 = std::sin(theta0);

        float32 s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
        float32 s1 = sinTheta / sinTheta0;

        return Quaternion(
            a.x * s0 + b2.x * s1,
            a.y * s0 + b2.y * s1,
            a.z * s0 + b2.z * s1,
            a.w * s0 + b2.w * s1
        );
    }

    // Instance methods
    float32 Length() const {
        return std::sqrt(x * x + y * y + z * z + w * w);
    }

    float32 LengthSquared() const {
        return x * x + y * y + z * z + w * w;
    }

    Quaternion Normalized() const {
        float32 len = Length();
        if (len > 0) {
            float32 invLen = 1.0f / len;
            return Quaternion(x * invLen, y * invLen, z * invLen, w * invLen);
        }
        return Identity();
    }

    void Normalize() {
        *this = Normalized();
    }

    Quaternion Conjugate() const {
        return Quaternion(-x, -y, -z, w);
    }

    Quaternion Inverse() const {
        float32 lenSq = LengthSquared();
        if (lenSq > 0) {
            float32 invLenSq = 1.0f / lenSq;
            return Quaternion(-x * invLenSq, -y * invLenSq, -z * invLenSq, w * invLenSq);
        }
        return Identity();
    }

    Vector3 ToEuler() const {
        Vector3 euler;

        // Roll (x-axis rotation)
        float32 sinr_cosp = 2.0f * (w * x + y * z);
        float32 cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
        euler.x = std::atan2(sinr_cosp, cosr_cosp);

        // Pitch (y-axis rotation)
        float32 sinp = 2.0f * (w * y - z * x);
        if (std::abs(sinp) >= 1.0f) {
            euler.y = std::copysign(3.14159265358979323846f * 0.5f, sinp);
        } else {
            euler.y = std::asin(sinp);
        }

        // Yaw (z-axis rotation)
        float32 siny_cosp = 2.0f * (w * z + x * y);
        float32 cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
        euler.z = std::atan2(siny_cosp, cosy_cosp);

        return euler;
    }

    Matrix4 ToMatrix() const {
        Matrix4 result(1.0f);

        float32 xx = x * x;
        float32 yy = y * y;
        float32 zz = z * z;
        float32 xy = x * y;
        float32 xz = x * z;
        float32 yz = y * z;
        float32 wx = w * x;
        float32 wy = w * y;
        float32 wz = w * z;

        result[0][0] = 1.0f - 2.0f * (yy + zz);
        result[0][1] = 2.0f * (xy + wz);
        result[0][2] = 2.0f * (xz - wy);

        result[1][0] = 2.0f * (xy - wz);
        result[1][1] = 1.0f - 2.0f * (xx + zz);
        result[1][2] = 2.0f * (yz + wx);

        result[2][0] = 2.0f * (xz + wy);
        result[2][1] = 2.0f * (yz - wx);
        result[2][2] = 1.0f - 2.0f * (xx + yy);

        return result;
    }

    Vector3 GetForward() const {
        return *this * Vector3::Forward;
    }

    Vector3 GetRight() const {
        return *this * Vector3::Right;
    }

    Vector3 GetUp() const {
        return *this * Vector3::Up;
    }
};

} // namespace DRX::Math

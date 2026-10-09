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

    /**
     * Build rotation from Euler angles in radians (Y-up, applied yaw -> pitch -> roll).
     * pitch rotates around X, yaw around Y, roll around Z.
     */
    static Quaternion FromEuler(float32 pitch, float32 yaw, float32 roll) {
        const float32 cp = std::cos(pitch * 0.5f);
        const float32 sp = std::sin(pitch * 0.5f);
        const float32 cy = std::cos(yaw * 0.5f);
        const float32 sy = std::sin(yaw * 0.5f);
        const float32 cr = std::cos(roll * 0.5f);
        const float32 sr = std::sin(roll * 0.5f);

        return Quaternion(
            cy * sp * cr + sy * cp * sr,
            sy * cp * cr - cy * sp * sr,
            cy * cp * sr - sy * sp * cr,
            cy * cp * cr + sy * sp * sr
        );
    }

    static Quaternion FromEuler(const Vector3& euler) {
        return FromEuler(euler.x, euler.y, euler.z);
    }

    /**
     * Rotation from a rotation matrix (upper 3x3, column-major, orthonormal).
     */
    static Quaternion FromRotationMatrix(const Matrix4& mat) {
        // r(row, col) = mat[col][row]
        const float32 r00 = mat[0][0], r01 = mat[1][0], r02 = mat[2][0];
        const float32 r10 = mat[0][1], r11 = mat[1][1], r12 = mat[2][1];
        const float32 r20 = mat[0][2], r21 = mat[1][2], r22 = mat[2][2];
        const float32 trace = r00 + r11 + r22;

        Quaternion q;
        if (trace > 0.0f) {
            const float32 s = std::sqrt(trace + 1.0f) * 2.0f;
            q = Quaternion((r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, 0.25f * s);
        } else if (r00 > r11 && r00 > r22) {
            const float32 s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
            q = Quaternion(0.25f * s, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s);
        } else if (r11 > r22) {
            const float32 s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
            q = Quaternion((r01 + r10) / s, 0.25f * s, (r12 + r21) / s, (r02 - r20) / s);
        } else {
            const float32 s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
            q = Quaternion((r02 + r20) / s, (r12 + r21) / s, 0.25f * s, (r10 - r01) / s);
        }
        return q.Normalized();
    }

    /**
     * Rotation whose forward axis (-Z) points along `forward` with `up` as reference.
     */
    static Quaternion LookRotation(const Vector3& forward, const Vector3& up = Vector3::Up) {
        const Vector3 f = forward.Normalized();
        if (f.LengthSquared() < 1e-12f) {
            return Identity();
        }

        Vector3 r = Vector3::Cross(f, up);
        if (r.LengthSquared() < 1e-12f) {
            // forward parallel to up: pick any perpendicular reference
            r = Vector3::Cross(f, std::abs(f.z) < 0.9f ? Vector3::Back : Vector3::Right);
        }
        r.Normalize();
        const Vector3 u = Vector3::Cross(r, f);

        Matrix4 basis(1.0f);
        basis[0][0] = r.x;  basis[0][1] = r.y;  basis[0][2] = r.z;
        basis[1][0] = u.x;  basis[1][1] = u.y;  basis[1][2] = u.z;
        basis[2][0] = -f.x; basis[2][1] = -f.y; basis[2][2] = -f.z;
        return FromRotationMatrix(basis);
    }

    /**
     * Shortest rotation that turns direction `from` into direction `to`.
     */
    static Quaternion FromToRotation(const Vector3& from, const Vector3& to) {
        const Vector3 a = from.Normalized();
        const Vector3 b = to.Normalized();
        const float32 d = Vector3::Dot(a, b);

        if (d >= 1.0f - 1e-6f) {
            return Identity();
        }
        if (d <= -1.0f + 1e-6f) {
            Vector3 axis = Vector3::Cross(Vector3::Right, a);
            if (axis.LengthSquared() < 1e-6f) {
                axis = Vector3::Cross(Vector3::Up, a);
            }
            return FromAxisAngle(axis, 3.14159265358979323846f);
        }

        const Vector3 c = Vector3::Cross(a, b);
        const float32 s = std::sqrt((1.0f + d) * 2.0f);
        const float32 inv_s = 1.0f / s;
        return Quaternion(c.x * inv_s, c.y * inv_s, c.z * inv_s, s * 0.5f).Normalized();
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

    /**
     * Euler angles in radians as (pitch, yaw, roll), inverse of FromEuler.
     */
    Vector3 ToEuler() const {
        constexpr float32 half_pi = 1.57079632679489661923f;
        const float32 sin_pitch = 2.0f * (w * x - y * z);

        Vector3 euler;
        if (std::abs(sin_pitch) >= 0.99999f) {
            // Gimbal lock: fold roll into yaw
            euler.x = std::copysign(half_pi, sin_pitch);
            euler.y = std::atan2(-2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z));
            euler.z = 0.0f;
        } else {
            euler.x = std::asin(sin_pitch);
            euler.y = std::atan2(2.0f * (x * z + w * y), 1.0f - 2.0f * (x * x + y * y));
            euler.z = std::atan2(2.0f * (x * y + w * z), 1.0f - 2.0f * (x * x + z * z));
        }
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

    Vector3 Rotate(const Vector3& v) const {
        return *this * v;
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

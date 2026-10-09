#pragma once

/**
 * DRX Engine - 4x4 Matrix
 * Column-major order (OpenGL/Vulkan convention)
 */

#include "../CoreTypes.h"
#include "Vector3.h"
#include "Vector4.h"
#include <cmath>
#include <cstring>

namespace DRX::Math {

struct Matrix4 {
    // Column-major: m[col][row]
    float32 m[4][4];

    // Constructors
    Matrix4() {
        std::memset(m, 0, sizeof(m));
    }

    Matrix4(float32 diagonal) {
        std::memset(m, 0, sizeof(m));
        m[0][0] = diagonal;
        m[1][1] = diagonal;
        m[2][2] = diagonal;
        m[3][3] = diagonal;
    }

    // Access
    float32* operator[](int col) { return m[col]; }
    const float32* operator[](int col) const { return m[col]; }

    Vector4 GetColumn(int col) const {
        return Vector4(m[col][0], m[col][1], m[col][2], m[col][3]);
    }

    Vector4 GetRow(int row) const {
        return Vector4(m[0][row], m[1][row], m[2][row], m[3][row]);
    }

    void SetColumn(int col, const Vector4& v) {
        m[col][0] = v.x;
        m[col][1] = v.y;
        m[col][2] = v.z;
        m[col][3] = v.w;
    }

    // Operators
    Matrix4 operator*(const Matrix4& other) const {
        Matrix4 result;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                result.m[col][row] =
                    m[0][row] * other.m[col][0] +
                    m[1][row] * other.m[col][1] +
                    m[2][row] * other.m[col][2] +
                    m[3][row] * other.m[col][3];
            }
        }
        return result;
    }

    Vector4 operator*(const Vector4& v) const {
        return Vector4(
            m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z + m[3][0] * v.w,
            m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z + m[3][1] * v.w,
            m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z + m[3][2] * v.w,
            m[0][3] * v.x + m[1][3] * v.y + m[2][3] * v.z + m[3][3] * v.w
        );
    }

    Vector3 TransformPoint(const Vector3& v) const {
        Vector4 result = *this * Vector4(v, 1.0f);
        return result.XYZ() / result.w;
    }

    Vector3 TransformVector(const Vector3& v) const {
        Vector4 result = *this * Vector4(v, 0.0f);
        return result.XYZ();
    }

    // Static factory methods
    static Matrix4 Identity() {
        return Matrix4(1.0f);
    }

    static Matrix4 Translation(const Vector3& t) {
        Matrix4 result(1.0f);
        result.m[3][0] = t.x;
        result.m[3][1] = t.y;
        result.m[3][2] = t.z;
        return result;
    }

    static Matrix4 Translation(float32 x, float32 y, float32 z) {
        return Translation(Vector3(x, y, z));
    }

    static Matrix4 Scale(const Vector3& s) {
        Matrix4 result(1.0f);
        result.m[0][0] = s.x;
        result.m[1][1] = s.y;
        result.m[2][2] = s.z;
        return result;
    }

    static Matrix4 Scale(float32 s) {
        return Scale(Vector3(s, s, s));
    }

    static Matrix4 RotationX(float32 radians) {
        Matrix4 result(1.0f);
        float32 c = std::cos(radians);
        float32 s = std::sin(radians);
        result.m[1][1] = c;
        result.m[1][2] = s;
        result.m[2][1] = -s;
        result.m[2][2] = c;
        return result;
    }

    static Matrix4 RotationY(float32 radians) {
        Matrix4 result(1.0f);
        float32 c = std::cos(radians);
        float32 s = std::sin(radians);
        result.m[0][0] = c;
        result.m[0][2] = -s;
        result.m[2][0] = s;
        result.m[2][2] = c;
        return result;
    }

    static Matrix4 RotationZ(float32 radians) {
        Matrix4 result(1.0f);
        float32 c = std::cos(radians);
        float32 s = std::sin(radians);
        result.m[0][0] = c;
        result.m[0][1] = s;
        result.m[1][0] = -s;
        result.m[1][1] = c;
        return result;
    }

    static Matrix4 LookAt(const Vector3& eye, const Vector3& target, const Vector3& up) {
        Vector3 f = (target - eye).Normalized();
        Vector3 r = Vector3::Cross(f, up).Normalized();
        Vector3 u = Vector3::Cross(r, f);

        Matrix4 result(1.0f);
        result.m[0][0] = r.x;
        result.m[1][0] = r.y;
        result.m[2][0] = r.z;
        result.m[0][1] = u.x;
        result.m[1][1] = u.y;
        result.m[2][1] = u.z;
        result.m[0][2] = -f.x;
        result.m[1][2] = -f.y;
        result.m[2][2] = -f.z;
        result.m[3][0] = -Vector3::Dot(r, eye);
        result.m[3][1] = -Vector3::Dot(u, eye);
        result.m[3][2] = Vector3::Dot(f, eye);
        return result;
    }

    static Matrix4 Perspective(float32 fovY, float32 aspect, float32 nearZ, float32 farZ) {
        float32 tanHalfFov = std::tan(fovY * 0.5f);

        Matrix4 result;
        result.m[0][0] = 1.0f / (aspect * tanHalfFov);
        result.m[1][1] = 1.0f / tanHalfFov;
        result.m[2][2] = -(farZ + nearZ) / (farZ - nearZ);
        result.m[2][3] = -1.0f;
        result.m[3][2] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
        return result;
    }

    static Matrix4 Orthographic(float32 left, float32 right, float32 bottom, float32 top, float32 nearZ, float32 farZ) {
        Matrix4 result(1.0f);
        result.m[0][0] = 2.0f / (right - left);
        result.m[1][1] = 2.0f / (top - bottom);
        result.m[2][2] = -2.0f / (farZ - nearZ);
        result.m[3][0] = -(right + left) / (right - left);
        result.m[3][1] = -(top + bottom) / (top - bottom);
        result.m[3][2] = -(farZ + nearZ) / (farZ - nearZ);
        return result;
    }

    /**
     * Vulkan clip space: right-handed view, Y down, depth in [0, 1].
     */
    static Matrix4 PerspectiveVulkan(float32 fovY, float32 aspect, float32 nearZ, float32 farZ) {
        const float32 f = 1.0f / std::tan(fovY * 0.5f);

        Matrix4 result;
        result.m[0][0] = f / aspect;
        result.m[1][1] = -f;
        result.m[2][2] = farZ / (nearZ - farZ);
        result.m[2][3] = -1.0f;
        result.m[3][2] = (nearZ * farZ) / (nearZ - farZ);
        return result;
    }

    static Matrix4 OrthographicVulkan(float32 left, float32 right, float32 bottom, float32 top, float32 nearZ, float32 farZ) {
        Matrix4 result(1.0f);
        result.m[0][0] = 2.0f / (right - left);
        result.m[1][1] = -2.0f / (top - bottom);
        result.m[2][2] = 1.0f / (nearZ - farZ);
        result.m[3][0] = -(right + left) / (right - left);
        result.m[3][1] = (top + bottom) / (top - bottom);
        result.m[3][2] = nearZ / (nearZ - farZ);
        return result;
    }

    // Instance methods
    Matrix4 Transposed() const {
        Matrix4 result;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                result.m[i][j] = m[j][i];
            }
        }
        return result;
    }

    float32 Determinant() const {
        const float32* a = Data();
        const float32 b00 = a[0] * a[5] - a[1] * a[4];
        const float32 b01 = a[0] * a[6] - a[2] * a[4];
        const float32 b02 = a[0] * a[7] - a[3] * a[4];
        const float32 b03 = a[1] * a[6] - a[2] * a[5];
        const float32 b04 = a[1] * a[7] - a[3] * a[5];
        const float32 b05 = a[2] * a[7] - a[3] * a[6];
        const float32 b06 = a[8] * a[13] - a[9] * a[12];
        const float32 b07 = a[8] * a[14] - a[10] * a[12];
        const float32 b08 = a[8] * a[15] - a[11] * a[12];
        const float32 b09 = a[9] * a[14] - a[10] * a[13];
        const float32 b10 = a[9] * a[15] - a[11] * a[13];
        const float32 b11 = a[10] * a[15] - a[11] * a[14];
        return b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    }

    /**
     * General inverse. Returns identity for singular matrices.
     */
    Matrix4 Inverse() const {
        const float32* a = Data();
        const float32 b00 = a[0] * a[5] - a[1] * a[4];
        const float32 b01 = a[0] * a[6] - a[2] * a[4];
        const float32 b02 = a[0] * a[7] - a[3] * a[4];
        const float32 b03 = a[1] * a[6] - a[2] * a[5];
        const float32 b04 = a[1] * a[7] - a[3] * a[5];
        const float32 b05 = a[2] * a[7] - a[3] * a[6];
        const float32 b06 = a[8] * a[13] - a[9] * a[12];
        const float32 b07 = a[8] * a[14] - a[10] * a[12];
        const float32 b08 = a[8] * a[15] - a[11] * a[12];
        const float32 b09 = a[9] * a[14] - a[10] * a[13];
        const float32 b10 = a[9] * a[15] - a[11] * a[13];
        const float32 b11 = a[10] * a[15] - a[11] * a[14];

        const float32 det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
        if (std::abs(det) < 1e-12f) {
            return Identity();
        }
        const float32 inv_det = 1.0f / det;

        Matrix4 result;
        float32* r = result.Data();
        r[0]  = ( a[5] * b11 - a[6] * b10 + a[7] * b09) * inv_det;
        r[1]  = (-a[1] * b11 + a[2] * b10 - a[3] * b09) * inv_det;
        r[2]  = ( a[13] * b05 - a[14] * b04 + a[15] * b03) * inv_det;
        r[3]  = (-a[9] * b05 + a[10] * b04 - a[11] * b03) * inv_det;
        r[4]  = (-a[4] * b11 + a[6] * b08 - a[7] * b07) * inv_det;
        r[5]  = ( a[0] * b11 - a[2] * b08 + a[3] * b07) * inv_det;
        r[6]  = (-a[12] * b05 + a[14] * b02 - a[15] * b01) * inv_det;
        r[7]  = ( a[8] * b05 - a[10] * b02 + a[11] * b01) * inv_det;
        r[8]  = ( a[4] * b10 - a[5] * b08 + a[7] * b06) * inv_det;
        r[9]  = (-a[0] * b10 + a[1] * b08 - a[3] * b06) * inv_det;
        r[10] = ( a[12] * b04 - a[13] * b02 + a[15] * b00) * inv_det;
        r[11] = (-a[8] * b04 + a[9] * b02 - a[11] * b00) * inv_det;
        r[12] = (-a[4] * b09 + a[5] * b07 - a[6] * b06) * inv_det;
        r[13] = ( a[0] * b09 - a[1] * b07 + a[2] * b06) * inv_det;
        r[14] = (-a[12] * b03 + a[13] * b01 - a[14] * b00) * inv_det;
        r[15] = ( a[8] * b03 - a[9] * b01 + a[10] * b00) * inv_det;
        return result;
    }

    const float32* Data() const {
        return &m[0][0];
    }

    float32* Data() {
        return &m[0][0];
    }
};

} // namespace DRX::Math

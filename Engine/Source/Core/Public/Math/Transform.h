#pragma once

/**
 * DRX Engine - Transform
 * Position, rotation, and scale representation
 */

#include "../CoreTypes.h"
#include "Vector3.h"
#include "Quaternion.h"
#include "Matrix4.h"

namespace DRX::Math {

struct Transform {
    Vector3 position;
    Quaternion rotation;
    Vector3 scale;

    // Constructors
    Transform()
        : position(Vector3::Zero)
        , rotation(Quaternion::Identity())
        , scale(Vector3::One)
    {}

    Transform(const Vector3& pos)
        : position(pos)
        , rotation(Quaternion::Identity())
        , scale(Vector3::One)
    {}

    Transform(const Vector3& pos, const Quaternion& rot)
        : position(pos)
        , rotation(rot)
        , scale(Vector3::One)
    {}

    Transform(const Vector3& pos, const Quaternion& rot, const Vector3& scl)
        : position(pos)
        , rotation(rot)
        , scale(scl)
    {}

    // Matrix conversion
    Matrix4 ToMatrix() const {
        Matrix4 result = rotation.ToMatrix();

        // Apply scale
        result[0][0] *= scale.x;
        result[0][1] *= scale.x;
        result[0][2] *= scale.x;

        result[1][0] *= scale.y;
        result[1][1] *= scale.y;
        result[1][2] *= scale.y;

        result[2][0] *= scale.z;
        result[2][1] *= scale.z;
        result[2][2] *= scale.z;

        // Apply translation
        result[3][0] = position.x;
        result[3][1] = position.y;
        result[3][2] = position.z;

        return result;
    }

    // Transform operations
    Vector3 TransformPoint(const Vector3& point) const {
        return position + rotation * (scale * point);
    }

    Vector3 TransformVector(const Vector3& vector) const {
        return rotation * (scale * vector);
    }

    Vector3 TransformDirection(const Vector3& direction) const {
        return rotation * direction;
    }

    Vector3 InverseTransformPoint(const Vector3& point) const {
        Quaternion invRotation = rotation.Inverse();
        Vector3 invScale(1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z);
        return invScale * (invRotation * (point - position));
    }

    Vector3 InverseTransformVector(const Vector3& vector) const {
        Quaternion invRotation = rotation.Inverse();
        Vector3 invScale(1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z);
        return invScale * (invRotation * vector);
    }

    // Direction vectors
    Vector3 GetForward() const {
        return rotation.GetForward();
    }

    Vector3 GetRight() const {
        return rotation.GetRight();
    }

    Vector3 GetUp() const {
        return rotation.GetUp();
    }

    // Modifiers
    void Translate(const Vector3& delta) {
        position += delta;
    }

    void Rotate(const Quaternion& delta) {
        rotation = delta * rotation;
        rotation.Normalize();
    }

    void RotateAround(const Vector3& point, const Vector3& axis, float32 angle) {
        Quaternion rot = Quaternion::FromAxisAngle(axis, angle);
        position = point + rot * (position - point);
        rotation = rot * rotation;
        rotation.Normalize();
    }

    void LookAt(const Vector3& target, const Vector3& up = Vector3::Up) {
        rotation = Quaternion::LookRotation(target - position, up);
    }

    // Interpolation
    static Transform Lerp(const Transform& a, const Transform& b, float32 t) {
        return Transform(
            Vector3::Lerp(a.position, b.position, t),
            Quaternion::Slerp(a.rotation, b.rotation, t),
            Vector3::Lerp(a.scale, b.scale, t)
        );
    }

    // Combine transforms
    Transform operator*(const Transform& other) const {
        Transform result;
        result.scale = scale * other.scale;
        result.rotation = rotation * other.rotation;
        result.position = position + rotation * (scale * other.position);
        return result;
    }

    // Inverse transform
    Transform Inverse() const {
        Transform result;
        result.rotation = rotation.Inverse();
        result.scale = Vector3(1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z);
        result.position = result.rotation * (result.scale * -position);
        return result;
    }

    // Identity
    static Transform Identity() {
        return Transform();
    }
};

} // namespace DRX::Math

/**
 * DRX Engine - Math Unit Tests
 */

#include <gtest/gtest.h>
#include "Core/Public/Math/Vector2.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Vector4.h"
#include "Core/Public/Math/Matrix4.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Transform.h"

using namespace DRX;
using namespace DRX::Math;

// ============================================================================
// Vector2 Tests
// ============================================================================

TEST(Vector2Test, DefaultConstructor) {
    Vector2 v;
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
}

TEST(Vector2Test, ParameterizedConstructor) {
    Vector2 v(3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.x, 3.0f);
    EXPECT_FLOAT_EQ(v.y, 4.0f);
}

TEST(Vector2Test, Length) {
    Vector2 v(3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.Length(), 5.0f);
}

TEST(Vector2Test, Normalize) {
    Vector2 v(3.0f, 4.0f);
    Vector2 n = v.Normalized();
    EXPECT_NEAR(n.Length(), 1.0f, 0.0001f);
}

TEST(Vector2Test, DotProduct) {
    Vector2 a(1.0f, 0.0f);
    Vector2 b(0.0f, 1.0f);
    EXPECT_FLOAT_EQ(Vector2::Dot(a, b), 0.0f);

    Vector2 c(1.0f, 0.0f);
    Vector2 d(1.0f, 0.0f);
    EXPECT_FLOAT_EQ(Vector2::Dot(c, d), 1.0f);
}

// ============================================================================
// Vector3 Tests
// ============================================================================

TEST(Vector3Test, DefaultConstructor) {
    Vector3 v;
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
}

TEST(Vector3Test, Addition) {
    Vector3 a(1.0f, 2.0f, 3.0f);
    Vector3 b(4.0f, 5.0f, 6.0f);
    Vector3 c = a + b;

    EXPECT_FLOAT_EQ(c.x, 5.0f);
    EXPECT_FLOAT_EQ(c.y, 7.0f);
    EXPECT_FLOAT_EQ(c.z, 9.0f);
}

TEST(Vector3Test, CrossProduct) {
    Vector3 a(1.0f, 0.0f, 0.0f);
    Vector3 b(0.0f, 1.0f, 0.0f);
    Vector3 c = Vector3::Cross(a, b);

    EXPECT_FLOAT_EQ(c.x, 0.0f);
    EXPECT_FLOAT_EQ(c.y, 0.0f);
    EXPECT_FLOAT_EQ(c.z, 1.0f);
}

TEST(Vector3Test, Reflect) {
    Vector3 v(1.0f, -1.0f, 0.0f);
    Vector3 n(0.0f, 1.0f, 0.0f);
    Vector3 r = Vector3::Reflect(v, n);

    EXPECT_FLOAT_EQ(r.x, 1.0f);
    EXPECT_FLOAT_EQ(r.y, 1.0f);
    EXPECT_FLOAT_EQ(r.z, 0.0f);
}

// ============================================================================
// Matrix4 Tests
// ============================================================================

TEST(Matrix4Test, Identity) {
    Matrix4 m = Matrix4::Identity();

    EXPECT_FLOAT_EQ(m[0][0], 1.0f);
    EXPECT_FLOAT_EQ(m[1][1], 1.0f);
    EXPECT_FLOAT_EQ(m[2][2], 1.0f);
    EXPECT_FLOAT_EQ(m[3][3], 1.0f);
    EXPECT_FLOAT_EQ(m[0][1], 0.0f);
}

TEST(Matrix4Test, Translation) {
    Matrix4 m = Matrix4::Translation(1.0f, 2.0f, 3.0f);
    Vector3 v(0.0f, 0.0f, 0.0f);
    Vector3 result = m.TransformPoint(v);

    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 3.0f);
}

TEST(Matrix4Test, Scale) {
    Matrix4 m = Matrix4::Scale(Vector3(2.0f, 3.0f, 4.0f));
    Vector3 v(1.0f, 1.0f, 1.0f);
    Vector3 result = m.TransformPoint(v);

    EXPECT_FLOAT_EQ(result.x, 2.0f);
    EXPECT_FLOAT_EQ(result.y, 3.0f);
    EXPECT_FLOAT_EQ(result.z, 4.0f);
}

// ============================================================================
// Quaternion Tests
// ============================================================================

TEST(QuaternionTest, Identity) {
    Quaternion q = Quaternion::Identity();

    EXPECT_FLOAT_EQ(q.x, 0.0f);
    EXPECT_FLOAT_EQ(q.y, 0.0f);
    EXPECT_FLOAT_EQ(q.z, 0.0f);
    EXPECT_FLOAT_EQ(q.w, 1.0f);
}

TEST(QuaternionTest, AxisAngle) {
    Quaternion q = Quaternion::FromAxisAngle(Vector3::Up, 3.14159f / 2.0f);
    Vector3 forward = q * Vector3::Forward;

    // Forward should become approximately Right after 90 degree rotation around Y
    EXPECT_NEAR(forward.x, 1.0f, 0.001f);
    EXPECT_NEAR(forward.y, 0.0f, 0.001f);
    EXPECT_NEAR(forward.z, 0.0f, 0.001f);
}

TEST(QuaternionTest, Conjugate) {
    Quaternion q(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion c = q.Conjugate();

    EXPECT_FLOAT_EQ(c.x, -1.0f);
    EXPECT_FLOAT_EQ(c.y, -2.0f);
    EXPECT_FLOAT_EQ(c.z, -3.0f);
    EXPECT_FLOAT_EQ(c.w, 4.0f);
}

// ============================================================================
// Transform Tests
// ============================================================================

TEST(TransformTest, DefaultConstructor) {
    Transform t;

    EXPECT_FLOAT_EQ(t.position.x, 0.0f);
    EXPECT_FLOAT_EQ(t.scale.x, 1.0f);
}

TEST(TransformTest, TransformPoint) {
    Transform t;
    t.position = Vector3(1.0f, 2.0f, 3.0f);

    Vector3 p(0.0f, 0.0f, 0.0f);
    Vector3 result = t.TransformPoint(p);

    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 3.0f);
}

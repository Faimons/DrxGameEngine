/**
 * DRX Engine - Renderer CPU-side tests (mesh building, bounds, fonts)
 */

#include <gtest/gtest.h>
#include "Renderer/Public/MeshBuilder.h"
#include "Renderer/Public/RenderTypes.h"
#include "Renderer/Public/Font.h"

#include <cmath>
#include <fstream>
#include <iterator>

using namespace DRX;
using namespace DRX::Renderer;
using Math::Vector3;

namespace {

// Every triangle must face away from the mesh centre (closed convex meshes)
void ExpectOutwardWinding(const MeshData& mesh) {
    const Vector3 center = mesh.ComputeBounds().GetCenter();
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vector3& a = mesh.vertices[mesh.indices[i]].position;
        const Vector3& b = mesh.vertices[mesh.indices[i + 1]].position;
        const Vector3& c = mesh.vertices[mesh.indices[i + 2]].position;
        const Vector3 n = Vector3::Cross(b - a, c - a);
        if (n.LengthSquared() < 1e-10f) continue;   // degenerate pole triangles
        const Vector3 centroid = (a + b + c) * (1.0f / 3.0f);
        EXPECT_GT(Vector3::Dot(n, centroid - center), 0.0f) << "triangle " << i / 3;
    }
}

void ExpectValidIndices(const MeshData& mesh) {
    ASSERT_FALSE(mesh.IsEmpty());
    EXPECT_EQ(mesh.indices.size() % 3, 0u);
    for (uint32 index : mesh.indices) {
        ASSERT_LT(index, mesh.vertices.size());
    }
}

} // namespace

TEST(MeshBuilderTest, BoxDimensionsAndWinding) {
    const MeshData box = MeshBuilder::Box(Vector3(2.0f, 4.0f, 6.0f));
    ExpectValidIndices(box);
    EXPECT_EQ(box.vertices.size(), 24u);
    EXPECT_EQ(box.indices.size(), 36u);

    const AABB bounds = box.ComputeBounds();
    EXPECT_FLOAT_EQ(bounds.min.x, -1.0f);
    EXPECT_FLOAT_EQ(bounds.max.y, 2.0f);
    EXPECT_FLOAT_EQ(bounds.max.z, 3.0f);
    ExpectOutwardWinding(box);
}

TEST(MeshBuilderTest, BoxWorldUVsMatchMetres) {
    const MeshData box = MeshBuilder::Box(Vector3(4.0f, 1.0f, 1.0f), true, 2.0f);
    float32 max_u = 0.0f;
    for (const Vertex& v : box.vertices) max_u = std::max(max_u, v.tex_coord.x);
    EXPECT_FLOAT_EQ(max_u, 2.0f);   // 4 m at 2 m per repeat
}

TEST(MeshBuilderTest, PlaneFacesUp) {
    const MeshData plane = MeshBuilder::Plane(10.0f, 10.0f, 4);
    ExpectValidIndices(plane);
    EXPECT_EQ(plane.vertices.size(), 25u);
    for (size_t i = 0; i + 2 < plane.indices.size(); i += 3) {
        const Vector3& a = plane.vertices[plane.indices[i]].position;
        const Vector3& b = plane.vertices[plane.indices[i + 1]].position;
        const Vector3& c = plane.vertices[plane.indices[i + 2]].position;
        EXPECT_GT(Vector3::Cross(b - a, c - a).y, 0.0f);
    }
}

TEST(MeshBuilderTest, ClosedShapesWindOutward) {
    ExpectOutwardWinding(MeshBuilder::Sphere(1.0f, 12, 8));
    ExpectOutwardWinding(MeshBuilder::Cylinder(0.5f, 2.0f, 12));
    ExpectOutwardWinding(MeshBuilder::Capsule(0.4f, 1.8f, 12, 6));
}

TEST(MeshBuilderTest, CapsuleHeightIncludesCaps) {
    const AABB bounds = MeshBuilder::Capsule(0.4f, 1.8f).ComputeBounds();
    EXPECT_NEAR(bounds.max.y - bounds.min.y, 1.8f, 1e-4f);
    EXPECT_NEAR(bounds.max.x, 0.4f, 1e-4f);
}

TEST(MeshBuilderTest, GridTextureSizeAndBorder) {
    const auto pixels = MeshBuilder::GridTexture(64, 4, {1, 1, 1, 1}, {0.5f, 0.5f, 0.5f, 1}, {0, 0, 0, 1});
    ASSERT_EQ(pixels.size(), 64u * 64u * 4u);
    EXPECT_EQ(pixels[0], 0);           // border
    EXPECT_EQ(pixels[3], 255);         // alpha
    const size_t inner = (static_cast<size_t>(5) * 64 + 5) * 4;
    EXPECT_EQ(pixels[inner], 255);     // background
}

TEST(AABBTest, ExpandContainsIntersects) {
    AABB box;
    EXPECT_FALSE(box.IsValid());
    box.Expand(Vector3(-1, -1, -1));
    box.Expand(Vector3(1, 1, 1));
    EXPECT_TRUE(box.IsValid());
    EXPECT_TRUE(box.Contains(Vector3::Zero));
    EXPECT_FALSE(box.Contains(Vector3(2, 0, 0)));

    AABB other{{0.5f, 0.5f, 0.5f}, {3, 3, 3}};
    EXPECT_TRUE(box.Intersects(other));
    AABB far{{5, 5, 5}, {6, 6, 6}};
    EXPECT_FALSE(box.Intersects(far));
}

TEST(AABBTest, TransformedByRotationAndTranslation) {
    const AABB box{{-1, -1, -2}, {1, 1, 2}};
    const AABB moved = box.Transformed(Math::Matrix4::Translation(10, 0, 0) *
                                       Math::Matrix4::RotationY(3.14159265f * 0.5f));
    EXPECT_NEAR(moved.min.x, 8.0f, 1e-4f);
    EXPECT_NEAR(moved.max.x, 12.0f, 1e-4f);
    EXPECT_NEAR(moved.max.z, 1.0f, 1e-4f);
}

TEST(MeshDataTest, AppendTransformsAndOffsetsIndices) {
    MeshData a = MeshBuilder::Box(Vector3::One);
    const MeshData b = MeshBuilder::Box(Vector3::One);
    const size_t vertex_count = a.vertices.size();
    a.Append(b, Math::Matrix4::Translation(5, 0, 0));
    ExpectValidIndices(a);
    EXPECT_EQ(a.vertices.size(), vertex_count * 2);
    EXPECT_NEAR(a.ComputeBounds().max.x, 5.5f, 1e-5f);
}

TEST(FontTest, DecodeUtf8) {
    const String text = "A\xC3\xA4\xE2\x82\xAC\xFF";   // A, ä, €, invalid byte
    size_t index = 0;
    EXPECT_EQ(FontAtlas::DecodeUtf8(text, index), 0x41u);
    EXPECT_EQ(FontAtlas::DecodeUtf8(text, index), 0xE4u);
    EXPECT_EQ(FontAtlas::DecodeUtf8(text, index), 0x20ACu);
    EXPECT_EQ(FontAtlas::DecodeUtf8(text, index), 0xFFFDu);
    EXPECT_EQ(index, text.size());
}

TEST(FontTest, BuildAndMeasure) {
    std::ifstream file(DRX_TEST_FONT_PATH, std::ios::binary);
    ASSERT_TRUE(file) << DRX_TEST_FONT_PATH;
    const Array<uint8> ttf((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    FontAtlas font;
    ASSERT_TRUE(font.Build(ttf.data(), ttf.size(), {16.0f, 32.0f}, 512));
    EXPECT_TRUE(font.IsValid());
    EXPECT_EQ(font.GetPixels().size(), static_cast<size_t>(font.GetWidth()) * font.GetHeight());

    const float32 small = font.MeasureText("Hello", 16.0f);
    const float32 large = font.MeasureText("Hello", 32.0f);
    EXPECT_GT(small, 0.0f);
    EXPECT_NEAR(large / small, 2.0f, 0.25f);
    EXPECT_GT(font.MeasureText("Hello World", 16.0f), small);
    EXPECT_FLOAT_EQ(font.MeasureText("", 16.0f), 0.0f);

    Array<GlyphQuad> quads;
    font.LayoutText("Hi \xC3\xA4", 0.0f, 0.0f, 16.0f, quads);
    EXPECT_EQ(quads.size(), 3u);   // space produces no quad
}

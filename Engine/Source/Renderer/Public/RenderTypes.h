#pragma once

/**
 * DRX Engine - Renderer data types shared by the scene renderer, loaders and gameplay code
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector2.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Vector4.h"
#include "Core/Public/Math/Matrix4.h"

#include <algorithm>
#include <limits>

namespace DRX::Renderer {

/**
 * Vertex layout used by all lit meshes
 */
struct Vertex {
    Math::Vector3 position;
    Math::Vector3 normal = Math::Vector3::Up;
    Math::Vector3 color = Math::Vector3::One;
    Math::Vector2 tex_coord;
};

/**
 * Axis aligned bounding box
 */
struct AABB {
    Math::Vector3 min = Math::Vector3(std::numeric_limits<float32>::max());
    Math::Vector3 max = Math::Vector3(std::numeric_limits<float32>::lowest());

    bool IsValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    Math::Vector3 GetCenter() const { return (min + max) * 0.5f; }
    Math::Vector3 GetExtents() const { return (max - min) * 0.5f; }

    void Expand(const Math::Vector3& point) {
        min = Math::Vector3::Min(min, point);
        max = Math::Vector3::Max(max, point);
    }

    void Merge(const AABB& other) {
        if (!other.IsValid()) return;
        Expand(other.min);
        Expand(other.max);
    }

    bool Contains(const Math::Vector3& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }

    bool Intersects(const AABB& other) const {
        return min.x <= other.max.x && max.x >= other.min.x &&
               min.y <= other.max.y && max.y >= other.min.y &&
               min.z <= other.max.z && max.z >= other.min.z;
    }

    // Bounds of this box after an affine transform
    AABB Transformed(const Math::Matrix4& m) const {
        AABB result;
        if (!IsValid()) return result;
        for (int i = 0; i < 8; ++i) {
            const Math::Vector3 corner((i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z);
            result.Expand(m.TransformPoint(corner));
        }
        return result;
    }
};

/**
 * CPU-side mesh (triangle list)
 */
struct MeshData {
    Array<Vertex> vertices;
    Array<uint32> indices;

    bool IsEmpty() const { return vertices.empty() || indices.empty(); }

    AABB ComputeBounds() const {
        AABB bounds;
        for (const auto& v : vertices) {
            bounds.Expand(v.position);
        }
        return bounds;
    }

    // Recomputes smooth vertex normals from triangle geometry
    void ComputeNormals() {
        for (auto& v : vertices) v.normal = Math::Vector3::Zero;
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            const uint32 a = indices[i], b = indices[i + 1], c = indices[i + 2];
            if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) continue;
            const Math::Vector3 n = Math::Vector3::Cross(vertices[b].position - vertices[a].position,
                                                         vertices[c].position - vertices[a].position);
            vertices[a].normal += n;
            vertices[b].normal += n;
            vertices[c].normal += n;
        }
        for (auto& v : vertices) {
            v.normal = v.normal.LengthSquared() > 1e-12f ? v.normal.Normalized() : Math::Vector3::Up;
        }
    }

    // Appends another mesh transformed by `transform`
    void Append(const MeshData& other, const Math::Matrix4& transform = Math::Matrix4::Identity()) {
        const auto base = static_cast<uint32>(vertices.size());
        const Math::Matrix4 normal_matrix = transform.Inverse().Transposed();
        for (Vertex v : other.vertices) {
            v.position = transform.TransformPoint(v.position);
            v.normal = normal_matrix.TransformVector(v.normal).Normalized();
            vertices.push_back(v);
        }
        for (uint32 index : other.indices) {
            indices.push_back(base + index);
        }
    }
};

/**
 * Surface description for a draw call
 */
struct Material {
    Math::Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    TextureHandle albedo;                       // invalid = white
    float32 roughness = 0.7f;
    float32 metallic = 0.0f;
    Math::Vector3 emissive = Math::Vector3::Zero;
    float32 emissive_strength = 0.0f;
    bool double_sided = false;
    bool cast_shadows = true;
};

/**
 * Camera used for one frame
 */
struct CameraParams {
    Math::Vector3 position;
    Math::Matrix4 view = Math::Matrix4::Identity();
    Math::Matrix4 projection = Math::Matrix4::Identity();  // Vulkan clip space (PerspectiveVulkan)
    float32 near_plane = 0.1f;
    float32 far_plane = 1000.0f;

    static CameraParams LookAt(const Math::Vector3& eye, const Math::Vector3& target,
                               float32 fov_y_radians, float32 aspect, float32 near_plane = 0.1f,
                               float32 far_plane = 1000.0f) {
        CameraParams camera;
        camera.position = eye;
        camera.view = Math::Matrix4::LookAt(eye, target, Math::Vector3::Up);
        camera.projection = Math::Matrix4::PerspectiveVulkan(fov_y_radians, aspect, near_plane, far_plane);
        camera.near_plane = near_plane;
        camera.far_plane = far_plane;
        return camera;
    }
};

/**
 * Sun, sky, ambient light and fog
 */
struct EnvironmentSettings {
    Math::Vector3 sun_direction = Math::Vector3(0.4f, 0.8f, 0.35f);  // towards the sun
    Math::Vector3 sun_color = Math::Vector3(1.0f, 0.95f, 0.85f);
    float32 sun_intensity = 3.0f;

    Math::Vector3 sky_zenith = Math::Vector3(0.18f, 0.36f, 0.75f);
    Math::Vector3 sky_horizon = Math::Vector3(0.65f, 0.75f, 0.88f);
    Math::Vector3 ground_color = Math::Vector3(0.25f, 0.23f, 0.2f);

    Math::Vector3 ambient_sky = Math::Vector3(0.55f, 0.65f, 0.85f);
    Math::Vector3 ambient_ground = Math::Vector3(0.3f, 0.27f, 0.22f);
    float32 ambient_intensity = 0.6f;

    Math::Vector3 fog_color = Math::Vector3(0.62f, 0.7f, 0.8f);
    float32 fog_density = 0.004f;

    bool shadows_enabled = true;
    float32 shadow_distance = 80.0f;   // radius around the camera covered by the shadow map
    float32 shadow_strength = 0.85f;
    float32 shadow_bias = 0.0015f;

    float32 exposure = 1.0f;
};

/**
 * Per-frame renderer statistics
 */
struct RenderStats {
    uint32 draw_calls = 0;
    uint32 triangles = 0;
    uint32 meshes_submitted = 0;
    uint32 meshes_culled = 0;
    uint32 shadow_casters = 0;
    uint32 particles = 0;
    uint32 ui_vertices = 0;
};

} // namespace DRX::Renderer

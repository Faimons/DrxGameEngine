/**
 * DRX Engine - Mesh and texture builders
 */

#include "Renderer/Public/MeshBuilder.h"

#include <algorithm>
#include <cmath>

namespace DRX::Renderer::MeshBuilder {

namespace {

constexpr float32 kPi = 3.14159265358979323846f;

uint8 ToByte(float32 v) {
    return static_cast<uint8>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

void PutPixel(Array<uint8>& pixels, uint32 size, uint32 x, uint32 y, const Math::Vector4& c) {
    uint8* p = &pixels[(static_cast<size_t>(y) * size + x) * 4];
    p[0] = ToByte(c.x);
    p[1] = ToByte(c.y);
    p[2] = ToByte(c.z);
    p[3] = ToByte(c.w);
}

} // namespace

MeshData Box(const Math::Vector3& size, bool world_uvs, float32 uv_scale, const Math::Vector3& color) {
    MeshData mesh;
    const Math::Vector3 h = size * 0.5f;
    const float32 s = uv_scale > 0.0f ? 1.0f / uv_scale : 1.0f;

    struct Face {
        Math::Vector3 normal;
        Math::Vector3 u_axis;   // points along +u
        Math::Vector3 v_axis;   // points along +v (downwards in the texture)
        float32 u_len, v_len;
    };
    // Each face: corners = center +- u_axis*half_u +- v_axis*half_v, wound counter-clockwise from outside
    const Face faces[6] = {
        {{ 1, 0, 0}, {0, 0, -1}, {0, -1, 0}, size.z, size.y},
        {{-1, 0, 0}, {0, 0,  1}, {0, -1, 0}, size.z, size.y},
        {{ 0, 1, 0}, {1, 0,  0}, {0, 0,  1}, size.x, size.z},
        {{ 0,-1, 0}, {1, 0,  0}, {0, 0, -1}, size.x, size.z},
        {{ 0, 0, 1}, {1, 0,  0}, {0, -1, 0}, size.x, size.y},
        {{ 0, 0,-1}, {-1, 0, 0}, {0, -1, 0}, size.x, size.y},
    };

    for (const Face& f : faces) {
        const Math::Vector3 center(f.normal.x * h.x, f.normal.y * h.y, f.normal.z * h.z);
        const Math::Vector3 du = f.u_axis * (f.u_len * 0.5f);
        const Math::Vector3 dv = f.v_axis * (f.v_len * 0.5f);
        const float32 u_max = world_uvs ? f.u_len * s : 1.0f;
        const float32 v_max = world_uvs ? f.v_len * s : 1.0f;

        const auto base = static_cast<uint32>(mesh.vertices.size());
        const Math::Vector3 corners[4] = {center - du - dv, center + du - dv, center + du + dv, center - du + dv};
        const Math::Vector2 uvs[4] = {{0, 0}, {u_max, 0}, {u_max, v_max}, {0, v_max}};
        for (int i = 0; i < 4; ++i) {
            mesh.vertices.push_back({corners[i], f.normal, color, uvs[i]});
        }

        // Ensure counter-clockwise winding when viewed from outside
        const Math::Vector3 face_normal = Math::Vector3::Cross(corners[1] - corners[0], corners[2] - corners[0]);
        if (Math::Vector3::Dot(face_normal, f.normal) >= 0.0f) {
            mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        } else {
            mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
        }
    }
    return mesh;
}

MeshData Plane(float32 width, float32 depth, uint32 subdivisions, bool world_uvs, float32 uv_scale) {
    MeshData mesh;
    subdivisions = std::max(1u, subdivisions);
    const float32 s = uv_scale > 0.0f ? 1.0f / uv_scale : 1.0f;
    const uint32 verts = subdivisions + 1;

    for (uint32 z = 0; z < verts; ++z) {
        for (uint32 x = 0; x < verts; ++x) {
            const float32 fx = static_cast<float32>(x) / static_cast<float32>(subdivisions);
            const float32 fz = static_cast<float32>(z) / static_cast<float32>(subdivisions);
            Vertex v;
            v.position = Math::Vector3((fx - 0.5f) * width, 0.0f, (fz - 0.5f) * depth);
            v.normal = Math::Vector3::Up;
            v.tex_coord = world_uvs ? Math::Vector2(fx * width * s, fz * depth * s) : Math::Vector2(fx, fz);
            mesh.vertices.push_back(v);
        }
    }
    for (uint32 z = 0; z < subdivisions; ++z) {
        for (uint32 x = 0; x < subdivisions; ++x) {
            const uint32 tl = z * verts + x, tr = tl + 1, bl = tl + verts, br = bl + 1;
            mesh.indices.insert(mesh.indices.end(), {tl, bl, tr, tr, bl, br});
        }
    }
    return mesh;
}

MeshData Sphere(float32 radius, uint32 segments, uint32 rings) {
    MeshData mesh;
    segments = std::max(3u, segments);
    rings = std::max(2u, rings);

    for (uint32 r = 0; r <= rings; ++r) {
        const float32 theta = kPi * static_cast<float32>(r) / static_cast<float32>(rings);
        for (uint32 s = 0; s <= segments; ++s) {
            const float32 phi = 2.0f * kPi * static_cast<float32>(s) / static_cast<float32>(segments);
            const Math::Vector3 n(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            mesh.vertices.push_back({n * radius, n, Math::Vector3::One,
                                     {static_cast<float32>(s) / static_cast<float32>(segments),
                                      static_cast<float32>(r) / static_cast<float32>(rings)}});
        }
    }
    for (uint32 r = 0; r < rings; ++r) {
        for (uint32 s = 0; s < segments; ++s) {
            const uint32 a = r * (segments + 1) + s, b = a + segments + 1;
            mesh.indices.insert(mesh.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    }
    return mesh;
}

MeshData Cylinder(float32 radius, float32 height, uint32 segments) {
    MeshData mesh;
    segments = std::max(3u, segments);
    const float32 half = height * 0.5f;

    // Side
    for (uint32 s = 0; s <= segments; ++s) {
        const float32 phi = 2.0f * kPi * static_cast<float32>(s) / static_cast<float32>(segments);
        const Math::Vector3 n(std::cos(phi), 0.0f, std::sin(phi));
        const float32 u = static_cast<float32>(s) / static_cast<float32>(segments);
        mesh.vertices.push_back({n * radius + Math::Vector3(0, -half, 0), n, Math::Vector3::One, {u, 1.0f}});
        mesh.vertices.push_back({n * radius + Math::Vector3(0, half, 0), n, Math::Vector3::One, {u, 0.0f}});
    }
    for (uint32 s = 0; s < segments; ++s) {
        const uint32 b0 = s * 2, t0 = b0 + 1, b1 = b0 + 2, t1 = b0 + 3;
        mesh.indices.insert(mesh.indices.end(), {b0, t0, b1, b1, t0, t1});
    }

    // Caps
    for (int cap = 0; cap < 2; ++cap) {
        const float32 y = cap == 0 ? half : -half;
        const Math::Vector3 n(0.0f, cap == 0 ? 1.0f : -1.0f, 0.0f);
        const auto center = static_cast<uint32>(mesh.vertices.size());
        mesh.vertices.push_back({{0, y, 0}, n, Math::Vector3::One, {0.5f, 0.5f}});
        for (uint32 s = 0; s <= segments; ++s) {
            const float32 phi = 2.0f * kPi * static_cast<float32>(s) / static_cast<float32>(segments);
            mesh.vertices.push_back({{std::cos(phi) * radius, y, std::sin(phi) * radius}, n, Math::Vector3::One,
                                     {0.5f + std::cos(phi) * 0.5f, 0.5f + std::sin(phi) * 0.5f}});
        }
        for (uint32 s = 0; s < segments; ++s) {
            const uint32 a = center + 1 + s, b = a + 1;
            if (cap == 0) mesh.indices.insert(mesh.indices.end(), {center, b, a});
            else mesh.indices.insert(mesh.indices.end(), {center, a, b});
        }
    }
    return mesh;
}

MeshData Capsule(float32 radius, float32 height, uint32 segments, uint32 rings) {
    MeshData mesh;
    segments = std::max(3u, segments);
    rings = std::max(2u, rings);
    const float32 half_body = std::max(0.0f, height * 0.5f - radius);

    // Hemisphere rings top (0..rings) then bottom (rings..2*rings); latitude from +Y to -Y
    const uint32 total_rings = rings * 2 + 1;
    for (uint32 r = 0; r <= total_rings; ++r) {
        const bool top = r <= rings;
        const uint32 local = top ? r : r - 1;
        const float32 theta = kPi * static_cast<float32>(local) / static_cast<float32>(rings * 2);
        const float32 offset = top ? half_body : -half_body;
        for (uint32 s = 0; s <= segments; ++s) {
            const float32 phi = 2.0f * kPi * static_cast<float32>(s) / static_cast<float32>(segments);
            const Math::Vector3 n(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            mesh.vertices.push_back({n * radius + Math::Vector3(0, offset, 0), n, Math::Vector3::One,
                                     {static_cast<float32>(s) / static_cast<float32>(segments),
                                      static_cast<float32>(r) / static_cast<float32>(total_rings)}});
        }
    }
    for (uint32 r = 0; r < total_rings; ++r) {
        for (uint32 s = 0; s < segments; ++s) {
            const uint32 a = r * (segments + 1) + s, b = a + segments + 1;
            mesh.indices.insert(mesh.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    }
    return mesh;
}

Array<uint8> GridTexture(uint32 size, uint32 cells, const Math::Vector4& background, const Math::Vector4& line,
                         const Math::Vector4& border) {
    size = std::max(8u, size);
    cells = std::max(1u, cells);
    Array<uint8> pixels(static_cast<size_t>(size) * size * 4);
    const uint32 cell = std::max(2u, size / cells);
    const uint32 border_width = std::max(2u, size / 64);

    for (uint32 y = 0; y < size; ++y) {
        for (uint32 x = 0; x < size; ++x) {
            Math::Vector4 c = background;
            if (x < border_width || y < border_width || x >= size - border_width || y >= size - border_width) {
                c = border;
            } else if (x % cell == 0 || y % cell == 0) {
                c = line;
            }
            PutPixel(pixels, size, x, y, c);
        }
    }
    return pixels;
}

Array<uint8> CheckerTexture(uint32 size, uint32 cells, const Math::Vector4& a, const Math::Vector4& b) {
    size = std::max(2u, size);
    cells = std::max(1u, cells);
    Array<uint8> pixels(static_cast<size_t>(size) * size * 4);
    const uint32 cell = std::max(1u, size / cells);
    for (uint32 y = 0; y < size; ++y) {
        for (uint32 x = 0; x < size; ++x) {
            PutPixel(pixels, size, x, y, ((x / cell) + (y / cell)) % 2 == 0 ? a : b);
        }
    }
    return pixels;
}

} // namespace DRX::Renderer::MeshBuilder

/**
 * DRX Engine - Procedural Mesh Generation Implementation
 */

#include "Procedural/Public/ProceduralMesh.h"
#include <cmath>
#include <algorithm>

namespace DRX::Procedural {

const float PI = 3.14159265359f;

// ============================================================================
// Mesh Implementation
// ============================================================================

void Mesh::CalculateNormals() {
    // Reset all normals
    for (auto& v : vertices) {
        v.nx = v.ny = v.nz = 0;
    }

    // Calculate face normals and add to vertices
    for (size_t i = 0; i < indices.size(); i += 3) {
        unsigned int i0 = indices[i];
        unsigned int i1 = indices[i + 1];
        unsigned int i2 = indices[i + 2];

        Vertex& v0 = vertices[i0];
        Vertex& v1 = vertices[i1];
        Vertex& v2 = vertices[i2];

        // Edge vectors
        float e1x = v1.x - v0.x, e1y = v1.y - v0.y, e1z = v1.z - v0.z;
        float e2x = v2.x - v0.x, e2y = v2.y - v0.y, e2z = v2.z - v0.z;

        // Cross product
        float nx = e1y * e2z - e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y - e1y * e2x;

        v0.nx += nx; v0.ny += ny; v0.nz += nz;
        v1.nx += nx; v1.ny += ny; v1.nz += nz;
        v2.nx += nx; v2.ny += ny; v2.nz += nz;
    }

    // Normalize
    for (auto& v : vertices) {
        float len = std::sqrt(v.nx*v.nx + v.ny*v.ny + v.nz*v.nz);
        if (len > 0.0001f) {
            v.nx /= len; v.ny /= len; v.nz /= len;
        }
    }
}

void Mesh::Transform(float tx, float ty, float tz) {
    for (auto& v : vertices) {
        v.x += tx; v.y += ty; v.z += tz;
    }
}

void Mesh::Scale(float sx, float sy, float sz) {
    for (auto& v : vertices) {
        v.x *= sx; v.y *= sy; v.z *= sz;
    }
}

void Mesh::Rotate(float rx, float ry, float rz) {
    float cx = std::cos(rx), sx = std::sin(rx);
    float cy = std::cos(ry), sy = std::sin(ry);
    float cz = std::cos(rz), sz = std::sin(rz);

    for (auto& v : vertices) {
        // Rotate X
        float y = v.y * cx - v.z * sx;
        float z = v.y * sx + v.z * cx;
        v.y = y; v.z = z;

        // Rotate Y
        float x = v.x * cy + v.z * sy;
        z = -v.x * sy + v.z * cy;
        v.x = x; v.z = z;

        // Rotate Z
        x = v.x * cz - v.y * sz;
        y = v.x * sz + v.y * cz;
        v.x = x; v.y = y;
    }
}

void Mesh::SetColor(float r, float g, float b, float a) {
    for (auto& v : vertices) {
        v.r = r; v.g = g; v.b = b; v.a = a;
    }
}

void Mesh::Merge(const Mesh& other) {
    unsigned int offset = static_cast<unsigned int>(vertices.size());
    vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
    for (auto idx : other.indices) {
        indices.push_back(idx + offset);
    }
}

// ============================================================================
// Primitive Generator
// ============================================================================

Mesh PrimitiveGenerator::CreateCube(float size) {
    Mesh mesh;
    mesh.name = "Cube";
    float s = size * 0.5f;

    // Vertices for each face
    float positions[24][3] = {
        // Front
        {-s, -s,  s}, { s, -s,  s}, { s,  s,  s}, {-s,  s,  s},
        // Back
        { s, -s, -s}, {-s, -s, -s}, {-s,  s, -s}, { s,  s, -s},
        // Left
        {-s, -s, -s}, {-s, -s,  s}, {-s,  s,  s}, {-s,  s, -s},
        // Right
        { s, -s,  s}, { s, -s, -s}, { s,  s, -s}, { s,  s,  s},
        // Top
        {-s,  s,  s}, { s,  s,  s}, { s,  s, -s}, {-s,  s, -s},
        // Bottom
        {-s, -s, -s}, { s, -s, -s}, { s, -s,  s}, {-s, -s,  s}
    };

    float normals[6][3] = {
        {0, 0, 1}, {0, 0, -1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}
    };

    float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    for (int face = 0; face < 6; ++face) {
        for (int v = 0; v < 4; ++v) {
            Vertex vert;
            int idx = face * 4 + v;
            vert.x = positions[idx][0];
            vert.y = positions[idx][1];
            vert.z = positions[idx][2];
            vert.nx = normals[face][0];
            vert.ny = normals[face][1];
            vert.nz = normals[face][2];
            vert.u = uvs[v][0];
            vert.v = uvs[v][1];
            mesh.vertices.push_back(vert);
        }
        unsigned int base = face * 4;
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }

    return mesh;
}

Mesh PrimitiveGenerator::CreateSphere(float radius, int segments) {
    Mesh mesh;
    mesh.name = "Sphere";

    for (int lat = 0; lat <= segments; ++lat) {
        float theta = lat * PI / segments;
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);

        for (int lon = 0; lon <= segments; ++lon) {
            float phi = lon * 2.0f * PI / segments;
            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);

            Vertex v;
            v.nx = cosPhi * sinTheta;
            v.ny = cosTheta;
            v.nz = sinPhi * sinTheta;
            v.x = radius * v.nx;
            v.y = radius * v.ny;
            v.z = radius * v.nz;
            v.u = (float)lon / segments;
            v.v = (float)lat / segments;
            mesh.vertices.push_back(v);
        }
    }

    for (int lat = 0; lat < segments; ++lat) {
        for (int lon = 0; lon < segments; ++lon) {
            int first = lat * (segments + 1) + lon;
            int second = first + segments + 1;

            mesh.indices.push_back(first);
            mesh.indices.push_back(second);
            mesh.indices.push_back(first + 1);

            mesh.indices.push_back(second);
            mesh.indices.push_back(second + 1);
            mesh.indices.push_back(first + 1);
        }
    }

    return mesh;
}

Mesh PrimitiveGenerator::CreateCylinder(float radius, float height, int segments) {
    Mesh mesh;
    mesh.name = "Cylinder";
    float halfH = height * 0.5f;

    // Side vertices
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * PI * i / segments;
        float x = std::cos(angle) * radius;
        float z = std::sin(angle) * radius;

        // Bottom
        Vertex vb;
        vb.x = x; vb.y = -halfH; vb.z = z;
        vb.nx = std::cos(angle); vb.ny = 0; vb.nz = std::sin(angle);
        vb.u = (float)i / segments; vb.v = 0;
        mesh.vertices.push_back(vb);

        // Top
        Vertex vt = vb;
        vt.y = halfH;
        vt.v = 1;
        mesh.vertices.push_back(vt);
    }

    // Side indices
    for (int i = 0; i < segments; ++i) {
        int base = i * 2;
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }

    // Top cap
    unsigned int topCenter = mesh.vertices.size();
    Vertex tc; tc.x = 0; tc.y = halfH; tc.z = 0;
    tc.nx = 0; tc.ny = 1; tc.nz = 0;
    tc.u = 0.5f; tc.v = 0.5f;
    mesh.vertices.push_back(tc);

    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * PI * i / segments;
        Vertex v;
        v.x = std::cos(angle) * radius; v.y = halfH; v.z = std::sin(angle) * radius;
        v.nx = 0; v.ny = 1; v.nz = 0;
        v.u = std::cos(angle) * 0.5f + 0.5f;
        v.v = std::sin(angle) * 0.5f + 0.5f;
        mesh.vertices.push_back(v);
    }

    for (int i = 0; i < segments; ++i) {
        mesh.indices.push_back(topCenter);
        mesh.indices.push_back(topCenter + 1 + i);
        mesh.indices.push_back(topCenter + 2 + i);
    }

    // Bottom cap
    unsigned int bottomCenter = mesh.vertices.size();
    Vertex bc; bc.x = 0; bc.y = -halfH; bc.z = 0;
    bc.nx = 0; bc.ny = -1; bc.nz = 0;
    bc.u = 0.5f; bc.v = 0.5f;
    mesh.vertices.push_back(bc);

    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * PI * i / segments;
        Vertex v;
        v.x = std::cos(angle) * radius; v.y = -halfH; v.z = std::sin(angle) * radius;
        v.nx = 0; v.ny = -1; v.nz = 0;
        v.u = std::cos(angle) * 0.5f + 0.5f;
        v.v = std::sin(angle) * 0.5f + 0.5f;
        mesh.vertices.push_back(v);
    }

    for (int i = 0; i < segments; ++i) {
        mesh.indices.push_back(bottomCenter);
        mesh.indices.push_back(bottomCenter + 2 + i);
        mesh.indices.push_back(bottomCenter + 1 + i);
    }

    return mesh;
}

Mesh PrimitiveGenerator::CreateCone(float radius, float height, int segments) {
    Mesh mesh;
    mesh.name = "Cone";

    // Apex
    unsigned int apexIdx = 0;
    Vertex apex;
    apex.x = 0; apex.y = height; apex.z = 0;
    apex.nx = 0; apex.ny = 1; apex.nz = 0;
    mesh.vertices.push_back(apex);

    // Base vertices
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * PI * i / segments;
        Vertex v;
        v.x = std::cos(angle) * radius;
        v.y = 0;
        v.z = std::sin(angle) * radius;
        v.nx = std::cos(angle);
        v.ny = radius / height;
        v.nz = std::sin(angle);
        float len = std::sqrt(v.nx*v.nx + v.ny*v.ny + v.nz*v.nz);
        v.nx /= len; v.ny /= len; v.nz /= len;
        mesh.vertices.push_back(v);
    }

    // Side triangles
    for (int i = 0; i < segments; ++i) {
        mesh.indices.push_back(apexIdx);
        mesh.indices.push_back(1 + i);
        mesh.indices.push_back(2 + i);
    }

    // Base cap
    unsigned int baseCenter = mesh.vertices.size();
    Vertex bc; bc.x = 0; bc.y = 0; bc.z = 0;
    bc.nx = 0; bc.ny = -1; bc.nz = 0;
    mesh.vertices.push_back(bc);

    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * PI * i / segments;
        Vertex v;
        v.x = std::cos(angle) * radius; v.y = 0; v.z = std::sin(angle) * radius;
        v.nx = 0; v.ny = -1; v.nz = 0;
        mesh.vertices.push_back(v);
    }

    for (int i = 0; i < segments; ++i) {
        mesh.indices.push_back(baseCenter);
        mesh.indices.push_back(baseCenter + 2 + i);
        mesh.indices.push_back(baseCenter + 1 + i);
    }

    return mesh;
}

Mesh PrimitiveGenerator::CreatePlane(float width, float depth, int subdivisions) {
    Mesh mesh;
    mesh.name = "Plane";

    float halfW = width * 0.5f;
    float halfD = depth * 0.5f;
    int segs = subdivisions + 1;

    for (int z = 0; z <= segs; ++z) {
        for (int x = 0; x <= segs; ++x) {
            Vertex v;
            v.x = -halfW + (width * x / segs);
            v.y = 0;
            v.z = -halfD + (depth * z / segs);
            v.nx = 0; v.ny = 1; v.nz = 0;
            v.u = (float)x / segs;
            v.v = (float)z / segs;
            mesh.vertices.push_back(v);
        }
    }

    for (int z = 0; z < segs; ++z) {
        for (int x = 0; x < segs; ++x) {
            int idx = z * (segs + 1) + x;
            mesh.indices.push_back(idx);
            mesh.indices.push_back(idx + segs + 1);
            mesh.indices.push_back(idx + 1);
            mesh.indices.push_back(idx + 1);
            mesh.indices.push_back(idx + segs + 1);
            mesh.indices.push_back(idx + segs + 2);
        }
    }

    return mesh;
}

Mesh PrimitiveGenerator::CreateCapsule(float radius, float height, int segments) {
    Mesh mesh;
    mesh.name = "Capsule";

    float cylinderH = height - 2 * radius;
    if (cylinderH < 0) cylinderH = 0;

    // Top hemisphere
    for (int lat = 0; lat <= segments / 2; ++lat) {
        float theta = lat * PI / segments;
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);

        for (int lon = 0; lon <= segments; ++lon) {
            float phi = lon * 2.0f * PI / segments;
            Vertex v;
            v.nx = std::cos(phi) * sinTheta;
            v.ny = cosTheta;
            v.nz = std::sin(phi) * sinTheta;
            v.x = radius * v.nx;
            v.y = radius * v.ny + cylinderH * 0.5f;
            v.z = radius * v.nz;
            mesh.vertices.push_back(v);
        }
    }

    // Bottom hemisphere
    for (int lat = segments / 2; lat <= segments; ++lat) {
        float theta = lat * PI / segments;
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);

        for (int lon = 0; lon <= segments; ++lon) {
            float phi = lon * 2.0f * PI / segments;
            Vertex v;
            v.nx = std::cos(phi) * sinTheta;
            v.ny = cosTheta;
            v.nz = std::sin(phi) * sinTheta;
            v.x = radius * v.nx;
            v.y = radius * v.ny - cylinderH * 0.5f;
            v.z = radius * v.nz;
            mesh.vertices.push_back(v);
        }
    }

    // Indices for both hemispheres
    int totalRows = segments + 1;
    for (int lat = 0; lat < totalRows; ++lat) {
        for (int lon = 0; lon < segments; ++lon) {
            int first = lat * (segments + 1) + lon;
            int second = first + segments + 1;

            mesh.indices.push_back(first);
            mesh.indices.push_back(second);
            mesh.indices.push_back(first + 1);

            mesh.indices.push_back(second);
            mesh.indices.push_back(second + 1);
            mesh.indices.push_back(first + 1);
        }
    }

    return mesh;
}

Mesh PrimitiveGenerator::CreateTorus(float majorRadius, float minorRadius, int majorSegs, int minorSegs) {
    Mesh mesh;
    mesh.name = "Torus";

    for (int i = 0; i <= majorSegs; ++i) {
        float u = (float)i / majorSegs * 2.0f * PI;
        float cu = std::cos(u);
        float su = std::sin(u);

        for (int j = 0; j <= minorSegs; ++j) {
            float v = (float)j / minorSegs * 2.0f * PI;
            float cv = std::cos(v);
            float sv = std::sin(v);

            Vertex vert;
            vert.x = (majorRadius + minorRadius * cv) * cu;
            vert.y = minorRadius * sv;
            vert.z = (majorRadius + minorRadius * cv) * su;
            vert.nx = cv * cu;
            vert.ny = sv;
            vert.nz = cv * su;
            vert.u = (float)i / majorSegs;
            vert.v = (float)j / minorSegs;
            mesh.vertices.push_back(vert);
        }
    }

    for (int i = 0; i < majorSegs; ++i) {
        for (int j = 0; j < minorSegs; ++j) {
            int idx = i * (minorSegs + 1) + j;
            mesh.indices.push_back(idx);
            mesh.indices.push_back(idx + minorSegs + 1);
            mesh.indices.push_back(idx + 1);
            mesh.indices.push_back(idx + 1);
            mesh.indices.push_back(idx + minorSegs + 1);
            mesh.indices.push_back(idx + minorSegs + 2);
        }
    }

    return mesh;
}

// ============================================================================
// Terrain Generator
// ============================================================================

float TerrainGenerator::Noise2D(float x, float y, unsigned int seed) {
    int n = static_cast<int>(x) + static_cast<int>(y) * 57 + seed;
    n = (n << 13) ^ n;
    return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
}

float TerrainGenerator::PerlinNoise(float x, float y, unsigned int seed, int octaves, float persistence, float lacunarity) {
    float total = 0;
    float amplitude = 1;
    float frequency = 1;
    float maxValue = 0;

    for (int i = 0; i < octaves; ++i) {
        float sx = x * frequency;
        float sy = y * frequency;

        int x0 = static_cast<int>(std::floor(sx));
        int y0 = static_cast<int>(std::floor(sy));
        int x1 = x0 + 1;
        int y1 = y0 + 1;

        float fx = sx - x0;
        float fy = sy - y0;

        float v00 = Noise2D(x0, y0, seed);
        float v10 = Noise2D(x1, y0, seed);
        float v01 = Noise2D(x0, y1, seed);
        float v11 = Noise2D(x1, y1, seed);

        float i1 = v00 + fx * (v10 - v00);
        float i2 = v01 + fx * (v11 - v01);
        float value = i1 + fy * (i2 - i1);

        total += value * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total / maxValue;
}

Mesh TerrainGenerator::Generate(const TerrainSettings& settings) {
    Mesh mesh;
    mesh.name = "Terrain";

    // Generate vertices
    for (int z = 0; z <= settings.depth; ++z) {
        for (int x = 0; x <= settings.width; ++x) {
            float wx = x * settings.cellSize;
            float wz = z * settings.cellSize;

            float height = PerlinNoise(
                x * 0.1f, z * 0.1f,
                settings.seed,
                settings.octaves,
                settings.persistence,
                settings.lacunarity
            );

            height = (height + 1.0f) * 0.5f * settings.maxHeight;

            Vertex v;
            v.x = wx - (settings.width * settings.cellSize * 0.5f);
            v.y = height;
            v.z = wz - (settings.depth * settings.cellSize * 0.5f);
            v.u = (float)x / settings.width;
            v.v = (float)z / settings.depth;

            // Color based on height
            float normalizedH = height / settings.maxHeight;
            if (normalizedH < 0.2f) {
                v.r = 0.2f; v.g = 0.5f; v.b = 0.2f; // Grass
            } else if (normalizedH < 0.5f) {
                v.r = 0.4f; v.g = 0.4f; v.b = 0.3f; // Dirt
            } else if (normalizedH < 0.8f) {
                v.r = 0.5f; v.g = 0.5f; v.b = 0.5f; // Rock
            } else {
                v.r = 1.0f; v.g = 1.0f; v.b = 1.0f; // Snow
            }

            mesh.vertices.push_back(v);
        }
    }

    // Generate indices
    for (int z = 0; z < settings.depth; ++z) {
        for (int x = 0; x < settings.width; ++x) {
            int idx = z * (settings.width + 1) + x;
            mesh.indices.push_back(idx);
            mesh.indices.push_back(idx + settings.width + 1);
            mesh.indices.push_back(idx + 1);
            mesh.indices.push_back(idx + 1);
            mesh.indices.push_back(idx + settings.width + 1);
            mesh.indices.push_back(idx + settings.width + 2);
        }
    }

    mesh.CalculateNormals();
    return mesh;
}

Mesh TerrainGenerator::GenerateFlat(int width, int depth, float cellSize) {
    TerrainSettings settings;
    settings.width = width;
    settings.depth = depth;
    settings.cellSize = cellSize;
    settings.maxHeight = 0;
    return Generate(settings);
}

Mesh TerrainGenerator::GenerateIsland(const TerrainSettings& settings) {
    Mesh mesh = Generate(settings);

    // Apply island mask
    float centerX = settings.width * 0.5f;
    float centerZ = settings.depth * 0.5f;
    float maxDist = std::min(centerX, centerZ);

    for (auto& v : mesh.vertices) {
        float dx = (v.x / settings.cellSize) + centerX;
        float dz = (v.z / settings.cellSize) + centerZ;
        dx -= centerX;
        dz -= centerZ;
        float dist = std::sqrt(dx*dx + dz*dz);
        float factor = 1.0f - std::min(1.0f, dist / maxDist);
        factor = factor * factor;
        v.y *= factor;
    }

    mesh.CalculateNormals();
    return mesh;
}

Mesh TerrainGenerator::GenerateMountain(const TerrainSettings& settings) {
    TerrainSettings mtnSettings = settings;
    mtnSettings.maxHeight = settings.maxHeight * 2.0f;
    mtnSettings.octaves = 6;
    return Generate(mtnSettings);
}

Mesh TerrainGenerator::GenerateValley(const TerrainSettings& settings) {
    Mesh mesh = Generate(settings);

    float centerZ = settings.depth * 0.5f;

    for (auto& v : mesh.vertices) {
        float dz = std::abs((v.z / settings.cellSize) + centerZ - centerZ);
        float factor = 1.0f - std::exp(-dz * 0.1f);
        v.y *= factor;
    }

    mesh.CalculateNormals();
    return mesh;
}

// ============================================================================
// Weapon Factory
// ============================================================================

Mesh WeaponFactory::CreateWeapon(const WeaponSettings& settings) {
    switch (settings.type) {
        case WeaponType::Pistol: return CreatePistol(settings.scale);
        case WeaponType::Rifle: return CreateRifle(settings.scale);
        case WeaponType::Shotgun: return CreateShotgun(settings.scale);
        case WeaponType::SMG: return CreateSMG(settings.scale);
        case WeaponType::Sniper: return CreateSniper(settings.scale);
        case WeaponType::RocketLauncher: return CreateRocketLauncher(settings.scale);
        case WeaponType::Knife: return CreateKnife(settings.scale);
        default: return CreateRifle(settings.scale);
    }
}

Mesh WeaponFactory::CreatePistol(float scale) {
    Mesh mesh;
    mesh.name = "Pistol";

    // Grip
    Mesh grip = PrimitiveGenerator::CreateCube(0.04f);
    grip.Scale(1.0f, 1.5f, 0.8f);
    grip.Transform(0, -0.03f, 0);
    grip.SetColor(0.2f, 0.15f, 0.1f);
    mesh.Merge(grip);

    // Slide
    Mesh slide = PrimitiveGenerator::CreateCube(0.035f);
    slide.Scale(1.0f, 0.8f, 3.0f);
    slide.Transform(0, 0.025f, 0.04f);
    slide.SetColor(0.3f, 0.3f, 0.3f);
    mesh.Merge(slide);

    // Barrel
    Mesh barrel = PrimitiveGenerator::CreateCylinder(0.006f, 0.05f, 8);
    barrel.Rotate(PI * 0.5f, 0, 0);
    barrel.Transform(0, 0.025f, 0.1f);
    barrel.SetColor(0.25f, 0.25f, 0.25f);
    mesh.Merge(barrel);

    // Trigger guard
    Mesh guard = PrimitiveGenerator::CreateTorus(0.012f, 0.003f, 12, 6);
    guard.Scale(1.0f, 0.5f, 1.0f);
    guard.Transform(0, -0.015f, 0.01f);
    guard.SetColor(0.3f, 0.3f, 0.3f);
    mesh.Merge(guard);

    mesh.Scale(scale, scale, scale);
    return mesh;
}

Mesh WeaponFactory::CreateRifle(float scale) {
    Mesh mesh;
    mesh.name = "Rifle";

    // Stock
    Mesh stock = PrimitiveGenerator::CreateCube(0.03f);
    stock.Scale(0.8f, 1.2f, 3.0f);
    stock.Transform(0, 0, -0.08f);
    stock.SetColor(0.25f, 0.2f, 0.15f);
    mesh.Merge(stock);

    // Receiver
    Mesh receiver = PrimitiveGenerator::CreateCube(0.025f);
    receiver.Scale(1.0f, 1.2f, 4.0f);
    receiver.Transform(0, 0.01f, 0.06f);
    receiver.SetColor(0.2f, 0.2f, 0.2f);
    mesh.Merge(receiver);

    // Barrel
    Mesh barrel = PrimitiveGenerator::CreateCylinder(0.008f, 0.25f, 12);
    barrel.Rotate(PI * 0.5f, 0, 0);
    barrel.Transform(0, 0.015f, 0.22f);
    barrel.SetColor(0.15f, 0.15f, 0.15f);
    mesh.Merge(barrel);

    // Grip
    Mesh grip = PrimitiveGenerator::CreateCube(0.02f);
    grip.Scale(0.8f, 2.0f, 0.8f);
    grip.Rotate(0.3f, 0, 0);
    grip.Transform(0, -0.035f, 0.02f);
    grip.SetColor(0.25f, 0.2f, 0.15f);
    mesh.Merge(grip);

    // Magazine
    Mesh mag = PrimitiveGenerator::CreateCube(0.015f);
    mag.Scale(0.8f, 2.5f, 1.0f);
    mag.Transform(0, -0.04f, 0.05f);
    mag.SetColor(0.3f, 0.3f, 0.3f);
    mesh.Merge(mag);

    // Front sight
    Mesh fsight = PrimitiveGenerator::CreateCube(0.005f);
    fsight.Transform(0, 0.035f, 0.3f);
    fsight.SetColor(0.1f, 0.1f, 0.1f);
    mesh.Merge(fsight);

    // Rear sight
    Mesh rsight = PrimitiveGenerator::CreateCube(0.008f);
    rsight.Scale(1.5f, 1.0f, 0.5f);
    rsight.Transform(0, 0.035f, 0.08f);
    rsight.SetColor(0.1f, 0.1f, 0.1f);
    mesh.Merge(rsight);

    mesh.Scale(scale, scale, scale);
    return mesh;
}

Mesh WeaponFactory::CreateShotgun(float scale) {
    Mesh mesh;
    mesh.name = "Shotgun";

    // Stock
    Mesh stock = PrimitiveGenerator::CreateCube(0.035f);
    stock.Scale(0.9f, 1.3f, 4.0f);
    stock.Transform(0, -0.01f, -0.1f);
    stock.SetColor(0.35f, 0.25f, 0.15f);
    mesh.Merge(stock);

    // Receiver
    Mesh receiver = PrimitiveGenerator::CreateCube(0.03f);
    receiver.Scale(1.0f, 1.0f, 2.5f);
    receiver.Transform(0, 0.005f, 0.06f);
    receiver.SetColor(0.15f, 0.15f, 0.15f);
    mesh.Merge(receiver);

    // Barrel
    Mesh barrel = PrimitiveGenerator::CreateCylinder(0.012f, 0.3f, 12);
    barrel.Rotate(PI * 0.5f, 0, 0);
    barrel.Transform(0, 0.01f, 0.27f);
    barrel.SetColor(0.1f, 0.1f, 0.1f);
    mesh.Merge(barrel);

    // Pump
    Mesh pump = PrimitiveGenerator::CreateCylinder(0.015f, 0.08f, 8);
    pump.Rotate(PI * 0.5f, 0, 0);
    pump.Transform(0, -0.015f, 0.15f);
    pump.SetColor(0.3f, 0.25f, 0.2f);
    mesh.Merge(pump);

    mesh.Scale(scale, scale, scale);
    return mesh;
}

Mesh WeaponFactory::CreateSMG(float scale) {
    Mesh mesh;
    mesh.name = "SMG";

    // Body
    Mesh body = PrimitiveGenerator::CreateCube(0.025f);
    body.Scale(1.0f, 1.0f, 3.0f);
    body.Transform(0, 0, 0);
    body.SetColor(0.2f, 0.2f, 0.2f);
    mesh.Merge(body);

    // Stock (foldable style)
    Mesh stock = PrimitiveGenerator::CreateCube(0.01f);
    stock.Scale(0.5f, 1.0f, 2.0f);
    stock.Transform(0, 0.015f, -0.055f);
    stock.SetColor(0.15f, 0.15f, 0.15f);
    mesh.Merge(stock);

    // Barrel
    Mesh barrel = PrimitiveGenerator::CreateCylinder(0.006f, 0.1f, 8);
    barrel.Rotate(PI * 0.5f, 0, 0);
    barrel.Transform(0, 0, 0.09f);
    barrel.SetColor(0.1f, 0.1f, 0.1f);
    mesh.Merge(barrel);

    // Grip
    Mesh grip = PrimitiveGenerator::CreateCube(0.015f);
    grip.Scale(0.8f, 1.8f, 0.6f);
    grip.Rotate(0.2f, 0, 0);
    grip.Transform(0, -0.025f, 0);
    grip.SetColor(0.25f, 0.25f, 0.25f);
    mesh.Merge(grip);

    // Magazine
    Mesh mag = PrimitiveGenerator::CreateCube(0.012f);
    mag.Scale(0.6f, 2.5f, 0.8f);
    mag.Transform(0, -0.035f, 0.02f);
    mag.SetColor(0.3f, 0.3f, 0.3f);
    mesh.Merge(mag);

    mesh.Scale(scale, scale, scale);
    return mesh;
}

Mesh WeaponFactory::CreateSniper(float scale) {
    Mesh mesh;
    mesh.name = "Sniper";

    // Stock
    Mesh stock = PrimitiveGenerator::CreateCube(0.03f);
    stock.Scale(0.7f, 1.5f, 4.0f);
    stock.Transform(0, 0, -0.1f);
    stock.SetColor(0.25f, 0.2f, 0.15f);
    mesh.Merge(stock);

    // Receiver
    Mesh receiver = PrimitiveGenerator::CreateCube(0.025f);
    receiver.Scale(1.0f, 1.0f, 3.5f);
    receiver.Transform(0, 0.01f, 0.06f);
    receiver.SetColor(0.18f, 0.18f, 0.18f);
    mesh.Merge(receiver);

    // Long Barrel
    Mesh barrel = PrimitiveGenerator::CreateCylinder(0.01f, 0.4f, 12);
    barrel.Rotate(PI * 0.5f, 0, 0);
    barrel.Transform(0, 0.01f, 0.35f);
    barrel.SetColor(0.12f, 0.12f, 0.12f);
    mesh.Merge(barrel);

    // Scope
    Mesh scope = PrimitiveGenerator::CreateCylinder(0.015f, 0.12f, 12);
    scope.Rotate(PI * 0.5f, 0, 0);
    scope.Transform(0, 0.045f, 0.08f);
    scope.SetColor(0.1f, 0.1f, 0.1f);
    mesh.Merge(scope);

    // Scope lens front
    Mesh lens1 = PrimitiveGenerator::CreateCylinder(0.012f, 0.005f, 12);
    lens1.Rotate(PI * 0.5f, 0, 0);
    lens1.Transform(0, 0.045f, 0.145f);
    lens1.SetColor(0.3f, 0.5f, 0.7f);
    mesh.Merge(lens1);

    // Bipod
    Mesh bipod1 = PrimitiveGenerator::CreateCylinder(0.003f, 0.06f, 6);
    bipod1.Rotate(0.3f, 0, 0);
    bipod1.Transform(-0.015f, -0.03f, 0.2f);
    bipod1.SetColor(0.15f, 0.15f, 0.15f);
    mesh.Merge(bipod1);

    Mesh bipod2 = PrimitiveGenerator::CreateCylinder(0.003f, 0.06f, 6);
    bipod2.Rotate(0.3f, 0, 0);
    bipod2.Transform(0.015f, -0.03f, 0.2f);
    bipod2.SetColor(0.15f, 0.15f, 0.15f);
    mesh.Merge(bipod2);

    mesh.Scale(scale, scale, scale);
    return mesh;
}

Mesh WeaponFactory::CreateRocketLauncher(float scale) {
    Mesh mesh;
    mesh.name = "RocketLauncher";

    // Main tube
    Mesh tube = PrimitiveGenerator::CreateCylinder(0.04f, 0.5f, 16);
    tube.Rotate(PI * 0.5f, 0, 0);
    tube.SetColor(0.3f, 0.35f, 0.25f);
    mesh.Merge(tube);

    // Front cap
    Mesh front = PrimitiveGenerator::CreateCylinder(0.035f, 0.02f, 16);
    front.Rotate(PI * 0.5f, 0, 0);
    front.Transform(0, 0, 0.26f);
    front.SetColor(0.25f, 0.25f, 0.2f);
    mesh.Merge(front);

    // Rear cap
    Mesh rear = PrimitiveGenerator::CreateCylinder(0.035f, 0.02f, 16);
    rear.Rotate(PI * 0.5f, 0, 0);
    rear.Transform(0, 0, -0.26f);
    rear.SetColor(0.25f, 0.25f, 0.2f);
    mesh.Merge(rear);

    // Handle
    Mesh handle = PrimitiveGenerator::CreateCube(0.02f);
    handle.Scale(0.6f, 1.5f, 0.6f);
    handle.Transform(0, -0.05f, -0.05f);
    handle.SetColor(0.2f, 0.2f, 0.2f);
    mesh.Merge(handle);

    // Sight
    Mesh sight = PrimitiveGenerator::CreateCube(0.015f);
    sight.Scale(1.0f, 2.0f, 1.0f);
    sight.Transform(0, 0.055f, 0.1f);
    sight.SetColor(0.15f, 0.15f, 0.15f);
    mesh.Merge(sight);

    mesh.Scale(scale, scale, scale);
    return mesh;
}

Mesh WeaponFactory::CreateKnife(float scale) {
    Mesh mesh;
    mesh.name = "Knife";

    // Handle
    Mesh handle = PrimitiveGenerator::CreateCylinder(0.012f, 0.1f, 8);
    handle.Rotate(PI * 0.5f, 0, 0);
    handle.Transform(0, 0, -0.05f);
    handle.SetColor(0.3f, 0.25f, 0.15f);
    mesh.Merge(handle);

    // Guard
    Mesh guard = PrimitiveGenerator::CreateCube(0.02f);
    guard.Scale(1.5f, 0.3f, 0.5f);
    guard.Transform(0, 0, 0);
    guard.SetColor(0.4f, 0.4f, 0.4f);
    mesh.Merge(guard);

    // Blade
    Mesh blade = PrimitiveGenerator::CreateCube(0.015f);
    blade.Scale(0.15f, 0.4f, 4.0f);
    blade.Transform(0, 0, 0.045f);
    blade.SetColor(0.8f, 0.8f, 0.8f);
    mesh.Merge(blade);

    mesh.Scale(scale, scale, scale);
    return mesh;
}

// ============================================================================
// Character Factory
// ============================================================================

CharacterFactory::CharacterMeshes CharacterFactory::CreateCharacter(const CharacterSettings& settings) {
    CharacterMeshes meshes;

    float bodyHeight = settings.height * 0.35f;
    float legOffset = settings.height * 0.5f - bodyHeight * 0.5f;

    // Body (torso)
    meshes.body = PrimitiveGenerator::CreateCube(1.0f);
    meshes.body.Scale(settings.shoulderWidth, bodyHeight, settings.shoulderWidth * 0.6f);
    meshes.body.Transform(0, bodyHeight * 0.5f, 0);
    meshes.body.SetColor(0.3f, 0.35f, 0.25f);

    // Head
    meshes.head = PrimitiveGenerator::CreateSphere(settings.headSize, 12);
    meshes.head.Transform(0, bodyHeight + settings.headSize, 0);
    meshes.head.SetColor(0.85f, 0.75f, 0.65f);

    // Arms
    float armWidth = 0.08f;
    meshes.leftArm = PrimitiveGenerator::CreateCapsule(armWidth, settings.armLength, 8);
    meshes.leftArm.Transform(-settings.shoulderWidth * 0.5f - armWidth, bodyHeight * 0.7f, 0);
    meshes.leftArm.SetColor(0.3f, 0.35f, 0.25f);

    meshes.rightArm = PrimitiveGenerator::CreateCapsule(armWidth, settings.armLength, 8);
    meshes.rightArm.Transform(settings.shoulderWidth * 0.5f + armWidth, bodyHeight * 0.7f, 0);
    meshes.rightArm.SetColor(0.3f, 0.35f, 0.25f);

    // Legs
    float legWidth = 0.1f;
    meshes.leftLeg = PrimitiveGenerator::CreateCapsule(legWidth, settings.legLength, 8);
    meshes.leftLeg.Transform(-settings.hipWidth * 0.4f, -legOffset, 0);
    meshes.leftLeg.SetColor(0.25f, 0.25f, 0.2f);

    meshes.rightLeg = PrimitiveGenerator::CreateCapsule(legWidth, settings.legLength, 8);
    meshes.rightLeg.Transform(settings.hipWidth * 0.4f, -legOffset, 0);
    meshes.rightLeg.SetColor(0.25f, 0.25f, 0.2f);

    // Combined mesh
    meshes.combined = meshes.body;
    meshes.combined.Merge(meshes.head);
    meshes.combined.Merge(meshes.leftArm);
    meshes.combined.Merge(meshes.rightArm);
    meshes.combined.Merge(meshes.leftLeg);
    meshes.combined.Merge(meshes.rightLeg);
    meshes.combined.name = "Character";

    return meshes;
}

Mesh CharacterFactory::CreateSoldier(float height) {
    CharacterSettings settings;
    settings.height = height;
    auto meshes = CreateCharacter(settings);

    // Add helmet
    Mesh helmet = PrimitiveGenerator::CreateSphere(settings.headSize * 1.1f, 12);
    helmet.Scale(1.0f, 0.8f, 1.0f);
    helmet.Transform(0, height * 0.35f + settings.headSize * 1.1f, 0);
    helmet.SetColor(0.3f, 0.35f, 0.25f);
    meshes.combined.Merge(helmet);

    meshes.combined.name = "Soldier";
    return meshes.combined;
}

Mesh CharacterFactory::CreateRobot(float height) {
    Mesh mesh;
    mesh.name = "Robot";

    float scale = height / 2.0f;

    // Torso
    Mesh torso = PrimitiveGenerator::CreateCube(0.4f * scale);
    torso.Scale(1.0f, 1.2f, 0.6f);
    torso.Transform(0, 0.5f * scale, 0);
    torso.SetColor(0.6f, 0.6f, 0.6f);
    mesh.Merge(torso);

    // Head
    Mesh head = PrimitiveGenerator::CreateCube(0.2f * scale);
    head.Transform(0, 0.85f * scale, 0);
    head.SetColor(0.5f, 0.5f, 0.55f);
    mesh.Merge(head);

    // Eyes
    Mesh eye1 = PrimitiveGenerator::CreateSphere(0.03f * scale, 8);
    eye1.Transform(-0.05f * scale, 0.87f * scale, 0.1f * scale);
    eye1.SetColor(1.0f, 0.2f, 0.2f);
    mesh.Merge(eye1);

    Mesh eye2 = PrimitiveGenerator::CreateSphere(0.03f * scale, 8);
    eye2.Transform(0.05f * scale, 0.87f * scale, 0.1f * scale);
    eye2.SetColor(1.0f, 0.2f, 0.2f);
    mesh.Merge(eye2);

    // Arms
    Mesh arm1 = PrimitiveGenerator::CreateCylinder(0.05f * scale, 0.5f * scale, 8);
    arm1.Transform(-0.25f * scale, 0.45f * scale, 0);
    arm1.SetColor(0.55f, 0.55f, 0.55f);
    mesh.Merge(arm1);

    Mesh arm2 = PrimitiveGenerator::CreateCylinder(0.05f * scale, 0.5f * scale, 8);
    arm2.Transform(0.25f * scale, 0.45f * scale, 0);
    arm2.SetColor(0.55f, 0.55f, 0.55f);
    mesh.Merge(arm2);

    // Legs
    Mesh leg1 = PrimitiveGenerator::CreateCylinder(0.06f * scale, 0.6f * scale, 8);
    leg1.Transform(-0.1f * scale, -0.1f * scale, 0);
    leg1.SetColor(0.5f, 0.5f, 0.5f);
    mesh.Merge(leg1);

    Mesh leg2 = PrimitiveGenerator::CreateCylinder(0.06f * scale, 0.6f * scale, 8);
    leg2.Transform(0.1f * scale, -0.1f * scale, 0);
    leg2.SetColor(0.5f, 0.5f, 0.5f);
    mesh.Merge(leg2);

    return mesh;
}

// ============================================================================
// Building Factory
// ============================================================================

Mesh BuildingFactory::CreateBuilding(const BuildingSettings& settings) {
    Mesh mesh;
    mesh.name = "Building";

    // Main structure
    Mesh walls = PrimitiveGenerator::CreateCube(1.0f);
    walls.Scale(settings.width, settings.height, settings.depth);
    walls.Transform(0, settings.height * 0.5f, 0);
    walls.SetColor(0.7f, 0.7f, 0.65f);
    mesh.Merge(walls);

    // Windows
    if (settings.hasWindows) {
        float windowSize = 1.0f;
        float windowSpacing = 3.0f;
        float floorHeight = settings.height / settings.floors;

        for (int floor = 0; floor < settings.floors; ++floor) {
            float y = (floor + 0.5f) * floorHeight;

            // Front windows
            for (float x = -settings.width * 0.4f; x <= settings.width * 0.4f; x += windowSpacing) {
                Mesh window = PrimitiveGenerator::CreateCube(windowSize);
                window.Scale(0.8f, 1.2f, 0.1f);
                window.Transform(x, y, settings.depth * 0.51f);
                window.SetColor(0.3f, 0.4f, 0.5f);
                mesh.Merge(window);
            }
        }
    }

    // Door
    if (settings.hasDoor) {
        Mesh door = PrimitiveGenerator::CreateCube(1.0f);
        door.Scale(1.5f, 2.5f, 0.2f);
        door.Transform(0, 1.25f, settings.depth * 0.51f);
        door.SetColor(0.4f, 0.25f, 0.15f);
        mesh.Merge(door);
    }

    // Roof
    if (settings.hasRoof) {
        Mesh roof = PrimitiveGenerator::CreateCube(1.0f);
        roof.Scale(settings.width + 0.5f, 0.3f, settings.depth + 0.5f);
        roof.Transform(0, settings.height + 0.15f, 0);
        roof.SetColor(0.4f, 0.3f, 0.25f);
        mesh.Merge(roof);
    }

    return mesh;
}

Mesh BuildingFactory::CreateHouse(float width, float depth) {
    BuildingSettings settings;
    settings.width = width;
    settings.depth = depth;
    settings.height = 6.0f;
    settings.floors = 2;
    return CreateBuilding(settings);
}

Mesh BuildingFactory::CreateWarehouse(float width, float depth) {
    BuildingSettings settings;
    settings.width = width;
    settings.depth = depth;
    settings.height = 8.0f;
    settings.floors = 1;
    settings.hasWindows = false;
    return CreateBuilding(settings);
}

Mesh BuildingFactory::CreateTower(float radius, float height) {
    Mesh mesh;
    mesh.name = "Tower";

    Mesh body = PrimitiveGenerator::CreateCylinder(radius, height, 16);
    body.Transform(0, height * 0.5f, 0);
    body.SetColor(0.6f, 0.6f, 0.55f);
    mesh.Merge(body);

    // Battlements
    for (int i = 0; i < 8; ++i) {
        float angle = i * PI * 0.25f;
        Mesh battlement = PrimitiveGenerator::CreateCube(radius * 0.3f);
        battlement.Scale(1.0f, 2.0f, 0.5f);
        battlement.Transform(
            std::cos(angle) * radius * 0.9f,
            height + radius * 0.3f,
            std::sin(angle) * radius * 0.9f
        );
        battlement.SetColor(0.55f, 0.55f, 0.5f);
        mesh.Merge(battlement);
    }

    return mesh;
}

Mesh BuildingFactory::CreateBunker(float width, float depth) {
    Mesh mesh;
    mesh.name = "Bunker";

    // Main structure (partially buried)
    Mesh body = PrimitiveGenerator::CreateCube(1.0f);
    body.Scale(width, 2.0f, depth);
    body.Transform(0, 0.5f, 0);
    body.SetColor(0.5f, 0.5f, 0.45f);
    mesh.Merge(body);

    // Gun slot
    Mesh slot = PrimitiveGenerator::CreateCube(0.3f);
    slot.Scale(2.0f, 0.5f, 0.5f);
    slot.Transform(0, 1.5f, depth * 0.51f);
    slot.SetColor(0.2f, 0.2f, 0.2f);
    mesh.Merge(slot);

    return mesh;
}

Mesh BuildingFactory::CreateWall(float length, float height, float thickness) {
    Mesh mesh;
    mesh.name = "Wall";

    Mesh wall = PrimitiveGenerator::CreateCube(1.0f);
    wall.Scale(length, height, thickness);
    wall.Transform(0, height * 0.5f, 0);
    wall.SetColor(0.6f, 0.6f, 0.55f);
    mesh.Merge(wall);

    return mesh;
}

// ============================================================================
// Vehicle Factory
// ============================================================================

Mesh VehicleFactory::CreateTank(float scale) {
    Mesh mesh;
    mesh.name = "Tank";

    // Hull
    Mesh hull = PrimitiveGenerator::CreateCube(1.0f);
    hull.Scale(2.5f * scale, 0.8f * scale, 4.0f * scale);
    hull.Transform(0, 0.6f * scale, 0);
    hull.SetColor(0.35f, 0.4f, 0.3f);
    mesh.Merge(hull);

    // Turret
    Mesh turret = PrimitiveGenerator::CreateCylinder(0.8f * scale, 0.5f * scale, 12);
    turret.Transform(0, 1.2f * scale, 0);
    turret.SetColor(0.35f, 0.4f, 0.3f);
    mesh.Merge(turret);

    // Gun
    Mesh gun = PrimitiveGenerator::CreateCylinder(0.1f * scale, 2.5f * scale, 8);
    gun.Rotate(PI * 0.5f, 0, 0);
    gun.Transform(0, 1.3f * scale, 1.8f * scale);
    gun.SetColor(0.3f, 0.35f, 0.25f);
    mesh.Merge(gun);

    // Tracks
    for (float side : {-1.0f, 1.0f}) {
        Mesh track = PrimitiveGenerator::CreateCube(1.0f);
        track.Scale(0.5f * scale, 0.6f * scale, 4.2f * scale);
        track.Transform(side * 1.3f * scale, 0.3f * scale, 0);
        track.SetColor(0.2f, 0.2f, 0.2f);
        mesh.Merge(track);
    }

    return mesh;
}

Mesh VehicleFactory::CreateJeep(float scale) {
    Mesh mesh;
    mesh.name = "Jeep";

    // Body
    Mesh body = PrimitiveGenerator::CreateCube(1.0f);
    body.Scale(1.8f * scale, 0.6f * scale, 3.5f * scale);
    body.Transform(0, 0.6f * scale, 0);
    body.SetColor(0.35f, 0.4f, 0.3f);
    mesh.Merge(body);

    // Cabin
    Mesh cabin = PrimitiveGenerator::CreateCube(1.0f);
    cabin.Scale(1.6f * scale, 0.8f * scale, 1.5f * scale);
    cabin.Transform(0, 1.1f * scale, -0.5f * scale);
    cabin.SetColor(0.35f, 0.4f, 0.3f);
    mesh.Merge(cabin);

    // Wheels
    float wheelR = 0.35f * scale;
    float wheelW = 0.15f * scale;
    float wheelX = 0.9f * scale;
    float wheelZ[] = {1.2f * scale, -1.0f * scale};

    for (float z : wheelZ) {
        for (float x : {-wheelX, wheelX}) {
            Mesh wheel = PrimitiveGenerator::CreateCylinder(wheelR, wheelW, 12);
            wheel.Rotate(0, 0, PI * 0.5f);
            wheel.Transform(x, wheelR, z);
            wheel.SetColor(0.15f, 0.15f, 0.15f);
            mesh.Merge(wheel);
        }
    }

    return mesh;
}

Mesh VehicleFactory::CreateHelicopter(float scale) {
    Mesh mesh;
    mesh.name = "Helicopter";

    // Fuselage
    Mesh fuselage = PrimitiveGenerator::CreateCapsule(0.8f * scale, 4.0f * scale, 12);
    fuselage.Rotate(0, 0, PI * 0.5f);
    fuselage.SetColor(0.35f, 0.4f, 0.3f);
    mesh.Merge(fuselage);

    // Tail boom
    Mesh tail = PrimitiveGenerator::CreateCylinder(0.15f * scale, 3.0f * scale, 8);
    tail.Rotate(0, 0, PI * 0.5f);
    tail.Transform(-2.5f * scale, 0.3f * scale, 0);
    tail.SetColor(0.35f, 0.4f, 0.3f);
    mesh.Merge(tail);

    // Main rotor
    for (int i = 0; i < 4; ++i) {
        float angle = i * PI * 0.5f;
        Mesh blade = PrimitiveGenerator::CreateCube(0.1f * scale);
        blade.Scale(25.0f, 0.3f, 1.5f);
        blade.Rotate(0, angle, 0);
        blade.Transform(0, 1.0f * scale, 0);
        blade.SetColor(0.3f, 0.3f, 0.3f);
        mesh.Merge(blade);
    }

    // Tail rotor
    for (int i = 0; i < 2; ++i) {
        Mesh blade = PrimitiveGenerator::CreateCube(0.05f * scale);
        blade.Scale(5.0f, 0.3f, 1.0f);
        blade.Rotate(0, 0, i * PI * 0.5f);
        blade.Transform(-4.0f * scale, 0.4f * scale, 0.3f * scale);
        blade.SetColor(0.3f, 0.3f, 0.3f);
        mesh.Merge(blade);
    }

    // Skids
    for (float x : {-0.6f * scale, 0.6f * scale}) {
        Mesh skid = PrimitiveGenerator::CreateCylinder(0.05f * scale, 2.5f * scale, 6);
        skid.Rotate(0, 0, PI * 0.5f);
        skid.Transform(x, -0.8f * scale, 0);
        skid.SetColor(0.2f, 0.2f, 0.2f);
        mesh.Merge(skid);
    }

    return mesh;
}

Mesh VehicleFactory::CreatePlane(float scale) {
    Mesh mesh;
    mesh.name = "Plane";

    // Fuselage
    Mesh fuselage = PrimitiveGenerator::CreateCapsule(0.5f * scale, 6.0f * scale, 12);
    fuselage.Rotate(0, 0, PI * 0.5f);
    fuselage.SetColor(0.6f, 0.6f, 0.6f);
    mesh.Merge(fuselage);

    // Wings
    Mesh wing = PrimitiveGenerator::CreateCube(0.1f * scale);
    wing.Scale(60.0f, 0.3f, 8.0f);
    wing.SetColor(0.6f, 0.6f, 0.6f);
    mesh.Merge(wing);

    // Tail wing
    Mesh tailWing = PrimitiveGenerator::CreateCube(0.08f * scale);
    tailWing.Scale(20.0f, 0.3f, 5.0f);
    tailWing.Transform(-2.5f * scale, 0, 0);
    tailWing.SetColor(0.6f, 0.6f, 0.6f);
    mesh.Merge(tailWing);

    // Vertical stabilizer
    Mesh vStab = PrimitiveGenerator::CreateCube(0.08f * scale);
    vStab.Scale(0.3f, 8.0f, 5.0f);
    vStab.Transform(-2.5f * scale, 0.35f * scale, 0);
    vStab.SetColor(0.6f, 0.6f, 0.6f);
    mesh.Merge(vStab);

    // Cockpit
    Mesh cockpit = PrimitiveGenerator::CreateSphere(0.35f * scale, 10);
    cockpit.Scale(1.0f, 0.8f, 1.5f);
    cockpit.Transform(1.0f * scale, 0.25f * scale, 0);
    cockpit.SetColor(0.3f, 0.4f, 0.5f);
    mesh.Merge(cockpit);

    return mesh;
}

Mesh VehicleFactory::CreateBoat(float scale) {
    Mesh mesh;
    mesh.name = "Boat";

    // Hull
    Mesh hull = PrimitiveGenerator::CreateCube(1.0f);
    hull.Scale(2.0f * scale, 0.8f * scale, 5.0f * scale);
    hull.Transform(0, -0.2f * scale, 0);
    hull.SetColor(0.4f, 0.4f, 0.4f);
    mesh.Merge(hull);

    // Deck
    Mesh deck = PrimitiveGenerator::CreateCube(1.0f);
    deck.Scale(1.8f * scale, 0.1f * scale, 4.5f * scale);
    deck.Transform(0, 0.25f * scale, 0);
    deck.SetColor(0.5f, 0.4f, 0.3f);
    mesh.Merge(deck);

    // Cabin
    Mesh cabin = PrimitiveGenerator::CreateCube(1.0f);
    cabin.Scale(1.0f * scale, 1.0f * scale, 1.5f * scale);
    cabin.Transform(0, 0.8f * scale, -0.5f * scale);
    cabin.SetColor(0.45f, 0.45f, 0.45f);
    mesh.Merge(cabin);

    return mesh;
}

// ============================================================================
// Environment Factory
// ============================================================================

Mesh EnvironmentFactory::CreateTree(float height) {
    Mesh mesh;
    mesh.name = "Tree";

    // Trunk
    Mesh trunk = PrimitiveGenerator::CreateCylinder(height * 0.05f, height * 0.4f, 8);
    trunk.Transform(0, height * 0.2f, 0);
    trunk.SetColor(0.4f, 0.25f, 0.15f);
    mesh.Merge(trunk);

    // Foliage
    Mesh foliage = PrimitiveGenerator::CreateCone(height * 0.25f, height * 0.6f, 8);
    foliage.Transform(0, height * 0.6f, 0);
    foliage.SetColor(0.2f, 0.5f, 0.2f);
    mesh.Merge(foliage);

    return mesh;
}

Mesh EnvironmentFactory::CreateRock(float size) {
    Mesh mesh = PrimitiveGenerator::CreateSphere(size, 6);
    mesh.name = "Rock";
    mesh.Scale(1.0f, 0.7f, 1.2f);

    // Add noise to vertices
    for (auto& v : mesh.vertices) {
        float noise = 0.8f + 0.4f * ((float)(rand() % 100) / 100.0f);
        v.x *= noise;
        v.y *= noise;
        v.z *= noise;
    }

    mesh.SetColor(0.5f, 0.5f, 0.45f);
    mesh.CalculateNormals();
    return mesh;
}

Mesh EnvironmentFactory::CreateBarrel(float radius, float height) {
    Mesh mesh = PrimitiveGenerator::CreateCylinder(radius, height, 12);
    mesh.name = "Barrel";
    mesh.Transform(0, height * 0.5f, 0);
    mesh.SetColor(0.4f, 0.25f, 0.15f);
    return mesh;
}

Mesh EnvironmentFactory::CreateCrate(float size) {
    Mesh mesh = PrimitiveGenerator::CreateCube(size);
    mesh.name = "Crate";
    mesh.Transform(0, size * 0.5f, 0);
    mesh.SetColor(0.6f, 0.5f, 0.3f);
    return mesh;
}

Mesh EnvironmentFactory::CreateSandbags(float width) {
    Mesh mesh;
    mesh.name = "Sandbags";

    for (int row = 0; row < 2; ++row) {
        for (int i = 0; i < 3; ++i) {
            Mesh bag = PrimitiveGenerator::CreateCapsule(0.15f, 0.6f, 8);
            bag.Rotate(0, 0, PI * 0.5f);
            float offset = (row % 2 == 0) ? 0 : 0.3f;
            bag.Transform(-width * 0.3f + i * 0.35f + offset, row * 0.28f + 0.15f, 0);
            bag.SetColor(0.6f, 0.55f, 0.4f);
            mesh.Merge(bag);
        }
    }

    return mesh;
}

Mesh EnvironmentFactory::CreateFence(float length, float height) {
    Mesh mesh;
    mesh.name = "Fence";

    int posts = (int)(length / 2.0f) + 1;
    float spacing = length / (posts - 1);

    // Posts
    for (int i = 0; i < posts; ++i) {
        Mesh post = PrimitiveGenerator::CreateCylinder(0.05f, height, 6);
        post.Transform(-length * 0.5f + i * spacing, height * 0.5f, 0);
        post.SetColor(0.4f, 0.25f, 0.15f);
        mesh.Merge(post);
    }

    // Horizontal bars
    for (float h : {height * 0.3f, height * 0.7f}) {
        Mesh bar = PrimitiveGenerator::CreateCylinder(0.03f, length, 6);
        bar.Rotate(0, 0, PI * 0.5f);
        bar.Transform(0, h, 0);
        bar.SetColor(0.4f, 0.25f, 0.15f);
        mesh.Merge(bar);
    }

    return mesh;
}

// ============================================================================
// Renderer bridge
// ============================================================================

Renderer::MeshData ToMeshData(const Mesh& mesh) {
    Renderer::MeshData data;
    data.vertices.reserve(mesh.vertices.size());

    bool has_normals = false;
    for (const auto& v : mesh.vertices) {
        Renderer::Vertex out;
        out.position = Math::Vector3(v.x, v.y, v.z);
        out.normal = Math::Vector3(v.nx, v.ny, v.nz);
        out.color = Math::Vector3(v.r, v.g, v.b);
        out.tex_coord = Math::Vector2(v.u, v.v);
        has_normals |= out.normal.LengthSquared() > 1e-8f;
        data.vertices.push_back(out);
    }

    const size_t vertex_count = data.vertices.size();
    data.indices.reserve(mesh.indices.size());
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32 a = mesh.indices[i];
        uint32 b = mesh.indices[i + 1];
        uint32 c = mesh.indices[i + 2];
        if (a >= vertex_count || b >= vertex_count || c >= vertex_count) continue;

        if (has_normals) {
            const Math::Vector3 face = Math::Vector3::Cross(data.vertices[b].position - data.vertices[a].position,
                                                            data.vertices[c].position - data.vertices[a].position);
            const Math::Vector3 authored = data.vertices[a].normal + data.vertices[b].normal + data.vertices[c].normal;
            if (Math::Vector3::Dot(face, authored) < 0.0f) {
                std::swap(b, c);
            }
        }
        data.indices.insert(data.indices.end(), {a, b, c});
    }

    if (!has_normals) {
        data.ComputeNormals();
    } else {
        for (auto& v : data.vertices) {
            v.normal = v.normal.LengthSquared() > 1e-8f ? v.normal.Normalized() : Math::Vector3::Up;
        }
    }
    return data;
}

} // namespace DRX::Procedural

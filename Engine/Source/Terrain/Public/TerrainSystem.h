/**
 * DRX Engine - Terrain System
 * Heightmap terrain with chunked LOD meshes, procedural generation, sculpting,
 * height/slope based colouring, foliage scattering and physics collision data.
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector2.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Vector4.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Matrix4.h"
#include "Renderer/Public/RenderTypes.h"

namespace DRX::Renderer { class SceneRenderer; }

namespace DRX::Terrain {

/**
 * Normalised heightmap (values usually 0..1, scaled by TerrainConfig::height_scale)
 */
class TerrainHeightmap {
public:
    bool Create(uint32 width, uint32 height, float32 default_height = 0.0f);

    // 16-bit RAW (square, little endian) or any image format supported by stb_image
    bool LoadFromFile(const String& path);

    uint32 GetWidth() const { return width_; }
    uint32 GetHeight() const { return height_; }
    const Array<float32>& GetData() const { return data_; }

    float32 GetHeight(uint32 x, uint32 y) const;
    float32 GetHeightBilinear(float32 u, float32 v) const;   // u, v in [0, 1]
    void SetHeight(uint32 x, uint32 y, float32 height);

    // Rescales to a new resolution with bilinear filtering
    void Resample(uint32 width, uint32 height);
    // Rescales values into [0, 1]
    void Normalize();

    // Sculpting brushes (centre and radius in heightmap pixels)
    void Flatten(uint32 x, uint32 y, uint32 radius, float32 target_height, float32 strength);
    void Raise(uint32 x, uint32 y, uint32 radius, float32 amount, float32 falloff);
    void Lower(uint32 x, uint32 y, uint32 radius, float32 amount, float32 falloff);
    void Smooth(uint32 x, uint32 y, uint32 radius, float32 strength);
    void Noise(uint32 x, uint32 y, uint32 radius, float32 strength, uint32 seed);

    // Procedural generation (deterministic for a given seed)
    void GeneratePerlinNoise(float32 frequency, int32 octaves, float32 persistence, uint32 seed);
    void GenerateDiamondSquare(float32 roughness, uint32 seed);
    void ApplyHydraulicErosion(int32 iterations, uint32 seed);

private:
    template<typename Fn>
    void ForEachInBrush(uint32 cx, uint32 cy, uint32 radius, Fn&& fn);

    uint32 width_ = 0;
    uint32 height_ = 0;
    Array<float32> data_;
};

/**
 * Per-layer weights painted onto the terrain (normalised per texel)
 */
class TerrainSplatMap {
public:
    bool Create(uint32 width, uint32 height, uint32 num_layers);

    uint32 GetWidth() const { return width_; }
    uint32 GetHeight() const { return height_; }
    uint32 GetNumLayers() const { return num_layers_; }
    bool IsEmpty() const { return num_layers_ == 0; }

    float32 GetWeight(uint32 x, uint32 y, uint32 layer) const;
    float32 GetWeightBilinear(float32 u, float32 v, uint32 layer) const;
    void SetWeight(uint32 x, uint32 y, uint32 layer, float32 weight);
    void Paint(uint32 x, uint32 y, uint32 radius, uint32 layer, float32 strength, float32 falloff);

    // RGBA8 image holding the weights of layers [layer_start, layer_start + 4)
    Array<uint8> GeneratePixels(uint32 layer_start) const;

private:
    void NormalizeWeights(uint32 x, uint32 y);

    uint32 width_ = 0;
    uint32 height_ = 0;
    uint32 num_layers_ = 0;
    Array<Array<float32>> layer_weights_;
};

/**
 * Surface layer. Without a painted splat map, layers are assigned by height and slope.
 */
struct TerrainLayer {
    String name;
    Math::Vector3 color = Math::Vector3::One;
    float32 min_height = -1e9f;     // world units
    float32 max_height = 1e9f;
    float32 min_slope = 0.0f;       // degrees
    float32 max_slope = 90.0f;
    float32 blend = 4.0f;           // softness of the height/slope transition (world units / degrees)
};

struct TerrainConfig {
    float32 size_x = 512.0f;            // world size
    float32 size_z = 512.0f;
    float32 height_scale = 60.0f;       // heightmap value 1.0 -> this many units
    Math::Vector3 origin = Math::Vector3(-256.0f, 0.0f, -256.0f);  // world position of heightmap (0, 0)

    uint32 chunks_x = 8;
    uint32 chunks_z = 8;
    uint32 chunk_resolution = 32;       // quads per chunk edge at LOD 0 (power of two)
    uint32 lod_levels = 3;
    Array<float32> lod_distances = {80.0f, 180.0f, 400.0f};  // switch to LOD n+1 beyond distance n
    float32 skirt_depth = 2.0f;         // hides cracks between chunks of different LOD

    float32 roughness = 0.95f;
};

/**
 * One chunk of the terrain grid with meshes for every LOD
 */
class TerrainChunk {
public:
    TerrainChunk(uint32 x, uint32 z) : chunk_x_(x), chunk_z_(z) {}

    uint32 GetX() const { return chunk_x_; }
    uint32 GetZ() const { return chunk_z_; }
    const Renderer::AABB& GetBounds() const { return bounds_; }
    uint32 GetCurrentLOD() const { return current_lod_; }
    const Array<MeshHandle>& GetLODMeshes() const { return lod_meshes_; }

private:
    friend class Terrain;

    uint32 chunk_x_;
    uint32 chunk_z_;
    uint32 current_lod_ = 0;
    Renderer::AABB bounds_;
    Array<MeshHandle> lod_meshes_;
};

/**
 * Scattered mesh instances (trees, rocks, grass clumps)
 */
struct FoliageType {
    String name;
    MeshHandle mesh;
    Renderer::Material material;
    float32 density = 1.0f;          // instances per 100 m^2
    float32 min_scale = 0.8f;
    float32 max_scale = 1.2f;
    float32 min_slope = 0.0f;        // degrees
    float32 max_slope = 30.0f;
    float32 min_height = -1e9f;      // world units
    float32 max_height = 1e9f;
    bool align_to_normal = false;
    bool random_rotation = true;
    float32 render_distance = 250.0f;
};

struct FoliageInstance {
    Math::Vector3 position;
    Math::Quaternion rotation;
    float32 scale = 1.0f;
};

class Terrain;

class TerrainFoliage {
public:
    void AddType(const FoliageType& type);
    void RemoveType(const String& name);
    void Clear();

    void Generate(const Terrain& terrain, uint32 seed);
    void Render(Renderer::SceneRenderer& renderer, const Math::Vector3& camera_position) const;

    uint32 GetInstanceCount() const;
    const Array<FoliageInstance>* GetInstances(const String& type) const;

private:
    Array<FoliageType> types_;
    HashMap<String, Array<FoliageInstance>> instances_;
};

/**
 * Terrain: heightmap + chunked meshes + optional splat map and foliage
 */
class Terrain {
public:
    Terrain() = default;
    ~Terrain() = default;

    bool Initialize(const TerrainConfig& config);

    // Replaces the heightmap (resampled to the chunk grid) and rebuilds the CPU meshes
    void SetHeightmap(const TerrainHeightmap& heightmap);
    TerrainHeightmap& GetHeightmap() { return heightmap_; }
    const TerrainHeightmap& GetHeightmap() const { return heightmap_; }

    // Call after sculpting the heightmap directly
    void RebuildMeshData();

    void SetLayers(const Array<TerrainLayer>& layers);
    const Array<TerrainLayer>& GetLayers() const { return layers_; }
    static Array<TerrainLayer> DefaultLayers(float32 height_scale);

    TerrainSplatMap& GetSplatMap() { return splatmap_; }
    TerrainFoliage& GetFoliage() { return foliage_; }
    const TerrainFoliage& GetFoliage() const { return foliage_; }

    // GPU meshes (call after any rebuild); safe to call repeatedly
    bool UploadMeshes(Renderer::SceneRenderer& renderer);
    void ReleaseMeshes(Renderer::SceneRenderer& renderer);

    // Selects LODs and submits visible chunks plus foliage
    void Render(Renderer::SceneRenderer& renderer, const Math::Vector3& camera_position);

    // Queries in world space
    float32 GetHeightAt(float32 x, float32 z) const;
    Math::Vector3 GetNormalAt(float32 x, float32 z) const;
    float32 GetSlopeAt(float32 x, float32 z) const;   // degrees
    bool IsInside(float32 x, float32 z) const;
    bool Raycast(const Math::Vector3& origin, const Math::Vector3& direction, float32 max_distance,
                 float32& out_distance, Math::Vector3& out_point) const;

    // Triangle mesh for physics (step = heightmap pixels per vertex)
    Renderer::MeshData BuildCollisionMesh(uint32 step = 2) const;

    const TerrainConfig& GetConfig() const { return config_; }
    Renderer::AABB GetBounds() const;
    const Array<UniquePtr<TerrainChunk>>& GetChunks() const { return chunks_; }

    Renderer::Material material;

private:
    Renderer::MeshData BuildChunkMesh(uint32 chunk_x, uint32 chunk_z, uint32 lod) const;
    Math::Vector3 ComputeVertexColor(float32 u, float32 v, float32 height, float32 slope_degrees) const;
    Math::Vector3 HeightmapNormal(float32 u, float32 v) const;
    uint32 HeightmapResolutionX() const { return config_.chunks_x * config_.chunk_resolution + 1; }
    uint32 HeightmapResolutionZ() const { return config_.chunks_z * config_.chunk_resolution + 1; }

    TerrainConfig config_;
    TerrainHeightmap heightmap_;
    TerrainSplatMap splatmap_;
    TerrainFoliage foliage_;
    Array<TerrainLayer> layers_;
    Array<UniquePtr<TerrainChunk>> chunks_;
    Array<Array<Renderer::MeshData>> chunk_mesh_data_;   // [chunk][lod]
    bool meshes_dirty_ = true;
};

} // namespace DRX::Terrain

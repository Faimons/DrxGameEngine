/**
 * DRX Engine - Terrain System
 * Heightmap-based terrain with LOD and streaming
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector2.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Matrix4.h"
#include "Renderer/Public/Texture.h"
#include "Renderer/Public/Material.h"
#include "Renderer/Public/Mesh.h"

namespace DRX::Terrain {

/**
 * Terrain layer for texture splatting
 */
struct TerrainLayer {
    String name;
    SharedPtr<Renderer::Texture> albedo_texture;
    SharedPtr<Renderer::Texture> normal_texture;
    SharedPtr<Renderer::Texture> mask_texture;  // R: metallic, G: roughness, B: AO

    float32 tiling_x = 10.0f;
    float32 tiling_y = 10.0f;
    float32 offset_x = 0.0f;
    float32 offset_y = 0.0f;

    float32 metallic = 0.0f;
    float32 roughness = 0.8f;
};

/**
 * Terrain chunk - a tile of terrain
 */
class TerrainChunk {
public:
    TerrainChunk(int32 x, int32 z, uint32 resolution);
    ~TerrainChunk();

    bool Generate(const float32* heightmap_data, uint32 heightmap_width,
                  float32 height_scale, float32 terrain_size);
    void UpdateLOD(const Math::Vector3& camera_pos);
    void Render(VkCommandBuffer cmd);

    int32 GetX() const { return chunk_x_; }
    int32 GetZ() const { return chunk_z_; }
    uint32 GetCurrentLOD() const { return current_lod_; }
    const Math::Vector3& GetCenter() const { return center_; }

    // Bounds
    Math::Vector3 GetMin() const { return bounds_min_; }
    Math::Vector3 GetMax() const { return bounds_max_; }

private:
    void GenerateMesh(uint32 lod);
    void GenerateNormals(Array<Renderer::StaticVertex>& vertices, const Array<uint32>& indices);

    int32 chunk_x_, chunk_z_;
    uint32 resolution_;
    uint32 current_lod_ = 0;

    Math::Vector3 center_;
    Math::Vector3 bounds_min_;
    Math::Vector3 bounds_max_;

    Array<float32> heights_;
    Array<SharedPtr<Renderer::Mesh>> lod_meshes_;
};

/**
 * Terrain configuration
 */
struct TerrainConfig {
    // Size
    float32 terrain_width = 1024.0f;
    float32 terrain_length = 1024.0f;
    float32 height_scale = 100.0f;

    // Chunks
    uint32 chunk_resolution = 64;
    uint32 num_chunks_x = 16;
    uint32 num_chunks_z = 16;

    // LOD
    uint32 num_lod_levels = 4;
    float32 lod_distance_multiplier = 2.0f;
    Array<float32> lod_distances = {50, 100, 200, 400};

    // Texturing
    uint32 splat_map_resolution = 512;
    uint32 max_layers = 8;
};

/**
 * Terrain heightmap operations
 */
class TerrainHeightmap {
public:
    TerrainHeightmap();
    ~TerrainHeightmap();

    bool LoadFromFile(const String& path);
    bool LoadFromTexture(SharedPtr<Renderer::Texture> texture);
    bool Create(uint32 width, uint32 height, float32 default_height = 0.0f);

    uint32 GetWidth() const { return width_; }
    uint32 GetHeight() const { return height_; }
    const float32* GetData() const { return data_.data(); }

    float32 GetHeight(uint32 x, uint32 y) const;
    float32 GetHeightBilinear(float32 u, float32 v) const;
    Math::Vector3 GetNormal(uint32 x, uint32 y) const;

    void SetHeight(uint32 x, uint32 y, float32 height);

    // Modification tools
    void Flatten(uint32 x, uint32 y, uint32 radius, float32 target_height, float32 strength);
    void Raise(uint32 x, uint32 y, uint32 radius, float32 amount, float32 falloff);
    void Lower(uint32 x, uint32 y, uint32 radius, float32 amount, float32 falloff);
    void Smooth(uint32 x, uint32 y, uint32 radius, float32 strength);
    void Noise(uint32 x, uint32 y, uint32 radius, float32 scale, float32 strength);

    // Procedural generation
    void GeneratePerlinNoise(float32 frequency, int32 octaves, float32 persistence);
    void GenerateDiamondSquare(float32 roughness);
    void ApplyHydraulicErosion(int32 iterations);

private:
    uint32 width_ = 0;
    uint32 height_ = 0;
    Array<float32> data_;
};

/**
 * Terrain splat map (texture blending weights)
 */
class TerrainSplatMap {
public:
    TerrainSplatMap();
    ~TerrainSplatMap();

    bool Create(uint32 width, uint32 height, uint32 num_layers);
    bool LoadFromFile(const String& path);

    uint32 GetWidth() const { return width_; }
    uint32 GetHeight() const { return height_; }
    uint32 GetNumLayers() const { return num_layers_; }

    // Get weight for specific layer at position
    float32 GetWeight(uint32 x, uint32 y, uint32 layer) const;
    void SetWeight(uint32 x, uint32 y, uint32 layer, float32 weight);

    // Painting
    void Paint(uint32 x, uint32 y, uint32 radius, uint32 layer, float32 strength, float32 falloff);

    // Generate textures for GPU
    SharedPtr<Renderer::Texture> GenerateSplatTexture(uint32 layer_start) const;

private:
    void NormalizeWeights(uint32 x, uint32 y);

    uint32 width_ = 0;
    uint32 height_ = 0;
    uint32 num_layers_ = 0;
    Array<Array<float32>> layer_weights_;
};

/**
 * Terrain foliage instance
 */
struct FoliageInstance {
    Math::Vector3 position;
    Math::Quaternion rotation;
    Math::Vector3 scale;
    Math::Vector4 color;
};

/**
 * Terrain foliage type
 */
struct FoliageType {
    String name;
    SharedPtr<Renderer::Mesh> mesh;
    SharedPtr<Renderer::Material> material;

    float32 min_scale = 0.8f;
    float32 max_scale = 1.2f;
    float32 density = 1.0f;
    float32 min_slope = 0.0f;
    float32 max_slope = 45.0f;
    float32 min_height = 0.0f;
    float32 max_height = 1000.0f;

    bool align_to_normal = true;
    bool random_rotation = true;
    bool cast_shadows = true;
};

/**
 * Terrain foliage system
 */
class TerrainFoliage {
public:
    TerrainFoliage();
    ~TerrainFoliage();

    void AddFoliageType(const FoliageType& type);
    void RemoveFoliageType(const String& name);

    void Generate(const TerrainHeightmap& heightmap, const TerrainSplatMap& splatmap,
                  const TerrainConfig& config);
    void Clear();

    void Render(VkCommandBuffer cmd, const Math::Matrix4& view_projection);
    void UpdateCulling(const Math::Vector3& camera_pos, float32 cull_distance);

    uint32 GetInstanceCount() const;

private:
    HashMap<String, FoliageType> foliage_types_;
    HashMap<String, Array<FoliageInstance>> instances_;

    // GPU buffers for instanced rendering
    // VkBuffer instance_buffer_;
};

/**
 * Main Terrain class
 */
class Terrain {
public:
    Terrain();
    ~Terrain();

    bool Initialize(const TerrainConfig& config);
    void Shutdown();

    void Update(const Math::Vector3& camera_pos, float32 delta_time);
    void Render(VkCommandBuffer cmd, const Math::Matrix4& view_projection);

    // Heightmap
    void SetHeightmap(const TerrainHeightmap& heightmap);
    TerrainHeightmap& GetHeightmap() { return heightmap_; }
    const TerrainHeightmap& GetHeightmap() const { return heightmap_; }

    // Layers
    void AddLayer(const TerrainLayer& layer);
    void RemoveLayer(uint32 index);
    TerrainLayer& GetLayer(uint32 index) { return layers_[index]; }
    uint32 GetLayerCount() const { return static_cast<uint32>(layers_.size()); }

    // Splat map
    TerrainSplatMap& GetSplatMap() { return splatmap_; }

    // Foliage
    TerrainFoliage& GetFoliage() { return foliage_; }

    // Height queries
    float32 GetHeightAt(float32 x, float32 z) const;
    Math::Vector3 GetNormalAt(float32 x, float32 z) const;
    bool Raycast(const Math::Vector3& origin, const Math::Vector3& direction,
                 float32& out_distance, Math::Vector3& out_point) const;

    // Configuration
    const TerrainConfig& GetConfig() const { return config_; }

    // Bounds
    Math::Vector3 GetMin() const;
    Math::Vector3 GetMax() const;

private:
    void GenerateChunks();
    void UpdateChunkLODs(const Math::Vector3& camera_pos);

    TerrainConfig config_;
    TerrainHeightmap heightmap_;
    TerrainSplatMap splatmap_;
    TerrainFoliage foliage_;
    Array<TerrainLayer> layers_;

    Array<UniquePtr<TerrainChunk>> chunks_;

    // Material for terrain rendering
    SharedPtr<Renderer::Material> terrain_material_;
};

/**
 * Terrain Manager
 */
class TerrainManager {
public:
    static TerrainManager& Instance();

    bool Initialize();
    void Shutdown();
    void Update(float32 delta_time);

    Terrain* CreateTerrain(const String& name, const TerrainConfig& config);
    void DestroyTerrain(const String& name);
    Terrain* GetTerrain(const String& name);

    void SetActiveTerrain(Terrain* terrain);
    Terrain* GetActiveTerrain() const { return active_terrain_; }

private:
    TerrainManager() = default;

    HashMap<String, UniquePtr<Terrain>> terrains_;
    Terrain* active_terrain_ = nullptr;
};

} // namespace DRX::Terrain

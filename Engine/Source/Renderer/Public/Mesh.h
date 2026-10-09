#pragma once

/**
 * DRX Engine - Mesh System
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector2.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Vector4.h"
#include "Core/Public/Math/Transform.h"
#include "HAL/Public/GraphicsDevice.h"

#include <vulkan/vulkan.h>

struct aiMesh;
struct aiNode;
struct aiScene;

namespace DRX::Renderer {

// ============================================================================
// Vertex Types
// ============================================================================

struct StaticVertex {
    Math::Vector3 position;
    Math::Vector3 normal;
    Math::Vector3 tangent;
    Math::Vector2 tex_coord;
};

struct SkinnedVertex {
    Math::Vector3 position;
    Math::Vector3 normal;
    Math::Vector3 tangent;
    Math::Vector2 tex_coord;
    Math::Vector4 bone_weights;
    uint32 bone_indices[4] = {0, 0, 0, 0};
};

// ============================================================================
// Bounding Volumes
// ============================================================================

struct AABB {
    Math::Vector3 min;
    Math::Vector3 max;

    Math::Vector3 GetCenter() const { return (min + max) * 0.5f; }
    Math::Vector3 GetExtents() const { return (max - min) * 0.5f; }
    Math::Vector3 GetSize() const { return max - min; }

    void Expand(const Math::Vector3& point) {
        min = Math::Vector3::Min(min, point);
        max = Math::Vector3::Max(max, point);
    }

    void Merge(const AABB& other) {
        min = Math::Vector3::Min(min, other.min);
        max = Math::Vector3::Max(max, other.max);
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
};

struct BoundingSphere {
    Math::Vector3 center;
    float32 radius = 0.0f;

    bool Contains(const Math::Vector3& point) const {
        return Math::Vector3::DistanceSquared(center, point) <= radius * radius;
    }

    bool Intersects(const BoundingSphere& other) const {
        const float32 r = radius + other.radius;
        return Math::Vector3::DistanceSquared(center, other.center) <= r * r;
    }
};

// ============================================================================
// Sub Mesh
// ============================================================================

struct SubMesh {
    uint32 index_offset = 0;
    uint32 index_count = 0;
    uint32 vertex_offset = 0;
    uint32 material_index = 0;
    AABB bounds;
};

// ============================================================================
// Mesh Class
// ============================================================================

class DRX_API Mesh : public NonCopyable {
public:
    Mesh();
    ~Mesh();

    bool Create(HAL::GraphicsDevice* device, const Array<StaticVertex>& vertices,
                const Array<uint32>& indices, const String& name);
    bool CreateFromFile(HAL::GraphicsDevice* device, const String& path);
    void Destroy();

    // Getters
    const String& GetName() const { return name_; }
    uint32 GetVertexCount() const { return static_cast<uint32>(vertices_.size()); }
    uint32 GetIndexCount() const { return static_cast<uint32>(indices_.size()); }
    const Array<StaticVertex>& GetVertices() const { return vertices_; }
    const Array<uint32>& GetIndices() const { return indices_; }
    const AABB& GetBounds() const { return bounds_; }
    uint32 GetMaterialIndex() const { return material_index_; }
    bool IsValid() const { return vertex_buffer_ != VK_NULL_HANDLE && index_buffer_ != VK_NULL_HANDLE; }

    VkBuffer GetVertexBuffer() const { return vertex_buffer_; }
    VkBuffer GetIndexBuffer() const { return index_buffer_; }

    // Drawing
    void Bind(VkCommandBuffer cmd) const;
    void Draw(VkCommandBuffer cmd) const;
    void DrawInstanced(VkCommandBuffer cmd, uint32 instance_count) const;

    // Vertex layout of StaticVertex
    static Array<VkVertexInputBindingDescription> GetBindingDescriptions();
    static Array<VkVertexInputAttributeDescription> GetAttributeDescriptions();

    // Primitives
    static SharedPtr<Mesh> CreateCube(HAL::GraphicsDevice* device, float32 size = 1.0f);
    static SharedPtr<Mesh> CreateSphere(HAL::GraphicsDevice* device, float32 radius = 0.5f,
                                        uint32 segments = 32, uint32 rings = 16);
    static SharedPtr<Mesh> CreatePlane(HAL::GraphicsDevice* device, float32 width = 1.0f, float32 height = 1.0f,
                                       uint32 subdivisions_x = 1, uint32 subdivisions_z = 1);
    static SharedPtr<Mesh> CreateCylinder(HAL::GraphicsDevice* device, float32 radius = 0.5f,
                                          float32 height = 1.0f, uint32 segments = 32);

private:
    bool UploadBuffers();
    bool CreateDeviceBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                            VkBuffer& buffer, VkDeviceMemory& memory);
    void ProcessAssimpMesh(const aiMesh* mesh);
    void CalculateBounds();

    HAL::GraphicsDevice* device_ = nullptr;
    String name_;

    Array<StaticVertex> vertices_;
    Array<uint32> indices_;
    uint32 material_index_ = 0;

    VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_buffer_memory_ = VK_NULL_HANDLE;
    VkBuffer index_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory index_buffer_memory_ = VK_NULL_HANDLE;

    AABB bounds_;
};

// ============================================================================
// Model (Collection of meshes)
// ============================================================================

class DRX_API Model : public NonCopyable {
public:
    Model();
    ~Model();

    bool LoadFromFile(HAL::GraphicsDevice* device, const String& path);
    void Destroy();

    void Draw(VkCommandBuffer cmd) const;

    const String& GetName() const { return name_; }
    const Array<SharedPtr<Mesh>>& GetMeshes() const { return meshes_; }
    const AABB& GetBounds() const { return bounds_; }

private:
    void ProcessNode(const aiNode* node, const aiScene* scene);

    HAL::GraphicsDevice* device_ = nullptr;
    String name_;
    Array<SharedPtr<Mesh>> meshes_;
    AABB bounds_;
};

// ============================================================================
// Mesh Manager
// ============================================================================

class DRX_API MeshManager {
public:
    static MeshManager& Instance();

    void Initialize(HAL::GraphicsDevice* device);
    void Shutdown();

    SharedPtr<Mesh> Load(const String& path);
    SharedPtr<Mesh> Get(const String& name);

    // Primitives
    SharedPtr<Mesh> GetCube() const { return cube_; }
    SharedPtr<Mesh> GetSphere() const { return sphere_; }
    SharedPtr<Mesh> GetPlane() const { return plane_; }
    SharedPtr<Mesh> GetCylinder() const { return cylinder_; }

    void Unload(const String& name);
    void UnloadAll();

private:
    MeshManager() = default;

    void CreatePrimitives();

    HAL::GraphicsDevice* device_ = nullptr;
    HashMap<String, SharedPtr<Mesh>> meshes_;

    SharedPtr<Mesh> cube_;
    SharedPtr<Mesh> sphere_;
    SharedPtr<Mesh> plane_;
    SharedPtr<Mesh> cylinder_;
};

} // namespace DRX::Renderer

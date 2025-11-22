#pragma once

/**
 * DRX Engine - Mesh System
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector2.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Vector4.h"
#include "HAL/Public/GraphicsDevice.h"

#include <vulkan/vulkan.h>

namespace DRX::Renderer {

// ============================================================================
// Vertex Types
// ============================================================================

struct StaticVertex {
    Math::Vector3 position;
    Math::Vector3 normal;
    Math::Vector3 tangent;
    Math::Vector2 tex_coord;

    static VkVertexInputBindingDescription GetBindingDescription();
    static Array<VkVertexInputAttributeDescription> GetAttributeDescriptions();
};

struct SkinnedVertex {
    Math::Vector3 position;
    Math::Vector3 normal;
    Math::Vector3 tangent;
    Math::Vector2 tex_coord;
    Math::Vector4 bone_weights;
    uint32 bone_indices[4];

    static VkVertexInputBindingDescription GetBindingDescription();
    static Array<VkVertexInputAttributeDescription> GetAttributeDescriptions();
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

    void Expand(const Math::Vector3& point);
    void Merge(const AABB& other);
    bool Contains(const Math::Vector3& point) const;
    bool Intersects(const AABB& other) const;
};

struct BoundingSphere {
    Math::Vector3 center;
    float32 radius;

    bool Contains(const Math::Vector3& point) const;
    bool Intersects(const BoundingSphere& other) const;
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

    bool Create(HAL::GraphicsDevice* device, const String& name);
    bool CreateFromFile(HAL::GraphicsDevice* device, const String& path);
    void Destroy();

    // Data upload
    void SetVertices(const Array<StaticVertex>& vertices);
    void SetVertices(const Array<SkinnedVertex>& vertices);
    void SetIndices(const Array<uint32>& indices);
    void AddSubMesh(const SubMesh& submesh);
    void Upload();

    // Getters
    const String& GetName() const { return name_; }
    uint32 GetVertexCount() const { return vertex_count_; }
    uint32 GetIndexCount() const { return index_count_; }
    const Array<SubMesh>& GetSubMeshes() const { return submeshes_; }
    const AABB& GetBounds() const { return bounds_; }
    bool IsSkinned() const { return is_skinned_; }

    VkBuffer GetVertexBuffer() const { return vertex_buffer_; }
    VkBuffer GetIndexBuffer() const { return index_buffer_; }

    // Drawing
    void Bind(VkCommandBuffer cmd);
    void Draw(VkCommandBuffer cmd, uint32 submesh_index = 0);
    void DrawAll(VkCommandBuffer cmd);

    // Primitives
    static SharedPtr<Mesh> CreateCube(HAL::GraphicsDevice* device, float32 size = 1.0f);
    static SharedPtr<Mesh> CreateSphere(HAL::GraphicsDevice* device, float32 radius = 0.5f, uint32 segments = 32);
    static SharedPtr<Mesh> CreatePlane(HAL::GraphicsDevice* device, float32 width = 1.0f, float32 height = 1.0f);
    static SharedPtr<Mesh> CreateCylinder(HAL::GraphicsDevice* device, float32 radius = 0.5f, float32 height = 1.0f);
    static SharedPtr<Mesh> CreateCapsule(HAL::GraphicsDevice* device, float32 radius = 0.5f, float32 height = 2.0f);

private:
    void CreateBuffers();
    void CalculateBounds();
    void CalculateTangents();

    HAL::GraphicsDevice* device_ = nullptr;
    String name_;

    // CPU data (cleared after upload)
    Array<StaticVertex> static_vertices_;
    Array<SkinnedVertex> skinned_vertices_;
    Array<uint32> indices_;

    // GPU resources
    VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_buffer_memory_ = VK_NULL_HANDLE;
    VkBuffer index_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory index_buffer_memory_ = VK_NULL_HANDLE;

    // Metadata
    uint32 vertex_count_ = 0;
    uint32 index_count_ = 0;
    Array<SubMesh> submeshes_;
    AABB bounds_;
    bool is_skinned_ = false;
    bool uploaded_ = false;
};

// ============================================================================
// Model (Collection of meshes)
// ============================================================================

struct ModelNode {
    String name;
    Math::Transform local_transform;
    Array<uint32> mesh_indices;
    Array<uint32> children;
    int32 parent = -1;
};

class DRX_API Model : public NonCopyable {
public:
    Model();
    ~Model();

    bool LoadFromFile(HAL::GraphicsDevice* device, const String& path);
    void Destroy();

    const String& GetName() const { return name_; }
    const String& GetPath() const { return path_; }

    const Array<SharedPtr<Mesh>>& GetMeshes() const { return meshes_; }
    const Array<MaterialHandle>& GetMaterials() const { return materials_; }
    const Array<ModelNode>& GetNodes() const { return nodes_; }

    SharedPtr<Mesh> GetMesh(uint32 index) const;
    const AABB& GetBounds() const { return bounds_; }

private:
    bool LoadWithAssimp(HAL::GraphicsDevice* device, const String& path);
    void ProcessNode(void* ai_node, int32 parent_index);
    SharedPtr<Mesh> ProcessMesh(HAL::GraphicsDevice* device, void* ai_mesh);

    HAL::GraphicsDevice* device_ = nullptr;
    String name_;
    String path_;

    Array<SharedPtr<Mesh>> meshes_;
    Array<MaterialHandle> materials_;
    Array<ModelNode> nodes_;
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
    SharedPtr<Model> LoadModel(const String& path);

    // Primitives
    SharedPtr<Mesh> GetCube();
    SharedPtr<Mesh> GetSphere();
    SharedPtr<Mesh> GetPlane();

    void Unload(const String& name);
    void UnloadAll();

private:
    MeshManager() = default;

    void CreatePrimitives();

    HAL::GraphicsDevice* device_ = nullptr;
    HashMap<String, SharedPtr<Mesh>> meshes_;
    HashMap<String, SharedPtr<Model>> models_;

    SharedPtr<Mesh> cube_;
    SharedPtr<Mesh> sphere_;
    SharedPtr<Mesh> plane_;
};

} // namespace DRX::Renderer

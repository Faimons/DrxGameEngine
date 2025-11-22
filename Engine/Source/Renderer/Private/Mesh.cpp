/**
 * DRX Engine - Mesh System Implementation
 */

#include "Renderer/Public/Mesh.h"
#include "Core/Public/Log.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <cstring>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace DRX::Renderer {

// ============================================================================
// Mesh Implementation
// ============================================================================

Mesh::Mesh() = default;

Mesh::~Mesh() {
    Destroy();
}

bool Mesh::Create(HAL::GraphicsDevice* device, const Array<StaticVertex>& vertices,
                  const Array<uint32>& indices, const String& name) {
    device_ = device;
    name_ = name;
    vertices_ = vertices;
    indices_ = indices;

    CalculateBounds();

    if (!CreateVertexBuffer()) {
        DRX_LOG_ERROR("Failed to create vertex buffer for mesh: {}", name);
        return false;
    }

    if (!CreateIndexBuffer()) {
        DRX_LOG_ERROR("Failed to create index buffer for mesh: {}", name);
        return false;
    }

    DRX_LOG_INFO("Created mesh: {} ({} vertices, {} indices)", name, vertices.size(), indices.size());
    return true;
}

bool Mesh::CreateFromFile(HAL::GraphicsDevice* device, const String& path) {
    device_ = device;
    name_ = path;

    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_FlipUVs |
        aiProcess_JoinIdenticalVertices |
        aiProcess_OptimizeMeshes
    );

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        DRX_LOG_ERROR("Assimp error loading {}: {}", path, importer.GetErrorString());
        return false;
    }

    // Process first mesh only for simple Mesh class
    if (scene->mNumMeshes > 0) {
        ProcessAssimpMesh(scene->mMeshes[0], scene);
    }

    CalculateBounds();

    if (!CreateVertexBuffer()) {
        return false;
    }

    if (!CreateIndexBuffer()) {
        return false;
    }

    DRX_LOG_INFO("Loaded mesh: {} ({} vertices, {} indices)", path, vertices_.size(), indices_.size());
    return true;
}

void Mesh::Destroy() {
    if (!device_) return;

    VkDevice vk_device = device_->GetDevice();

    if (vertex_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk_device, vertex_buffer_, nullptr);
        vertex_buffer_ = VK_NULL_HANDLE;
    }

    if (vertex_buffer_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, vertex_buffer_memory_, nullptr);
        vertex_buffer_memory_ = VK_NULL_HANDLE;
    }

    if (index_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk_device, index_buffer_, nullptr);
        index_buffer_ = VK_NULL_HANDLE;
    }

    if (index_buffer_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, index_buffer_memory_, nullptr);
        index_buffer_memory_ = VK_NULL_HANDLE;
    }

    vertices_.clear();
    indices_.clear();
    device_ = nullptr;
}

void Mesh::Bind(VkCommandBuffer cmd) const {
    VkBuffer buffers[] = {vertex_buffer_};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, index_buffer_, 0, VK_INDEX_TYPE_UINT32);
}

void Mesh::Draw(VkCommandBuffer cmd) const {
    vkCmdDrawIndexed(cmd, static_cast<uint32>(indices_.size()), 1, 0, 0, 0);
}

void Mesh::DrawInstanced(VkCommandBuffer cmd, uint32 instance_count) const {
    vkCmdDrawIndexed(cmd, static_cast<uint32>(indices_.size()), instance_count, 0, 0, 0);
}

SharedPtr<Mesh> Mesh::CreateCube(HAL::GraphicsDevice* device, float32 size) {
    float32 h = size * 0.5f;

    Array<StaticVertex> vertices = {
        // Front face
        {{-h, -h,  h}, {0, 0, 1}, {1, 0, 0}, {0, 0}},
        {{ h, -h,  h}, {0, 0, 1}, {1, 0, 0}, {1, 0}},
        {{ h,  h,  h}, {0, 0, 1}, {1, 0, 0}, {1, 1}},
        {{-h,  h,  h}, {0, 0, 1}, {1, 0, 0}, {0, 1}},
        // Back face
        {{ h, -h, -h}, {0, 0, -1}, {-1, 0, 0}, {0, 0}},
        {{-h, -h, -h}, {0, 0, -1}, {-1, 0, 0}, {1, 0}},
        {{-h,  h, -h}, {0, 0, -1}, {-1, 0, 0}, {1, 1}},
        {{ h,  h, -h}, {0, 0, -1}, {-1, 0, 0}, {0, 1}},
        // Top face
        {{-h,  h,  h}, {0, 1, 0}, {1, 0, 0}, {0, 0}},
        {{ h,  h,  h}, {0, 1, 0}, {1, 0, 0}, {1, 0}},
        {{ h,  h, -h}, {0, 1, 0}, {1, 0, 0}, {1, 1}},
        {{-h,  h, -h}, {0, 1, 0}, {1, 0, 0}, {0, 1}},
        // Bottom face
        {{-h, -h, -h}, {0, -1, 0}, {1, 0, 0}, {0, 0}},
        {{ h, -h, -h}, {0, -1, 0}, {1, 0, 0}, {1, 0}},
        {{ h, -h,  h}, {0, -1, 0}, {1, 0, 0}, {1, 1}},
        {{-h, -h,  h}, {0, -1, 0}, {1, 0, 0}, {0, 1}},
        // Right face
        {{ h, -h,  h}, {1, 0, 0}, {0, 0, -1}, {0, 0}},
        {{ h, -h, -h}, {1, 0, 0}, {0, 0, -1}, {1, 0}},
        {{ h,  h, -h}, {1, 0, 0}, {0, 0, -1}, {1, 1}},
        {{ h,  h,  h}, {1, 0, 0}, {0, 0, -1}, {0, 1}},
        // Left face
        {{-h, -h, -h}, {-1, 0, 0}, {0, 0, 1}, {0, 0}},
        {{-h, -h,  h}, {-1, 0, 0}, {0, 0, 1}, {1, 0}},
        {{-h,  h,  h}, {-1, 0, 0}, {0, 0, 1}, {1, 1}},
        {{-h,  h, -h}, {-1, 0, 0}, {0, 0, 1}, {0, 1}},
    };

    Array<uint32> indices = {
        0,  1,  2,  2,  3,  0,   // Front
        4,  5,  6,  6,  7,  4,   // Back
        8,  9,  10, 10, 11, 8,   // Top
        12, 13, 14, 14, 15, 12,  // Bottom
        16, 17, 18, 18, 19, 16,  // Right
        20, 21, 22, 22, 23, 20   // Left
    };

    auto mesh = MakeShared<Mesh>();
    mesh->Create(device, vertices, indices, "Cube");
    return mesh;
}

SharedPtr<Mesh> Mesh::CreateSphere(HAL::GraphicsDevice* device, float32 radius,
                                    uint32 segments, uint32 rings) {
    Array<StaticVertex> vertices;
    Array<uint32> indices;

    for (uint32 y = 0; y <= rings; y++) {
        for (uint32 x = 0; x <= segments; x++) {
            float32 x_segment = static_cast<float32>(x) / static_cast<float32>(segments);
            float32 y_segment = static_cast<float32>(y) / static_cast<float32>(rings);
            float32 x_pos = std::cos(x_segment * 2.0f * static_cast<float32>(M_PI)) * std::sin(y_segment * static_cast<float32>(M_PI));
            float32 y_pos = std::cos(y_segment * static_cast<float32>(M_PI));
            float32 z_pos = std::sin(x_segment * 2.0f * static_cast<float32>(M_PI)) * std::sin(y_segment * static_cast<float32>(M_PI));

            StaticVertex vertex;
            vertex.position = {x_pos * radius, y_pos * radius, z_pos * radius};
            vertex.normal = {x_pos, y_pos, z_pos};
            vertex.tex_coord = {x_segment, y_segment};

            // Calculate tangent
            float32 theta = x_segment * 2.0f * static_cast<float32>(M_PI);
            vertex.tangent = {-std::sin(theta), 0, std::cos(theta)};

            vertices.push_back(vertex);
        }
    }

    for (uint32 y = 0; y < rings; y++) {
        for (uint32 x = 0; x < segments; x++) {
            uint32 current = y * (segments + 1) + x;
            uint32 next = current + segments + 1;

            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);

            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }

    auto mesh = MakeShared<Mesh>();
    mesh->Create(device, vertices, indices, "Sphere");
    return mesh;
}

SharedPtr<Mesh> Mesh::CreatePlane(HAL::GraphicsDevice* device, float32 width, float32 height,
                                   uint32 subdivisions_x, uint32 subdivisions_y) {
    Array<StaticVertex> vertices;
    Array<uint32> indices;

    float32 half_width = width * 0.5f;
    float32 half_height = height * 0.5f;

    for (uint32 y = 0; y <= subdivisions_y; y++) {
        for (uint32 x = 0; x <= subdivisions_x; x++) {
            float32 u = static_cast<float32>(x) / static_cast<float32>(subdivisions_x);
            float32 v = static_cast<float32>(y) / static_cast<float32>(subdivisions_y);

            StaticVertex vertex;
            vertex.position = {
                u * width - half_width,
                0.0f,
                v * height - half_height
            };
            vertex.normal = {0, 1, 0};
            vertex.tangent = {1, 0, 0};
            vertex.tex_coord = {u, v};

            vertices.push_back(vertex);
        }
    }

    for (uint32 y = 0; y < subdivisions_y; y++) {
        for (uint32 x = 0; x < subdivisions_x; x++) {
            uint32 current = y * (subdivisions_x + 1) + x;
            uint32 next = current + subdivisions_x + 1;

            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);

            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }

    auto mesh = MakeShared<Mesh>();
    mesh->Create(device, vertices, indices, "Plane");
    return mesh;
}

SharedPtr<Mesh> Mesh::CreateCylinder(HAL::GraphicsDevice* device, float32 radius,
                                      float32 height, uint32 segments) {
    Array<StaticVertex> vertices;
    Array<uint32> indices;

    float32 half_height = height * 0.5f;

    // Side vertices
    for (uint32 i = 0; i <= segments; i++) {
        float32 angle = static_cast<float32>(i) / static_cast<float32>(segments) * 2.0f * static_cast<float32>(M_PI);
        float32 x = std::cos(angle);
        float32 z = std::sin(angle);

        // Bottom vertex
        StaticVertex bottom;
        bottom.position = {x * radius, -half_height, z * radius};
        bottom.normal = {x, 0, z};
        bottom.tangent = {-z, 0, x};
        bottom.tex_coord = {static_cast<float32>(i) / segments, 0};
        vertices.push_back(bottom);

        // Top vertex
        StaticVertex top;
        top.position = {x * radius, half_height, z * radius};
        top.normal = {x, 0, z};
        top.tangent = {-z, 0, x};
        top.tex_coord = {static_cast<float32>(i) / segments, 1};
        vertices.push_back(top);
    }

    // Side indices
    for (uint32 i = 0; i < segments; i++) {
        uint32 base = i * 2;
        indices.push_back(base);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    // Cap centers
    uint32 bottom_center = static_cast<uint32>(vertices.size());
    StaticVertex bc;
    bc.position = {0, -half_height, 0};
    bc.normal = {0, -1, 0};
    bc.tangent = {1, 0, 0};
    bc.tex_coord = {0.5f, 0.5f};
    vertices.push_back(bc);

    uint32 top_center = static_cast<uint32>(vertices.size());
    StaticVertex tc;
    tc.position = {0, half_height, 0};
    tc.normal = {0, 1, 0};
    tc.tangent = {1, 0, 0};
    tc.tex_coord = {0.5f, 0.5f};
    vertices.push_back(tc);

    // Cap vertices
    uint32 cap_start = static_cast<uint32>(vertices.size());
    for (uint32 i = 0; i <= segments; i++) {
        float32 angle = static_cast<float32>(i) / static_cast<float32>(segments) * 2.0f * static_cast<float32>(M_PI);
        float32 x = std::cos(angle);
        float32 z = std::sin(angle);

        StaticVertex bv;
        bv.position = {x * radius, -half_height, z * radius};
        bv.normal = {0, -1, 0};
        bv.tangent = {1, 0, 0};
        bv.tex_coord = {x * 0.5f + 0.5f, z * 0.5f + 0.5f};
        vertices.push_back(bv);

        StaticVertex tv;
        tv.position = {x * radius, half_height, z * radius};
        tv.normal = {0, 1, 0};
        tv.tangent = {1, 0, 0};
        tv.tex_coord = {x * 0.5f + 0.5f, z * 0.5f + 0.5f};
        vertices.push_back(tv);
    }

    // Cap indices
    for (uint32 i = 0; i < segments; i++) {
        uint32 base = cap_start + i * 2;
        // Bottom cap
        indices.push_back(bottom_center);
        indices.push_back(base + 2);
        indices.push_back(base);
        // Top cap
        indices.push_back(top_center);
        indices.push_back(base + 1);
        indices.push_back(base + 3);
    }

    auto mesh = MakeShared<Mesh>();
    mesh->Create(device, vertices, indices, "Cylinder");
    return mesh;
}

Array<VkVertexInputBindingDescription> Mesh::GetBindingDescriptions() {
    return {{0, sizeof(StaticVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
}

Array<VkVertexInputAttributeDescription> Mesh::GetAttributeDescriptions() {
    return {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticVertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticVertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticVertex, tangent)},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(StaticVertex, tex_coord)}
    };
}

void Mesh::ProcessAssimpMesh(aiMesh* mesh, const aiScene* scene) {
    vertices_.reserve(mesh->mNumVertices);

    for (uint32 i = 0; i < mesh->mNumVertices; i++) {
        StaticVertex vertex;

        vertex.position = {
            mesh->mVertices[i].x,
            mesh->mVertices[i].y,
            mesh->mVertices[i].z
        };

        if (mesh->HasNormals()) {
            vertex.normal = {
                mesh->mNormals[i].x,
                mesh->mNormals[i].y,
                mesh->mNormals[i].z
            };
        }

        if (mesh->HasTangentsAndBitangents()) {
            vertex.tangent = {
                mesh->mTangents[i].x,
                mesh->mTangents[i].y,
                mesh->mTangents[i].z
            };
        }

        if (mesh->mTextureCoords[0]) {
            vertex.tex_coord = {
                mesh->mTextureCoords[0][i].x,
                mesh->mTextureCoords[0][i].y
            };
        }

        vertices_.push_back(vertex);
    }

    for (uint32 i = 0; i < mesh->mNumFaces; i++) {
        aiFace& face = mesh->mFaces[i];
        for (uint32 j = 0; j < face.mNumIndices; j++) {
            indices_.push_back(face.mIndices[j]);
        }
    }

    material_index_ = mesh->mMaterialIndex;
}

void Mesh::CalculateBounds() {
    if (vertices_.empty()) return;

    bounds_.min = vertices_[0].position;
    bounds_.max = vertices_[0].position;

    for (const auto& v : vertices_) {
        bounds_.min.x = std::min(bounds_.min.x, v.position.x);
        bounds_.min.y = std::min(bounds_.min.y, v.position.y);
        bounds_.min.z = std::min(bounds_.min.z, v.position.z);
        bounds_.max.x = std::max(bounds_.max.x, v.position.x);
        bounds_.max.y = std::max(bounds_.max.y, v.position.y);
        bounds_.max.z = std::max(bounds_.max.z, v.position.z);
    }
}

bool Mesh::CreateVertexBuffer() {
    VkDevice vk_device = device_->GetDevice();
    VkDeviceSize buffer_size = sizeof(StaticVertex) * vertices_.size();

    // Create staging buffer
    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;

    VkBufferCreateInfo staging_info{};
    staging_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_info.size = buffer_size;
    staging_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(vk_device, &staging_info, nullptr, &staging_buffer);

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(vk_device, staging_buffer, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = device_->FindMemoryType(
        mem_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    vkAllocateMemory(vk_device, &alloc_info, nullptr, &staging_memory);
    vkBindBufferMemory(vk_device, staging_buffer, staging_memory, 0);

    void* data;
    vkMapMemory(vk_device, staging_memory, 0, buffer_size, 0, &data);
    std::memcpy(data, vertices_.data(), buffer_size);
    vkUnmapMemory(vk_device, staging_memory);

    // Create vertex buffer
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(vk_device, &buffer_info, nullptr, &vertex_buffer_);

    vkGetBufferMemoryRequirements(vk_device, vertex_buffer_, &mem_requirements);

    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = device_->FindMemoryType(
        mem_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    vkAllocateMemory(vk_device, &alloc_info, nullptr, &vertex_buffer_memory_);
    vkBindBufferMemory(vk_device, vertex_buffer_, vertex_buffer_memory_, 0);

    // Copy staging to vertex buffer
    VkCommandBuffer cmd = device_->BeginSingleTimeCommands();

    VkBufferCopy copy_region{};
    copy_region.size = buffer_size;
    vkCmdCopyBuffer(cmd, staging_buffer, vertex_buffer_, 1, &copy_region);

    device_->EndSingleTimeCommands(cmd);

    vkDestroyBuffer(vk_device, staging_buffer, nullptr);
    vkFreeMemory(vk_device, staging_memory, nullptr);

    return true;
}

bool Mesh::CreateIndexBuffer() {
    VkDevice vk_device = device_->GetDevice();
    VkDeviceSize buffer_size = sizeof(uint32) * indices_.size();

    // Create staging buffer
    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;

    VkBufferCreateInfo staging_info{};
    staging_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_info.size = buffer_size;
    staging_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(vk_device, &staging_info, nullptr, &staging_buffer);

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(vk_device, staging_buffer, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = device_->FindMemoryType(
        mem_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    vkAllocateMemory(vk_device, &alloc_info, nullptr, &staging_memory);
    vkBindBufferMemory(vk_device, staging_buffer, staging_memory, 0);

    void* data;
    vkMapMemory(vk_device, staging_memory, 0, buffer_size, 0, &data);
    std::memcpy(data, indices_.data(), buffer_size);
    vkUnmapMemory(vk_device, staging_memory);

    // Create index buffer
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(vk_device, &buffer_info, nullptr, &index_buffer_);

    vkGetBufferMemoryRequirements(vk_device, index_buffer_, &mem_requirements);

    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = device_->FindMemoryType(
        mem_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    vkAllocateMemory(vk_device, &alloc_info, nullptr, &index_buffer_memory_);
    vkBindBufferMemory(vk_device, index_buffer_, index_buffer_memory_, 0);

    // Copy staging to index buffer
    VkCommandBuffer cmd = device_->BeginSingleTimeCommands();

    VkBufferCopy copy_region{};
    copy_region.size = buffer_size;
    vkCmdCopyBuffer(cmd, staging_buffer, index_buffer_, 1, &copy_region);

    device_->EndSingleTimeCommands(cmd);

    vkDestroyBuffer(vk_device, staging_buffer, nullptr);
    vkFreeMemory(vk_device, staging_memory, nullptr);

    return true;
}

// ============================================================================
// Model Implementation
// ============================================================================

Model::Model() = default;

Model::~Model() {
    Destroy();
}

bool Model::LoadFromFile(HAL::GraphicsDevice* device, const String& path) {
    device_ = device;
    name_ = path;

    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_FlipUVs |
        aiProcess_JoinIdenticalVertices |
        aiProcess_OptimizeMeshes
    );

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        DRX_LOG_ERROR("Assimp error loading model {}: {}", path, importer.GetErrorString());
        return false;
    }

    ProcessNode(scene->mRootNode, scene);

    DRX_LOG_INFO("Loaded model: {} ({} meshes)", path, meshes_.size());
    return true;
}

void Model::Destroy() {
    meshes_.clear();
    materials_.clear();
    device_ = nullptr;
}

void Model::Draw(VkCommandBuffer cmd) const {
    for (size_t i = 0; i < meshes_.size(); i++) {
        meshes_[i]->Bind(cmd);
        meshes_[i]->Draw(cmd);
    }
}

void Model::ProcessNode(aiNode* node, const aiScene* scene) {
    for (uint32 i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        auto drx_mesh = MakeShared<Mesh>();

        Array<StaticVertex> vertices;
        Array<uint32> indices;

        vertices.reserve(mesh->mNumVertices);
        for (uint32 j = 0; j < mesh->mNumVertices; j++) {
            StaticVertex vertex;
            vertex.position = {mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z};

            if (mesh->HasNormals()) {
                vertex.normal = {mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z};
            }

            if (mesh->HasTangentsAndBitangents()) {
                vertex.tangent = {mesh->mTangents[j].x, mesh->mTangents[j].y, mesh->mTangents[j].z};
            }

            if (mesh->mTextureCoords[0]) {
                vertex.tex_coord = {mesh->mTextureCoords[0][j].x, mesh->mTextureCoords[0][j].y};
            }

            vertices.push_back(vertex);
        }

        for (uint32 j = 0; j < mesh->mNumFaces; j++) {
            aiFace& face = mesh->mFaces[j];
            for (uint32 k = 0; k < face.mNumIndices; k++) {
                indices.push_back(face.mIndices[k]);
            }
        }

        drx_mesh->Create(device_, vertices, indices, mesh->mName.C_Str());
        meshes_.push_back(drx_mesh);
    }

    for (uint32 i = 0; i < node->mNumChildren; i++) {
        ProcessNode(node->mChildren[i], scene);
    }
}

// ============================================================================
// Mesh Manager Implementation
// ============================================================================

MeshManager& MeshManager::Instance() {
    static MeshManager instance;
    return instance;
}

void MeshManager::Initialize(HAL::GraphicsDevice* device) {
    device_ = device;
    CreatePrimitives();
    DRX_LOG_INFO("MeshManager initialized");
}

void MeshManager::Shutdown() {
    meshes_.clear();
    cube_.reset();
    sphere_.reset();
    plane_.reset();
    cylinder_.reset();
    device_ = nullptr;
    DRX_LOG_INFO("MeshManager shutdown");
}

SharedPtr<Mesh> MeshManager::Load(const String& path) {
    auto it = meshes_.find(path);
    if (it != meshes_.end()) {
        return it->second;
    }

    auto mesh = MakeShared<Mesh>();
    if (!mesh->CreateFromFile(device_, path)) {
        return nullptr;
    }

    meshes_[path] = mesh;
    return mesh;
}

SharedPtr<Mesh> MeshManager::Get(const String& name) {
    auto it = meshes_.find(name);
    return it != meshes_.end() ? it->second : nullptr;
}

void MeshManager::Unload(const String& name) {
    meshes_.erase(name);
}

void MeshManager::UnloadAll() {
    meshes_.clear();
}

void MeshManager::CreatePrimitives() {
    cube_ = Mesh::CreateCube(device_, 1.0f);
    sphere_ = Mesh::CreateSphere(device_, 0.5f, 32, 16);
    plane_ = Mesh::CreatePlane(device_, 10.0f, 10.0f, 10, 10);
    cylinder_ = Mesh::CreateCylinder(device_, 0.5f, 1.0f, 32);

    meshes_["__cube"] = cube_;
    meshes_["__sphere"] = sphere_;
    meshes_["__plane"] = plane_;
    meshes_["__cylinder"] = cylinder_;
}

} // namespace DRX::Renderer

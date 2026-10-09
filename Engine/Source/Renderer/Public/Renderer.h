#pragma once

/**
 * DRX Engine - Renderer Interface
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Platform.h"
#include "Core/Public/Math/Matrix4.h"
#include "Core/Public/Math/Vector2.h"
#include "Core/Public/Math/Vector3.h"
#include "HAL/Public/GraphicsDevice.h"

#include <vulkan/vulkan.h>

namespace DRX::Renderer {

// ============================================================================
// Vertex Structure
// ============================================================================

struct Vertex {
    Math::Vector3 position;
    Math::Vector3 normal;
    Math::Vector3 color;
    Math::Vector2 tex_coord;

    static VkVertexInputBindingDescription GetBindingDescription() {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static Array<VkVertexInputAttributeDescription> GetAttributeDescriptions() {
        Array<VkVertexInputAttributeDescription> attributes(4);

        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(Vertex, position);

        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(Vertex, normal);

        attributes[2].binding = 0;
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[2].offset = offsetof(Vertex, color);

        attributes[3].binding = 0;
        attributes[3].location = 3;
        attributes[3].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[3].offset = offsetof(Vertex, tex_coord);

        return attributes;
    }
};

// ============================================================================
// Uniform Buffer Objects
// ============================================================================

struct UniformBufferObject {
    alignas(16) Math::Matrix4 model;
    alignas(16) Math::Matrix4 view;
    alignas(16) Math::Matrix4 projection;
    alignas(16) Math::Vector3 camera_pos;
    alignas(4) float32 time;
};

// ============================================================================
// Forward Renderer
// ============================================================================

class DRX_API ForwardRenderer {
public:
    ForwardRenderer();
    ~ForwardRenderer();

    ForwardRenderer(const ForwardRenderer&) = delete;
    ForwardRenderer& operator=(const ForwardRenderer&) = delete;

    bool Initialize(HAL::GraphicsDevice* device);
    void Shutdown();

    // Records into the device's current command buffer between BeginFrame/EndFrame
    void BeginFrame();
    void EndFrame();

    void SetCamera(const Math::Matrix4& view, const Math::Matrix4& projection);
    void SetCameraPosition(const Math::Vector3& pos);

    void DrawTriangle();

    // Call after the swap chain was recreated
    void OnResize(uint32 width, uint32 height);

private:
    bool CreateRenderPass();
    bool CreateFramebuffers();
    bool CreatePipeline();
    bool CreateVertexBuffer();
    bool CreateUniformBuffers();
    bool CreateDescriptorPool();
    bool CreateDescriptorSets();
    bool CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory);

    void CleanupFramebuffers();
    void UpdateUniformBuffer(uint32 frame_index);

    HAL::GraphicsDevice* device_ = nullptr;
    bool frame_active_ = false;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    Array<VkFramebuffer> framebuffers_;
    uint64 swap_chain_generation_ = 0;

    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_buffer_memory_ = VK_NULL_HANDLE;

    Array<VkBuffer> uniform_buffers_;
    Array<VkDeviceMemory> uniform_buffers_memory_;
    Array<void*> uniform_buffers_mapped_;

    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    Array<VkDescriptorSet> descriptor_sets_;

    // Camera
    Math::Matrix4 view_matrix_;
    Math::Matrix4 projection_matrix_;
    Math::Vector3 camera_position_;

    float64 start_time_ = 0.0;
    float32 time_ = 0.0f;
};

} // namespace DRX::Renderer

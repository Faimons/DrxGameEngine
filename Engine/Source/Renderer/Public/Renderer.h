#pragma once

/**
 * DRX Engine - Renderer Interface
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Platform.h"
#include "Core/Public/Math/Matrix4.h"
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
// Render Pass
// ============================================================================

class DRX_API RenderPass {
public:
    RenderPass();
    ~RenderPass();

    bool Initialize(HAL::GraphicsDevice* device);
    void Shutdown();

    VkRenderPass GetHandle() const { return render_pass_; }

private:
    HAL::GraphicsDevice* device_ = nullptr;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
};

// ============================================================================
// Pipeline
// ============================================================================

struct PipelineConfig {
    String vertex_shader_path;
    String fragment_shader_path;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    bool enable_depth_test = true;
    bool enable_blending = false;
    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    VkPolygonMode polygon_mode = VK_POLYGON_MODE_FILL;
};

class DRX_API Pipeline {
public:
    Pipeline();
    ~Pipeline();

    bool Initialize(HAL::GraphicsDevice* device, const PipelineConfig& config);
    void Shutdown();

    void Bind(VkCommandBuffer cmd);

    VkPipeline GetHandle() const { return pipeline_; }
    VkPipelineLayout GetLayout() const { return layout_; }

private:
    VkShaderModule CreateShaderModule(const Array<byte>& code);

    HAL::GraphicsDevice* device_ = nullptr;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    bool owns_layout_ = false;
};

// ============================================================================
// Forward Renderer
// ============================================================================

class DRX_API ForwardRenderer {
public:
    ForwardRenderer();
    ~ForwardRenderer();

    bool Initialize(HAL::GraphicsDevice* device);
    void Shutdown();

    void BeginFrame();
    void EndFrame();

    void SetCamera(const Math::Matrix4& view, const Math::Matrix4& projection);
    void SetCameraPosition(const Math::Vector3& pos);

    void DrawTriangle();

    void OnResize(uint32 width, uint32 height);

private:
    bool CreateRenderPass();
    bool CreateFramebuffers();
    bool CreatePipeline();
    bool CreateVertexBuffer();
    bool CreateUniformBuffers();
    bool CreateDescriptorPool();
    bool CreateDescriptorSets();

    void CleanupFramebuffers();
    void UpdateUniformBuffer(uint32 frame_index);

    HAL::GraphicsDevice* device_ = nullptr;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    Array<VkFramebuffer> framebuffers_;

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

    float32 time_ = 0.0f;
};

} // namespace DRX::Renderer

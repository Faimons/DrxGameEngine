/**
 * DRX Engine - Deferred Renderer
 * G-Buffer based deferred rendering pipeline for complex lighting
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Vector4.h"
#include "Core/Public/Math/Matrix4.h"
#include "HAL/Public/GraphicsDevice.h"
#include "Renderer/Public/Texture.h"
#include "Renderer/Public/Mesh.h"
#include "Renderer/Public/Material.h"

#include <vulkan/vulkan.h>

namespace DRX::Renderer {

/**
 * G-Buffer attachment types
 */
enum class GBufferAttachment : uint32 {
    Position = 0,      // RGB: World position, A: unused
    Normal = 1,        // RGB: World normal (encoded), A: unused
    Albedo = 2,        // RGB: Albedo color, A: Alpha
    MetallicRoughness = 3,  // R: Metallic, G: Roughness, B: AO, A: unused
    Emissive = 4,      // RGB: Emissive color, A: Emissive strength
    Depth = 5,         // Depth buffer
    Count = 6
};

/**
 * Light types for deferred lighting pass
 */
enum class LightType : uint32 {
    Directional = 0,
    Point = 1,
    Spot = 2
};

/**
 * Light data for GPU
 */
struct alignas(16) GPULight {
    Math::Vector4 position;       // xyz: position, w: type
    Math::Vector4 direction;      // xyz: direction, w: inner cone
    Math::Vector4 color;          // xyz: color, w: intensity
    Math::Vector4 params;         // x: range, y: outer cone, z: shadow index, w: enabled
};

/**
 * Global lighting uniforms
 */
struct alignas(16) LightingUniforms {
    Math::Matrix4 view;
    Math::Matrix4 projection;
    Math::Matrix4 inv_view_projection;
    Math::Vector4 camera_position;
    Math::Vector4 ambient_color;
    uint32 num_lights;
    uint32 padding[3];
};

/**
 * G-Buffer for deferred rendering
 */
class GBuffer {
public:
    GBuffer() = default;
    ~GBuffer();

    bool Create(HAL::GraphicsDevice* device, uint32 width, uint32 height);
    void Destroy();
    void Resize(uint32 width, uint32 height);

    VkFramebuffer GetFramebuffer() const { return framebuffer_; }
    VkRenderPass GetRenderPass() const { return render_pass_; }

    SharedPtr<Texture> GetAttachment(GBufferAttachment attachment) const;
    VkImageView GetAttachmentView(GBufferAttachment attachment) const;

    uint32 GetWidth() const { return width_; }
    uint32 GetHeight() const { return height_; }

private:
    bool CreateAttachments();
    bool CreateRenderPass();
    bool CreateFramebuffer();

    HAL::GraphicsDevice* device_ = nullptr;
    uint32 width_ = 0;
    uint32 height_ = 0;

    Array<SharedPtr<Texture>> attachments_;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
};

/**
 * Deferred Renderer
 * Implements G-Buffer based deferred shading
 */
class DeferredRenderer {
public:
    static constexpr uint32 MAX_LIGHTS = 256;

    DeferredRenderer() = default;
    ~DeferredRenderer();

    bool Initialize(HAL::GraphicsDevice* device, uint32 width, uint32 height);
    void Shutdown();
    void Resize(uint32 width, uint32 height);

    // Frame rendering
    void BeginFrame();
    void EndFrame();

    // Geometry pass - renders to G-Buffer
    void BeginGeometryPass();
    void EndGeometryPass();

    // Lighting pass - reads G-Buffer, outputs to screen
    void BeginLightingPass();
    void EndLightingPass();

    // Submit geometry
    void SubmitMesh(const SharedPtr<Mesh>& mesh, const SharedPtr<Material>& material,
                    const Math::Matrix4& transform);

    // Light management
    void AddDirectionalLight(const Math::Vector3& direction, const Math::Vector3& color, float32 intensity);
    void AddPointLight(const Math::Vector3& position, const Math::Vector3& color, float32 intensity, float32 range);
    void AddSpotLight(const Math::Vector3& position, const Math::Vector3& direction,
                      const Math::Vector3& color, float32 intensity, float32 range,
                      float32 inner_cone, float32 outer_cone);
    void ClearLights();

    // Camera
    void SetCamera(const Math::Matrix4& view, const Math::Matrix4& projection,
                   const Math::Vector3& position);

    // Settings
    void SetAmbientColor(const Math::Vector3& color) { ambient_color_ = color; }

    // Access
    GBuffer& GetGBuffer() { return gbuffer_; }
    HAL::GraphicsDevice* GetDevice() const { return device_; }

private:
    bool CreateGeometryPipeline();
    bool CreateLightingPipeline();
    bool CreateDescriptorSets();
    bool CreateUniformBuffers();
    void UpdateLightBuffer();
    void UpdateCameraBuffer();

    HAL::GraphicsDevice* device_ = nullptr;
    GBuffer gbuffer_;

    // Geometry pass
    VkPipeline geometry_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout geometry_pipeline_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout geometry_descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet geometry_descriptor_set_ = VK_NULL_HANDLE;

    // Lighting pass
    VkPipeline lighting_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout lighting_pipeline_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout lighting_descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet lighting_descriptor_set_ = VK_NULL_HANDLE;

    // Uniform buffers
    VkBuffer camera_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory camera_buffer_memory_ = VK_NULL_HANDLE;
    void* camera_buffer_mapped_ = nullptr;

    VkBuffer light_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory light_buffer_memory_ = VK_NULL_HANDLE;
    void* light_buffer_mapped_ = nullptr;

    // Descriptor pool
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;

    // Lights
    Array<GPULight> lights_;
    LightingUniforms lighting_uniforms_;
    Math::Vector3 ambient_color_ = {0.03f, 0.03f, 0.03f};

    // Current frame state
    VkCommandBuffer current_cmd_ = VK_NULL_HANDLE;
    bool in_geometry_pass_ = false;
    bool in_lighting_pass_ = false;
};

} // namespace DRX::Renderer

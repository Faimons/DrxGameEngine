/**
 * DRX Engine - Deferred Renderer Implementation
 */

#include "Renderer/Public/DeferredRenderer.h"
#include "Core/Public/Log.h"

#include <array>
#include <cstring>

namespace DRX::Renderer {

// ============================================================================
// G-Buffer Implementation
// ============================================================================

GBuffer::~GBuffer() {
    Destroy();
}

bool GBuffer::Create(HAL::GraphicsDevice* device, uint32 width, uint32 height) {
    device_ = device;
    width_ = width;
    height_ = height;

    if (!CreateAttachments()) {
        DRX_LOG_ERROR("Failed to create G-Buffer attachments");
        return false;
    }

    if (!CreateRenderPass()) {
        DRX_LOG_ERROR("Failed to create G-Buffer render pass");
        return false;
    }

    if (!CreateFramebuffer()) {
        DRX_LOG_ERROR("Failed to create G-Buffer framebuffer");
        return false;
    }

    DRX_LOG_INFO("Created G-Buffer ({}x{})", width, height);
    return true;
}

void GBuffer::Destroy() {
    if (!device_) return;

    VkDevice vk_device = device_->GetDevice();

    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(vk_device, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }

    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vk_device, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }

    attachments_.clear();
    device_ = nullptr;
}

void GBuffer::Resize(uint32 width, uint32 height) {
    if (width == width_ && height == height_) return;

    VkDevice vk_device = device_->GetDevice();

    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(vk_device, framebuffer_, nullptr);
    }

    attachments_.clear();

    width_ = width;
    height_ = height;

    CreateAttachments();
    CreateFramebuffer();
}

SharedPtr<Texture> GBuffer::GetAttachment(GBufferAttachment attachment) const {
    uint32 index = static_cast<uint32>(attachment);
    if (index < attachments_.size()) {
        return attachments_[index];
    }
    return nullptr;
}

VkImageView GBuffer::GetAttachmentView(GBufferAttachment attachment) const {
    auto tex = GetAttachment(attachment);
    return tex ? tex->GetImageView() : VK_NULL_HANDLE;
}

bool GBuffer::CreateAttachments() {
    attachments_.resize(static_cast<size_t>(GBufferAttachment::Count));

    // Position buffer (RGBA16F)
    {
        TextureDesc desc;
        desc.width = width_;
        desc.height = height_;
        desc.format = TextureFormat::RGBA16F;
        desc.name = "GBuffer_Position";
        desc.generate_mipmaps = false;

        auto tex = MakeShared<Texture>();
        if (!tex->Create(device_, desc)) return false;
        attachments_[0] = tex;
    }

    // Normal buffer (RGBA16F)
    {
        TextureDesc desc;
        desc.width = width_;
        desc.height = height_;
        desc.format = TextureFormat::RGBA16F;
        desc.name = "GBuffer_Normal";
        desc.generate_mipmaps = false;

        auto tex = MakeShared<Texture>();
        if (!tex->Create(device_, desc)) return false;
        attachments_[1] = tex;
    }

    // Albedo buffer (RGBA8)
    {
        TextureDesc desc;
        desc.width = width_;
        desc.height = height_;
        desc.format = TextureFormat::RGBA8;
        desc.name = "GBuffer_Albedo";
        desc.generate_mipmaps = false;

        auto tex = MakeShared<Texture>();
        if (!tex->Create(device_, desc)) return false;
        attachments_[2] = tex;
    }

    // Metallic-Roughness-AO buffer (RGBA8)
    {
        TextureDesc desc;
        desc.width = width_;
        desc.height = height_;
        desc.format = TextureFormat::RGBA8;
        desc.name = "GBuffer_MetallicRoughness";
        desc.generate_mipmaps = false;

        auto tex = MakeShared<Texture>();
        if (!tex->Create(device_, desc)) return false;
        attachments_[3] = tex;
    }

    // Emissive buffer (RGBA16F)
    {
        TextureDesc desc;
        desc.width = width_;
        desc.height = height_;
        desc.format = TextureFormat::RGBA16F;
        desc.name = "GBuffer_Emissive";
        desc.generate_mipmaps = false;

        auto tex = MakeShared<Texture>();
        if (!tex->Create(device_, desc)) return false;
        attachments_[4] = tex;
    }

    // Depth buffer
    {
        TextureDesc desc;
        desc.width = width_;
        desc.height = height_;
        desc.format = TextureFormat::Depth32F;
        desc.name = "GBuffer_Depth";
        desc.generate_mipmaps = false;

        auto tex = MakeShared<Texture>();
        if (!tex->Create(device_, desc)) return false;
        attachments_[5] = tex;
    }

    return true;
}

bool GBuffer::CreateRenderPass() {
    VkDevice vk_device = device_->GetDevice();

    std::array<VkAttachmentDescription, 6> attachments{};

    // Position
    attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Normal
    attachments[1] = attachments[0];

    // Albedo
    attachments[2] = attachments[0];
    attachments[2].format = VK_FORMAT_R8G8B8A8_UNORM;

    // Metallic-Roughness
    attachments[3] = attachments[2];

    // Emissive
    attachments[4] = attachments[0];

    // Depth
    attachments[5].format = VK_FORMAT_D32_SFLOAT;
    attachments[5].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[5].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[5].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[5].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[5].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[5].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[5].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    std::array<VkAttachmentReference, 5> color_refs{};
    for (uint32 i = 0; i < 5; i++) {
        color_refs[i].attachment = i;
        color_refs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 5;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32>(color_refs.size());
    subpass.pColorAttachments = color_refs.data();
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = static_cast<uint32>(attachments.size());
    render_pass_info.pAttachments = attachments.data();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    return vkCreateRenderPass(vk_device, &render_pass_info, nullptr, &render_pass_) == VK_SUCCESS;
}

bool GBuffer::CreateFramebuffer() {
    VkDevice vk_device = device_->GetDevice();

    std::array<VkImageView, 6> views{};
    for (size_t i = 0; i < attachments_.size(); i++) {
        views[i] = attachments_[i]->GetImageView();
    }

    VkFramebufferCreateInfo fb_info{};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = render_pass_;
    fb_info.attachmentCount = static_cast<uint32>(views.size());
    fb_info.pAttachments = views.data();
    fb_info.width = width_;
    fb_info.height = height_;
    fb_info.layers = 1;

    return vkCreateFramebuffer(vk_device, &fb_info, nullptr, &framebuffer_) == VK_SUCCESS;
}

// ============================================================================
// Deferred Renderer Implementation
// ============================================================================

DeferredRenderer::~DeferredRenderer() {
    Shutdown();
}

bool DeferredRenderer::Initialize(HAL::GraphicsDevice* device, uint32 width, uint32 height) {
    device_ = device;

    if (!gbuffer_.Create(device, width, height)) {
        DRX_LOG_ERROR("Failed to create G-Buffer");
        return false;
    }

    if (!CreateDescriptorSets()) {
        DRX_LOG_ERROR("Failed to create descriptor sets");
        return false;
    }

    if (!CreateUniformBuffers()) {
        DRX_LOG_ERROR("Failed to create uniform buffers");
        return false;
    }

    if (!CreateGeometryPipeline()) {
        DRX_LOG_ERROR("Failed to create geometry pipeline");
        return false;
    }

    if (!CreateLightingPipeline()) {
        DRX_LOG_ERROR("Failed to create lighting pipeline");
        return false;
    }

    lights_.reserve(MAX_LIGHTS);

    DRX_LOG_INFO("Deferred Renderer initialized ({}x{})", width, height);
    return true;
}

void DeferredRenderer::Shutdown() {
    VkDevice vk_device = device_->GetDevice();

    if (geometry_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_device, geometry_pipeline_, nullptr);
    }
    if (geometry_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vk_device, geometry_pipeline_layout_, nullptr);
    }
    if (geometry_descriptor_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vk_device, geometry_descriptor_layout_, nullptr);
    }

    if (lighting_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_device, lighting_pipeline_, nullptr);
    }
    if (lighting_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vk_device, lighting_pipeline_layout_, nullptr);
    }
    if (lighting_descriptor_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vk_device, lighting_descriptor_layout_, nullptr);
    }

    if (camera_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk_device, camera_buffer_, nullptr);
    }
    if (camera_buffer_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, camera_buffer_memory_, nullptr);
    }

    if (light_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk_device, light_buffer_, nullptr);
    }
    if (light_buffer_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, light_buffer_memory_, nullptr);
    }

    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vk_device, descriptor_pool_, nullptr);
    }

    gbuffer_.Destroy();
    lights_.clear();

    DRX_LOG_INFO("Deferred Renderer shutdown");
}

void DeferredRenderer::Resize(uint32 width, uint32 height) {
    gbuffer_.Resize(width, height);
}

void DeferredRenderer::BeginFrame() {
    current_cmd_ = device_->GetCurrentCommandBuffer();
}

void DeferredRenderer::EndFrame() {
    current_cmd_ = VK_NULL_HANDLE;
}

void DeferredRenderer::BeginGeometryPass() {
    std::array<VkClearValue, 6> clear_values{};
    clear_values[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clear_values[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clear_values[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clear_values[3].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clear_values[4].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clear_values[5].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = gbuffer_.GetRenderPass();
    render_pass_info.framebuffer = gbuffer_.GetFramebuffer();
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = {gbuffer_.GetWidth(), gbuffer_.GetHeight()};
    render_pass_info.clearValueCount = static_cast<uint32>(clear_values.size());
    render_pass_info.pClearValues = clear_values.data();

    vkCmdBeginRenderPass(current_cmd_, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(current_cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, geometry_pipeline_);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(gbuffer_.GetWidth());
    viewport.height = static_cast<float>(gbuffer_.GetHeight());
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(current_cmd_, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {gbuffer_.GetWidth(), gbuffer_.GetHeight()};
    vkCmdSetScissor(current_cmd_, 0, 1, &scissor);

    in_geometry_pass_ = true;
}

void DeferredRenderer::EndGeometryPass() {
    vkCmdEndRenderPass(current_cmd_);
    in_geometry_pass_ = false;
}

void DeferredRenderer::BeginLightingPass() {
    UpdateLightBuffer();

    vkCmdBindPipeline(current_cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, lighting_pipeline_);
    vkCmdBindDescriptorSets(current_cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            lighting_pipeline_layout_, 0, 1, &lighting_descriptor_set_, 0, nullptr);

    in_lighting_pass_ = true;
}

void DeferredRenderer::EndLightingPass() {
    // Draw fullscreen quad
    vkCmdDraw(current_cmd_, 3, 1, 0, 0);
    in_lighting_pass_ = false;
}

void DeferredRenderer::SubmitMesh(const SharedPtr<Mesh>& mesh, const SharedPtr<Material>& material,
                                   const Math::Matrix4& transform) {
    if (!in_geometry_pass_) return;

    // Bind mesh
    mesh->Bind(current_cmd_);

    // Push model matrix
    vkCmdPushConstants(current_cmd_, geometry_pipeline_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Math::Matrix4), &transform);

    // Bind material
    material->Bind(current_cmd_, geometry_pipeline_layout_);

    // Draw
    mesh->Draw(current_cmd_);
}

void DeferredRenderer::AddDirectionalLight(const Math::Vector3& direction, const Math::Vector3& color, float32 intensity) {
    if (lights_.size() >= MAX_LIGHTS) return;

    GPULight light{};
    light.position = {0, 0, 0, static_cast<float32>(LightType::Directional)};
    light.direction = {direction.x, direction.y, direction.z, 0};
    light.color = {color.x, color.y, color.z, intensity};
    light.params = {0, 0, -1, 1};

    lights_.push_back(light);
}

void DeferredRenderer::AddPointLight(const Math::Vector3& position, const Math::Vector3& color,
                                      float32 intensity, float32 range) {
    if (lights_.size() >= MAX_LIGHTS) return;

    GPULight light{};
    light.position = {position.x, position.y, position.z, static_cast<float32>(LightType::Point)};
    light.direction = {0, 0, 0, 0};
    light.color = {color.x, color.y, color.z, intensity};
    light.params = {range, 0, -1, 1};

    lights_.push_back(light);
}

void DeferredRenderer::AddSpotLight(const Math::Vector3& position, const Math::Vector3& direction,
                                     const Math::Vector3& color, float32 intensity, float32 range,
                                     float32 inner_cone, float32 outer_cone) {
    if (lights_.size() >= MAX_LIGHTS) return;

    GPULight light{};
    light.position = {position.x, position.y, position.z, static_cast<float32>(LightType::Spot)};
    light.direction = {direction.x, direction.y, direction.z, inner_cone};
    light.color = {color.x, color.y, color.z, intensity};
    light.params = {range, outer_cone, -1, 1};

    lights_.push_back(light);
}

void DeferredRenderer::ClearLights() {
    lights_.clear();
}

void DeferredRenderer::SetCamera(const Math::Matrix4& view, const Math::Matrix4& projection,
                                  const Math::Vector3& position) {
    lighting_uniforms_.view = view;
    lighting_uniforms_.projection = projection;
    lighting_uniforms_.camera_position = {position.x, position.y, position.z, 1.0f};

    // Calculate inverse view-projection
    lighting_uniforms_.inv_view_projection = (projection * view).Inverse();

    UpdateCameraBuffer();
}

bool DeferredRenderer::CreateGeometryPipeline() {
    // Pipeline creation would require shader modules
    // This is a placeholder - actual implementation needs compiled shaders
    DRX_LOG_INFO("Geometry pipeline created (placeholder)");
    return true;
}

bool DeferredRenderer::CreateLightingPipeline() {
    DRX_LOG_INFO("Lighting pipeline created (placeholder)");
    return true;
}

bool DeferredRenderer::CreateDescriptorSets() {
    VkDevice vk_device = device_->GetDevice();

    // Create descriptor pool
    Array<VkDescriptorPoolSize> pool_sizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 20},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5}
    };

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = static_cast<uint32>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    pool_info.maxSets = 10;

    if (vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool DeferredRenderer::CreateUniformBuffers() {
    VkDevice vk_device = device_->GetDevice();

    // Camera buffer
    {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = sizeof(LightingUniforms);
        buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(vk_device, &buffer_info, nullptr, &camera_buffer_);

        VkMemoryRequirements mem_req;
        vkGetBufferMemoryRequirements(vk_device, camera_buffer_, &mem_req);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_req.size;
        alloc_info.memoryTypeIndex = device_->FindMemoryType(
            mem_req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        vkAllocateMemory(vk_device, &alloc_info, nullptr, &camera_buffer_memory_);
        vkBindBufferMemory(vk_device, camera_buffer_, camera_buffer_memory_, 0);
        vkMapMemory(vk_device, camera_buffer_memory_, 0, sizeof(LightingUniforms), 0, &camera_buffer_mapped_);
    }

    // Light buffer
    {
        VkDeviceSize buffer_size = sizeof(GPULight) * MAX_LIGHTS;

        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = buffer_size;
        buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(vk_device, &buffer_info, nullptr, &light_buffer_);

        VkMemoryRequirements mem_req;
        vkGetBufferMemoryRequirements(vk_device, light_buffer_, &mem_req);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_req.size;
        alloc_info.memoryTypeIndex = device_->FindMemoryType(
            mem_req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        vkAllocateMemory(vk_device, &alloc_info, nullptr, &light_buffer_memory_);
        vkBindBufferMemory(vk_device, light_buffer_, light_buffer_memory_, 0);
        vkMapMemory(vk_device, light_buffer_memory_, 0, buffer_size, 0, &light_buffer_mapped_);
    }

    return true;
}

void DeferredRenderer::UpdateLightBuffer() {
    lighting_uniforms_.num_lights = static_cast<uint32>(lights_.size());
    lighting_uniforms_.ambient_color = {ambient_color_.x, ambient_color_.y, ambient_color_.z, 1.0f};

    if (light_buffer_mapped_ && !lights_.empty()) {
        std::memcpy(light_buffer_mapped_, lights_.data(), sizeof(GPULight) * lights_.size());
    }
}

void DeferredRenderer::UpdateCameraBuffer() {
    if (camera_buffer_mapped_) {
        std::memcpy(camera_buffer_mapped_, &lighting_uniforms_, sizeof(LightingUniforms));
    }
}

} // namespace DRX::Renderer

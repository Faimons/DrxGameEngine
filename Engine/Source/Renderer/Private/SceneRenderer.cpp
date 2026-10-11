/**
 * DRX Engine - Scene Renderer: setup, resources and pass recording
 */

#include "Renderer/Public/SceneRenderer.h"
#include "Core/Public/Log.h"
#include "Core/Public/Platform.h"

#include "Shaders/lit.vert.h"
#include "Shaders/lit.frag.h"
#include "Shaders/shadow.vert.h"
#include "Shaders/sky.vert.h"
#include "Shaders/sky.frag.h"
#include "Shaders/particle.vert.h"
#include "Shaders/particle.frag.h"
#include "Shaders/line.vert.h"
#include "Shaders/line.frag.h"
#include "Shaders/ui.vert.h"
#include "Shaders/ui.frag.h"
#include "Embedded/Roboto-Medium.ttf.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace DRX::Renderer {

namespace {

// std140 mirror of FrameUniforms in common.glsl
struct FrameUniforms {
    Math::Matrix4 view;
    Math::Matrix4 projection;
    Math::Matrix4 view_projection;
    Math::Matrix4 inverse_view_projection;
    Math::Matrix4 light_view_projection;
    Math::Vector4 camera_position;
    Math::Vector4 sun_direction;
    Math::Vector4 sun_color;
    Math::Vector4 sky_zenith;
    Math::Vector4 sky_horizon;
    Math::Vector4 ground_color;
    Math::Vector4 ambient_sky;
    Math::Vector4 ambient_ground;
    Math::Vector4 fog_color;
    Math::Vector4 shadow_params;
    Math::Vector4 exposure;
};
static_assert(sizeof(FrameUniforms) == 5 * 64 + 11 * 16, "FrameUniforms must match std140 layout");

// Mirrors DrawConstants in common.glsl
struct DrawConstants {
    Math::Matrix4 model;
    Math::Vector4 color;
    Math::Vector4 params;
    Math::Vector4 emissive;
};
static_assert(sizeof(DrawConstants) <= 128, "Push constants must fit the guaranteed 128 bytes");

struct UIConstants {
    Math::Vector4 transform;
    Math::Vector4 mode;
};

constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

enum BlendMode : int { kBlendNone = 0, kBlendAlpha = 1, kBlendAdditive = 2 };

struct Plane {
    Math::Vector3 normal;
    float32 distance;
};

// Frustum planes (pointing inwards) for Vulkan clip space (depth 0..1)
void ExtractFrustum(const Math::Matrix4& m, Plane planes[6]) {
    auto row = [&m](int r) { return Math::Vector4(m[0][r], m[1][r], m[2][r], m[3][r]); };
    const Math::Vector4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    const Math::Vector4 raw[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2};
    for (int i = 0; i < 6; ++i) {
        const Math::Vector3 n(raw[i].x, raw[i].y, raw[i].z);
        const float32 len = n.Length();
        planes[i].normal = len > 0.0f ? n / len : n;
        planes[i].distance = len > 0.0f ? raw[i].w / len : raw[i].w;
    }
}

bool IsVisible(const Plane planes[6], const AABB& box) {
    if (!box.IsValid()) return true;
    for (int i = 0; i < 6; ++i) {
        const Math::Vector3 p(planes[i].normal.x >= 0.0f ? box.max.x : box.min.x,
                              planes[i].normal.y >= 0.0f ? box.max.y : box.min.y,
                              planes[i].normal.z >= 0.0f ? box.max.z : box.min.z);
        if (Math::Vector3::Dot(planes[i].normal, p) + planes[i].distance < 0.0f) {
            return false;
        }
    }
    return true;
}

VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, size_t size) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = code;
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return module;
}

Math::Vector4 ToVec4(const Math::Vector3& v, float32 w) {
    return Math::Vector4(v.x, v.y, v.z, w);
}

} // namespace

// ============================================================================
// Lifecycle
// ============================================================================

SceneRenderer::SceneRenderer() = default;

SceneRenderer::~SceneRenderer() {
    Shutdown();
}

bool SceneRenderer::Initialize(HAL::GraphicsDevice* device, const SceneRendererConfig& config) {
    if (!device || device->GetDevice() == VK_NULL_HANDLE) {
        DRX_LOG_ERROR("SceneRenderer requires an initialized graphics device");
        return false;
    }
    if (device_) {
        Shutdown();
    }

    device_ = device;
    config_ = config;
    swap_chain_generation_ = device_->GetSwapChainGeneration();
    start_time_ = Platform::GetTimeSeconds();

    // Index 0 is reserved as "invalid" so that default handles never alias a resource
    meshes_.assign(1, GpuMesh{});
    textures_.assign(1, GpuTexture{});

    const bool ok =
        CreateDescriptorLayouts() &&
        CreateSamplers() &&
        CreateShadowResources() &&
        CreateMainRenderPass() &&
        CreateDepthResources() &&
        CreateFramebuffers() &&
        CreatePipelines() &&
        CreateFrameResources() &&
        CreateDefaultTextures();

    if (!ok) {
        DRX_LOG_ERROR("SceneRenderer initialization failed");
        Shutdown();
        return false;
    }

    DRX_LOG_INFO("Scene renderer initialized (shadow map {}x{}, depth format {})",
                 config_.shadow_map_size, config_.shadow_map_size, static_cast<int>(depth_format_));
    return true;
}

void SceneRenderer::Shutdown() {
    if (!device_) return;

    VkDevice vk = device_->GetDevice();
    vkDeviceWaitIdle(vk);

    ProcessPendingDestroys(true);

    for (uint32 i = 1; i < meshes_.size(); ++i) {
        auto& mesh = meshes_[i];
        if (!mesh.alive) continue;
        device_->DestroyBuffer(mesh.vertex_buffer, mesh.vertex_memory);
        device_->DestroyBuffer(mesh.index_buffer, mesh.index_memory);
        for (size_t f = 0; f < mesh.frame_buffers.size(); ++f) device_->DestroyBuffer(mesh.frame_buffers[f], mesh.frame_memory[f]);
    }
    meshes_.clear();
    free_meshes_.clear();

    for (uint32 i = 1; i < textures_.size(); ++i) {
        auto& texture = textures_[i];
        if (!texture.alive) continue;
        if (texture.view != VK_NULL_HANDLE) vkDestroyImageView(vk, texture.view, nullptr);
        device_->DestroyImage(texture.image, texture.memory);
    }
    textures_.clear();
    free_textures_.clear();

    DestroyDynamicBuffer(screenshot_.buffer);
    screenshot_ = Screenshot{};

    for (auto& frame : frames_) {
        DestroyDynamicBuffer(frame.uniforms);
        DestroyDynamicBuffer(frame.particles);
        DestroyDynamicBuffer(frame.lines);
        DestroyDynamicBuffer(frame.ui);
    }
    frames_.clear();

    VkPipeline* pipelines[] = {&lit_pipeline_, &lit_double_sided_pipeline_, &lit_transparent_pipeline_,
                               &shadow_pipeline_, &sky_pipeline_, &particle_alpha_pipeline_,
                               &particle_additive_pipeline_, &line_pipeline_, &ui_pipeline_};
    for (VkPipeline* pipeline : pipelines) {
        if (*pipeline != VK_NULL_HANDLE) vkDestroyPipeline(vk, *pipeline, nullptr);
        *pipeline = VK_NULL_HANDLE;
    }

    DestroySwapChainResources();

    if (main_pass_ != VK_NULL_HANDLE) vkDestroyRenderPass(vk, main_pass_, nullptr);
    if (shadow_framebuffer_ != VK_NULL_HANDLE) vkDestroyFramebuffer(vk, shadow_framebuffer_, nullptr);
    if (shadow_pass_ != VK_NULL_HANDLE) vkDestroyRenderPass(vk, shadow_pass_, nullptr);
    if (shadow_view_ != VK_NULL_HANDLE) vkDestroyImageView(vk, shadow_view_, nullptr);
    device_->DestroyImage(shadow_image_, shadow_memory_);
    if (linear_sampler_ != VK_NULL_HANDLE) vkDestroySampler(vk, linear_sampler_, nullptr);
    if (shadow_sampler_ != VK_NULL_HANDLE) vkDestroySampler(vk, shadow_sampler_, nullptr);
    if (pipeline_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(vk, pipeline_layout_, nullptr);
    if (descriptor_pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(vk, descriptor_pool_, nullptr);
    if (frame_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(vk, frame_layout_, nullptr);
    if (texture_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(vk, texture_layout_, nullptr);

    main_pass_ = shadow_pass_ = VK_NULL_HANDLE;
    shadow_framebuffer_ = VK_NULL_HANDLE;
    shadow_view_ = VK_NULL_HANDLE;
    linear_sampler_ = shadow_sampler_ = VK_NULL_HANDLE;
    pipeline_layout_ = VK_NULL_HANDLE;
    descriptor_pool_ = VK_NULL_HANDLE;
    frame_layout_ = texture_layout_ = VK_NULL_HANDLE;
    white_texture_ = particle_texture_ = font_texture_ = TextureHandle();
    frame_begun_ = false;

    device_ = nullptr;
    DRX_LOG_INFO("Scene renderer shutdown");
}

VkExtent2D SceneRenderer::GetFramebufferExtent() const {
    return device_ ? device_->GetSwapChainExtent() : VkExtent2D{0, 0};
}

// ============================================================================
// Setup
// ============================================================================

bool SceneRenderer::CreateDescriptorLayouts() {
    VkDevice vk = device_->GetDevice();

    VkDescriptorSetLayoutBinding frame_bindings[2] = {};
    frame_bindings[0].binding = 0;
    frame_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frame_bindings[0].descriptorCount = 1;
    frame_bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    frame_bindings[1].binding = 1;
    frame_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    frame_bindings[1].descriptorCount = 1;
    frame_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo frame_info{};
    frame_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    frame_info.bindingCount = 2;
    frame_info.pBindings = frame_bindings;

    VkDescriptorSetLayoutBinding texture_binding{};
    texture_binding.binding = 0;
    texture_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texture_binding.descriptorCount = 1;
    texture_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo texture_info{};
    texture_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    texture_info.bindingCount = 1;
    texture_info.pBindings = &texture_binding;

    if (vkCreateDescriptorSetLayout(vk, &frame_info, nullptr, &frame_layout_) != VK_SUCCESS ||
        vkCreateDescriptorSetLayout(vk, &texture_info, nullptr, &texture_layout_) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create descriptor set layouts");
        return false;
    }

    const uint32 frame_count = device_->GetMaxFramesInFlight();
    VkDescriptorPoolSize pool_sizes[2] = {};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = frame_count;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount = frame_count + config_.max_textures;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = frame_count + config_.max_textures;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(vk, &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create descriptor pool");
        return false;
    }

    const VkDescriptorSetLayout set_layouts[2] = {frame_layout_, texture_layout_};
    VkPushConstantRange push_range{};
    push_range.stageFlags = kPushStages;
    push_range.offset = 0;
    push_range.size = sizeof(DrawConstants);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 2;
    layout_info.pSetLayouts = set_layouts;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;

    if (vkCreatePipelineLayout(vk, &layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create pipeline layout");
        return false;
    }
    return true;
}

bool SceneRenderer::CreateSamplers() {
    VkDevice vk = device_->GetDevice();

    VkSamplerCreateInfo linear{};
    linear.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    linear.magFilter = VK_FILTER_LINEAR;
    linear.minFilter = VK_FILTER_LINEAR;
    linear.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    linear.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    linear.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    linear.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    linear.maxLod = VK_LOD_CLAMP_NONE;
    if (device_->IsAnisotropyEnabled()) {
        linear.anisotropyEnable = VK_TRUE;
        linear.maxAnisotropy = std::min(8.0f, device_->GetMaxAnisotropy());
    }

    VkSamplerCreateInfo shadow{};
    shadow.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    shadow.magFilter = VK_FILTER_NEAREST;
    shadow.minFilter = VK_FILTER_NEAREST;
    shadow.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    shadow.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadow.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadow.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadow.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    shadow.compareEnable = VK_TRUE;
    shadow.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    shadow.maxLod = 0.0f;

    // Hardware PCF (linear compare filtering) when the depth format supports it
    shadow_format_ = device_->FindDepthFormat(true);
    if (shadow_format_ != VK_FORMAT_UNDEFINED) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(device_->GetPhysicalDevice(), shadow_format_, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) {
            shadow.magFilter = VK_FILTER_LINEAR;
            shadow.minFilter = VK_FILTER_LINEAR;
        }
    }

    if (vkCreateSampler(vk, &linear, nullptr, &linear_sampler_) != VK_SUCCESS ||
        vkCreateSampler(vk, &shadow, nullptr, &shadow_sampler_) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create samplers");
        return false;
    }
    return true;
}

bool SceneRenderer::CreateShadowResources() {
    VkDevice vk = device_->GetDevice();
    if (shadow_format_ == VK_FORMAT_UNDEFINED) {
        DRX_LOG_ERROR("No sampleable depth format for shadow maps");
        return false;
    }

    const uint32 size = std::max(256u, config_.shadow_map_size);

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = shadow_format_;
    image_info.extent = {size, size, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!device_->CreateImage(image_info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, shadow_image_, shadow_memory_)) {
        return false;
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = shadow_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = shadow_format_;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(vk, &view_info, nullptr, &shadow_view_) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create shadow map view");
        return false;
    }

    VkAttachmentDescription depth{};
    depth.format = shadow_format_;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depth_ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dependencies[2] = {};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo pass_info{};
    pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass_info.attachmentCount = 1;
    pass_info.pAttachments = &depth;
    pass_info.subpassCount = 1;
    pass_info.pSubpasses = &subpass;
    pass_info.dependencyCount = 2;
    pass_info.pDependencies = dependencies;

    if (vkCreateRenderPass(vk, &pass_info, nullptr, &shadow_pass_) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create shadow render pass");
        return false;
    }

    VkFramebufferCreateInfo fb_info{};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = shadow_pass_;
    fb_info.attachmentCount = 1;
    fb_info.pAttachments = &shadow_view_;
    fb_info.width = size;
    fb_info.height = size;
    fb_info.layers = 1;

    if (vkCreateFramebuffer(vk, &fb_info, nullptr, &shadow_framebuffer_) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create shadow framebuffer");
        return false;
    }
    config_.shadow_map_size = size;
    return true;
}

bool SceneRenderer::CreateMainRenderPass() {
    depth_format_ = device_->FindDepthFormat(false);
    if (depth_format_ == VK_FORMAT_UNDEFINED) {
        DRX_LOG_ERROR("No supported depth format");
        return false;
    }

    VkAttachmentDescription attachments[2] = {};
    attachments[0].format = device_->GetSwapChainFormat();
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format = depth_format_;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    if (vkCreateRenderPass(device_->GetDevice(), &info, nullptr, &main_pass_) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create main render pass");
        return false;
    }
    return true;
}

bool SceneRenderer::CreateDepthResources() {
    const VkExtent2D extent = device_->GetSwapChainExtent();

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = depth_format_;
    image_info.extent = {std::max(1u, extent.width), std::max(1u, extent.height), 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!device_->CreateImage(image_info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depth_image_, depth_memory_)) {
        return false;
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = depth_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = depth_format_;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_->GetDevice(), &view_info, nullptr, &depth_view_) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create depth image view");
        return false;
    }
    return true;
}

bool SceneRenderer::CreateFramebuffers() {
    const auto& views = device_->GetSwapChainImageViews();
    const VkExtent2D extent = device_->GetSwapChainExtent();
    framebuffers_.assign(views.size(), VK_NULL_HANDLE);

    for (size_t i = 0; i < views.size(); ++i) {
        const VkImageView attachments[2] = {views[i], depth_view_};

        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = main_pass_;
        info.attachmentCount = 2;
        info.pAttachments = attachments;
        info.width = extent.width;
        info.height = extent.height;
        info.layers = 1;

        if (vkCreateFramebuffer(device_->GetDevice(), &info, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            DRX_LOG_ERROR("Failed to create framebuffer {}", i);
            return false;
        }
    }
    swap_chain_generation_ = device_->GetSwapChainGeneration();
    return true;
}

void SceneRenderer::DestroySwapChainResources() {
    VkDevice vk = device_->GetDevice();
    for (auto framebuffer : framebuffers_) {
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(vk, framebuffer, nullptr);
    }
    framebuffers_.clear();
    if (depth_view_ != VK_NULL_HANDLE) vkDestroyImageView(vk, depth_view_, nullptr);
    depth_view_ = VK_NULL_HANDLE;
    device_->DestroyImage(depth_image_, depth_memory_);
}

void SceneRenderer::RecreateSwapChainResources() {
    vkDeviceWaitIdle(device_->GetDevice());
    DestroySwapChainResources();
    if (!CreateDepthResources() || !CreateFramebuffers()) {
        DRX_LOG_FATAL("Failed to recreate swap chain dependent resources");
    }
}

VkPipeline SceneRenderer::CreatePipeline(const uint32_t* vert, size_t vert_size, const uint32_t* frag, size_t frag_size,
                                         VkRenderPass pass, const VkPipelineVertexInputStateCreateInfo& vertex_input,
                                         VkPrimitiveTopology topology, VkCullModeFlags cull, bool depth_test,
                                         bool depth_write, int blend_mode, bool depth_bias) {
    VkDevice vk = device_->GetDevice();

    VkShaderModule vert_module = CreateShaderModule(vk, vert, vert_size);
    VkShaderModule frag_module = frag ? CreateShaderModule(vk, frag, frag_size) : VK_NULL_HANDLE;
    if (vert_module == VK_NULL_HANDLE || (frag && frag_module == VK_NULL_HANDLE)) {
        if (vert_module != VK_NULL_HANDLE) vkDestroyShaderModule(vk, vert_module, nullptr);
        if (frag_module != VK_NULL_HANDLE) vkDestroyShaderModule(vk, frag_module, nullptr);
        return VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_module;
    stages[1].pName = "main";

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = topology;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = cull;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    if (depth_bias) {
        raster.depthBiasEnable = VK_TRUE;
        raster.depthBiasConstantFactor = 1.25f;
        raster.depthBiasSlopeFactor = 1.75f;
    }

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = depth_test ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (blend_mode != kBlendNone) {
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = blend_mode == kBlendAdditive ? VK_BLEND_FACTOR_ONE
                                                                 : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = frag ? 1u : 0u;
    color_blend.pAttachments = &blend;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = frag ? 2u : 1u;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport_state;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &color_blend;
    info.pDynamicState = &dynamic;
    info.layout = pipeline_layout_;
    info.renderPass = pass;
    info.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
        pipeline = VK_NULL_HANDLE;
    }

    vkDestroyShaderModule(vk, vert_module, nullptr);
    if (frag_module != VK_NULL_HANDLE) vkDestroyShaderModule(vk, frag_module, nullptr);
    return pipeline;
}

bool SceneRenderer::CreatePipelines() {
    // Lit / shadow vertex layout
    VkVertexInputBindingDescription mesh_binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription mesh_attributes[4] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32>(offsetof(Vertex, position))},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32>(offsetof(Vertex, normal))},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32>(offsetof(Vertex, color))},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32>(offsetof(Vertex, tex_coord))},
    };
    VkPipelineVertexInputStateCreateInfo mesh_input{};
    mesh_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    mesh_input.vertexBindingDescriptionCount = 1;
    mesh_input.pVertexBindingDescriptions = &mesh_binding;
    mesh_input.vertexAttributeDescriptionCount = 4;
    mesh_input.pVertexAttributeDescriptions = mesh_attributes;

    VkPipelineVertexInputStateCreateInfo shadow_input = mesh_input;
    shadow_input.vertexAttributeDescriptionCount = 1;

    VkPipelineVertexInputStateCreateInfo empty_input{};
    empty_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkVertexInputBindingDescription particle_binding{0, sizeof(ParticleVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription particle_attributes[3] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32>(offsetof(ParticleVertex, position))},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32>(offsetof(ParticleVertex, tex_coord))},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32>(offsetof(ParticleVertex, color))},
    };
    VkPipelineVertexInputStateCreateInfo particle_input{};
    particle_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    particle_input.vertexBindingDescriptionCount = 1;
    particle_input.pVertexBindingDescriptions = &particle_binding;
    particle_input.vertexAttributeDescriptionCount = 3;
    particle_input.pVertexAttributeDescriptions = particle_attributes;

    VkVertexInputBindingDescription line_binding{0, sizeof(LineVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription line_attributes[2] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32>(offsetof(LineVertex, position))},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32>(offsetof(LineVertex, color))},
    };
    VkPipelineVertexInputStateCreateInfo line_input{};
    line_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    line_input.vertexBindingDescriptionCount = 1;
    line_input.pVertexBindingDescriptions = &line_binding;
    line_input.vertexAttributeDescriptionCount = 2;
    line_input.pVertexAttributeDescriptions = line_attributes;

    VkVertexInputBindingDescription ui_binding{0, sizeof(UIVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription ui_attributes[3] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32>(offsetof(UIVertex, position))},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32>(offsetof(UIVertex, tex_coord))},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32>(offsetof(UIVertex, color))},
    };
    VkPipelineVertexInputStateCreateInfo ui_input{};
    ui_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    ui_input.vertexBindingDescriptionCount = 1;
    ui_input.pVertexBindingDescriptions = &ui_binding;
    ui_input.vertexAttributeDescriptionCount = 3;
    ui_input.pVertexAttributeDescriptions = ui_attributes;

    const auto tri = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    lit_pipeline_ = CreatePipeline(kShader_lit_vert, sizeof(kShader_lit_vert), kShader_lit_frag, sizeof(kShader_lit_frag),
                                   main_pass_, mesh_input, tri, VK_CULL_MODE_BACK_BIT, true, true, kBlendNone, false);
    lit_double_sided_pipeline_ = CreatePipeline(kShader_lit_vert, sizeof(kShader_lit_vert), kShader_lit_frag,
                                                sizeof(kShader_lit_frag), main_pass_, mesh_input, tri,
                                                VK_CULL_MODE_NONE, true, true, kBlendNone, false);
    lit_transparent_pipeline_ = CreatePipeline(kShader_lit_vert, sizeof(kShader_lit_vert), kShader_lit_frag,
                                               sizeof(kShader_lit_frag), main_pass_, mesh_input, tri,
                                               VK_CULL_MODE_NONE, true, false, kBlendAlpha, false);
    shadow_pipeline_ = CreatePipeline(kShader_shadow_vert, sizeof(kShader_shadow_vert), nullptr, 0, shadow_pass_,
                                      shadow_input, tri, VK_CULL_MODE_NONE, true, true, kBlendNone, true);
    sky_pipeline_ = CreatePipeline(kShader_sky_vert, sizeof(kShader_sky_vert), kShader_sky_frag, sizeof(kShader_sky_frag),
                                   main_pass_, empty_input, tri, VK_CULL_MODE_NONE, false, false, kBlendNone, false);
    particle_alpha_pipeline_ = CreatePipeline(kShader_particle_vert, sizeof(kShader_particle_vert), kShader_particle_frag,
                                              sizeof(kShader_particle_frag), main_pass_, particle_input, tri,
                                              VK_CULL_MODE_NONE, true, false, kBlendAlpha, false);
    particle_additive_pipeline_ = CreatePipeline(kShader_particle_vert, sizeof(kShader_particle_vert),
                                                 kShader_particle_frag, sizeof(kShader_particle_frag), main_pass_,
                                                 particle_input, tri, VK_CULL_MODE_NONE, true, false, kBlendAdditive,
                                                 false);
    line_pipeline_ = CreatePipeline(kShader_line_vert, sizeof(kShader_line_vert), kShader_line_frag,
                                    sizeof(kShader_line_frag), main_pass_, line_input, VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
                                    VK_CULL_MODE_NONE, true, false, kBlendAlpha, false);
    ui_pipeline_ = CreatePipeline(kShader_ui_vert, sizeof(kShader_ui_vert), kShader_ui_frag, sizeof(kShader_ui_frag),
                                  main_pass_, ui_input, tri, VK_CULL_MODE_NONE, false, false, kBlendAlpha, false);

    const VkPipeline all[] = {lit_pipeline_, lit_double_sided_pipeline_, lit_transparent_pipeline_, shadow_pipeline_,
                              sky_pipeline_, particle_alpha_pipeline_, particle_additive_pipeline_, line_pipeline_,
                              ui_pipeline_};
    for (VkPipeline pipeline : all) {
        if (pipeline == VK_NULL_HANDLE) {
            DRX_LOG_ERROR("Failed to create scene renderer pipelines");
            return false;
        }
    }
    return true;
}

bool SceneRenderer::CreateFrameResources() {
    VkDevice vk = device_->GetDevice();
    frames_.resize(device_->GetMaxFramesInFlight());

    for (auto& frame : frames_) {
        if (!EnsureCapacity(frame.uniforms, sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) {
            return false;
        }

        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = descriptor_pool_;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &frame_layout_;
        if (vkAllocateDescriptorSets(vk, &alloc, &frame.frame_set) != VK_SUCCESS) {
            DRX_LOG_ERROR("Failed to allocate frame descriptor set");
            return false;
        }

        VkDescriptorBufferInfo buffer_info{frame.uniforms.buffer, 0, sizeof(FrameUniforms)};
        VkDescriptorImageInfo shadow_info{shadow_sampler_, shadow_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        VkWriteDescriptorSet writes[2] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = frame.frame_set;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &buffer_info;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = frame.frame_set;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &shadow_info;
        vkUpdateDescriptorSets(vk, 2, writes, 0, nullptr);
    }
    return true;
}

bool SceneRenderer::CreateDefaultTextures() {
    const uint8 white[4] = {255, 255, 255, 255};
    white_texture_ = CreateTexture(1, 1, white, false, false, "white");

    // Soft round sprite for particles (premultiplied falloff in alpha)
    constexpr uint32 kSize = 64;
    Array<uint8> soft(kSize * kSize * 4);
    for (uint32 y = 0; y < kSize; ++y) {
        for (uint32 x = 0; x < kSize; ++x) {
            const float32 dx = (static_cast<float32>(x) + 0.5f) / kSize * 2.0f - 1.0f;
            const float32 dy = (static_cast<float32>(y) + 0.5f) / kSize * 2.0f - 1.0f;
            const float32 falloff = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
            uint8* p = &soft[(y * kSize + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = static_cast<uint8>(std::round(falloff * falloff * 255.0f));
        }
    }
    particle_texture_ = CreateTexture(kSize, kSize, soft.data(), false, true, "soft_particle");

    // Font atlas (R8 coverage expanded to RGBA so it shares the texture path)
    if (!font_.Build(kFontRoboto, kFontRoboto_size, config_.font_sizes, 1024)) {
        DRX_LOG_ERROR("Failed to build font atlas");
        return false;
    }
    const auto& coverage = font_.GetPixels();
    Array<uint8> rgba(coverage.size() * 4);
    for (size_t i = 0; i < coverage.size(); ++i) {
        rgba[i * 4 + 0] = coverage[i];
        rgba[i * 4 + 1] = coverage[i];
        rgba[i * 4 + 2] = coverage[i];
        rgba[i * 4 + 3] = 255;
    }
    font_texture_ = CreateTexture(font_.GetWidth(), font_.GetHeight(), rgba.data(), false, false, "font_atlas");

    return white_texture_.IsValid() && particle_texture_.IsValid() && font_texture_.IsValid();
}

// ============================================================================
// Dynamic buffers & deferred destruction
// ============================================================================

bool SceneRenderer::EnsureCapacity(DynamicBuffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage) {
    if (size <= buffer.capacity && buffer.buffer != VK_NULL_HANDLE) {
        return true;
    }

    // The frame owning this buffer has already been waited on, so it can be replaced immediately
    DestroyDynamicBuffer(buffer);

    VkDeviceSize capacity = 4096;
    while (capacity < size) capacity *= 2;

    if (!device_->CreateBuffer(capacity, usage,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               buffer.buffer, buffer.memory)) {
        return false;
    }
    if (vkMapMemory(device_->GetDevice(), buffer.memory, 0, capacity, 0, &buffer.mapped) != VK_SUCCESS) {
        DestroyDynamicBuffer(buffer);
        return false;
    }
    buffer.capacity = capacity;
    return true;
}

void SceneRenderer::DestroyDynamicBuffer(DynamicBuffer& buffer) {
    if (buffer.mapped) {
        vkUnmapMemory(device_->GetDevice(), buffer.memory);
        buffer.mapped = nullptr;
    }
    device_->DestroyBuffer(buffer.buffer, buffer.memory);
    buffer.capacity = 0;
}

void SceneRenderer::DeferDestroy(Function<void()> destroy) {
    pending_destroys_.push_back({frame_counter_, std::move(destroy)});
}

void SceneRenderer::ProcessPendingDestroys(bool force_all) {
    const uint64 frames_in_flight = device_->GetMaxFramesInFlight();
    auto it = pending_destroys_.begin();
    while (it != pending_destroys_.end()) {
        if (force_all || frame_counter_ >= it->frame + frames_in_flight + 1) {
            it->destroy();
            it = pending_destroys_.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// Meshes
// ============================================================================

MeshHandle SceneRenderer::CreateMesh(const MeshData& data, const String& name) {
    if (!device_) return MeshHandle();
    if (data.IsEmpty() || data.indices.size() % 3 != 0) {
        DRX_LOG_ERROR("Mesh '{}' is empty or not a triangle list", name);
        return MeshHandle();
    }
    for (uint32 index : data.indices) {
        if (index >= data.vertices.size()) {
            DRX_LOG_ERROR("Mesh '{}' has out-of-range index {}", name, index);
            return MeshHandle();
        }
    }

    GpuMesh mesh;
    const VkDeviceSize vertex_size = sizeof(Vertex) * data.vertices.size();
    const VkDeviceSize index_size = sizeof(uint32) * data.indices.size();

    const bool ok =
        device_->CreateBuffer(vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.vertex_buffer, mesh.vertex_memory) &&
        device_->CreateBuffer(index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.index_buffer, mesh.index_memory) &&
        device_->UploadBuffer(mesh.vertex_buffer, data.vertices.data(), vertex_size) &&
        device_->UploadBuffer(mesh.index_buffer, data.indices.data(), index_size);

    if (!ok) {
        device_->DestroyBuffer(mesh.vertex_buffer, mesh.vertex_memory);
        device_->DestroyBuffer(mesh.index_buffer, mesh.index_memory);
        DRX_LOG_ERROR("Failed to upload mesh '{}'", name);
        return MeshHandle();
    }

    mesh.index_count = static_cast<uint32>(data.indices.size());
    mesh.bounds = data.ComputeBounds();
    mesh.name = name;
    mesh.alive = true;
    return MeshHandle(StoreMesh(std::move(mesh)));
}

uint32 SceneRenderer::StoreMesh(GpuMesh&& mesh) {
    uint32 id;
    if (!free_meshes_.empty()) {
        id = free_meshes_.back();
        free_meshes_.pop_back();
        meshes_[id] = std::move(mesh);
    } else {
        id = static_cast<uint32>(meshes_.size());
        meshes_.push_back(std::move(mesh));
    }
    return id;
}

MeshHandle SceneRenderer::CreateDynamicMesh(const MeshData& data, const String& name) {
    if (!device_) return MeshHandle();
    if (data.IsEmpty() || data.indices.size() % 3 != 0) {
        DRX_LOG_ERROR("Dynamic mesh '{}' is empty or not a triangle list", name);
        return MeshHandle();
    }
    for (uint32 index : data.indices) {
        if (index >= data.vertices.size()) {
            DRX_LOG_ERROR("Dynamic mesh '{}' has out-of-range index {}", name, index);
            return MeshHandle();
        }
    }

    GpuMesh mesh;
    const VkDeviceSize vertex_size = sizeof(Vertex) * data.vertices.size();
    const VkDeviceSize index_size = sizeof(uint32) * data.indices.size();
    bool ok = device_->CreateBuffer(index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.index_buffer, mesh.index_memory) &&
              device_->UploadBuffer(mesh.index_buffer, data.indices.data(), index_size);
    const uint32 frames = std::max(1u, device_->GetMaxFramesInFlight());
    for (uint32 i = 0; ok && i < frames; ++i) {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        ok = device_->CreateBuffer(vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer,
                                   memory) &&
             vkMapMemory(device_->GetDevice(), memory, 0, vertex_size, 0, &mapped) == VK_SUCCESS;
        if (buffer != VK_NULL_HANDLE) {
            mesh.frame_buffers.push_back(buffer);
            mesh.frame_memory.push_back(memory);
            mesh.frame_mapped.push_back(mapped);
        }
        if (ok) std::memcpy(mapped, data.vertices.data(), static_cast<size_t>(vertex_size));
    }
    if (!ok) {
        device_->DestroyBuffer(mesh.index_buffer, mesh.index_memory);
        for (size_t i = 0; i < mesh.frame_buffers.size(); ++i) device_->DestroyBuffer(mesh.frame_buffers[i], mesh.frame_memory[i]);
        DRX_LOG_ERROR("Failed to create dynamic mesh '{}'", name);
        return MeshHandle();
    }

    mesh.index_count = static_cast<uint32>(data.indices.size());
    mesh.vertex_count = static_cast<uint32>(data.vertices.size());
    mesh.bounds = data.ComputeBounds();
    mesh.name = name;
    mesh.alive = true;
    return MeshHandle(StoreMesh(std::move(mesh)));
}

bool SceneRenderer::UpdateDynamicMesh(MeshHandle handle, const Array<Vertex>& vertices) {
    if (!IsMeshValid(handle)) return false;
    GpuMesh& mesh = meshes_[handle.GetId()];
    if (mesh.frame_buffers.empty() || vertices.size() != mesh.vertex_count) return false;
    const uint32 frame = device_->GetCurrentFrameIndex() % static_cast<uint32>(mesh.frame_mapped.size());
    std::memcpy(mesh.frame_mapped[frame], vertices.data(), sizeof(Vertex) * vertices.size());
    AABB bounds;
    for (const Vertex& v : vertices) bounds.Expand(v.position);
    mesh.bounds = bounds;
    return true;
}

VkBuffer SceneRenderer::GetVertexBuffer(const GpuMesh& mesh) const {
    if (mesh.frame_buffers.empty()) return mesh.vertex_buffer;
    return mesh.frame_buffers[device_->GetCurrentFrameIndex() % static_cast<uint32>(mesh.frame_buffers.size())];
}

void SceneRenderer::DestroyMesh(MeshHandle handle) {
    if (!device_ || !IsMeshValid(handle)) return;

    const uint32 id = handle.GetId();
    GpuMesh mesh = std::move(meshes_[id]);
    meshes_[id] = GpuMesh{};

    HAL::GraphicsDevice* device = device_;
    DeferDestroy([device, mesh]() mutable {
        device->DestroyBuffer(mesh.vertex_buffer, mesh.vertex_memory);
        device->DestroyBuffer(mesh.index_buffer, mesh.index_memory);
        for (size_t i = 0; i < mesh.frame_buffers.size(); ++i) device->DestroyBuffer(mesh.frame_buffers[i], mesh.frame_memory[i]);
    });
    // The slot may be reused once the deferred destroy has run; reserving it now is safe
    // because the GPU objects were moved out of the slot.
    free_meshes_.push_back(id);
}

bool SceneRenderer::IsMeshValid(MeshHandle handle) const {
    return handle.IsValid() && handle.GetId() > 0 && handle.GetId() < meshes_.size() && meshes_[handle.GetId()].alive;
}

AABB SceneRenderer::GetMeshBounds(MeshHandle handle) const {
    return IsMeshValid(handle) ? meshes_[handle.GetId()].bounds : AABB{};
}

const SceneRenderer::GpuMesh* SceneRenderer::GetMesh(uint32 id) const {
    return (id > 0 && id < meshes_.size() && meshes_[id].alive) ? &meshes_[id] : nullptr;
}

// ============================================================================
// Textures
// ============================================================================

TextureHandle SceneRenderer::CreateTexture(uint32 width, uint32 height, const uint8* rgba_pixels, bool srgb,
                                           bool generate_mips, const String& name) {
    if (!device_ || !rgba_pixels || width == 0 || height == 0) {
        DRX_LOG_ERROR("Invalid texture data for '{}'", name);
        return TextureHandle();
    }

    VkDevice vk = device_->GetDevice();
    const VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    // Mipmaps are generated with linear blits; fall back to a single level if unsupported
    uint32 mip_levels = 1;
    if (generate_mips) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(device_->GetPhysicalDevice(), format, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) {
            mip_levels = static_cast<uint32>(std::floor(std::log2(static_cast<float32>(std::max(width, height))))) + 1;
        }
    }

    GpuTexture texture;
    texture.width = width;
    texture.height = height;
    texture.name = name;

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = mip_levels;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!device_->CreateImage(image_info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture.image, texture.memory)) {
        return TextureHandle();
    }

    const VkDeviceSize byte_size = static_cast<VkDeviceSize>(width) * height * 4;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    if (!device_->CreateBuffer(byte_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               staging, staging_memory) ||
        vkMapMemory(vk, staging_memory, 0, byte_size, 0, &mapped) != VK_SUCCESS) {
        device_->DestroyBuffer(staging, staging_memory);
        device_->DestroyImage(texture.image, texture.memory);
        return TextureHandle();
    }
    std::memcpy(mapped, rgba_pixels, static_cast<size_t>(byte_size));
    vkUnmapMemory(vk, staging_memory);

    VkCommandBuffer cmd = device_->BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = mip_levels;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cmd, staging, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Downsample each level from the previous one
    int32 mip_width = static_cast<int32>(width);
    int32 mip_height = static_cast<int32>(height);
    barrier.subresourceRange.levelCount = 1;
    for (uint32 level = 1; level < mip_levels; ++level) {
        barrier.subresourceRange.baseMipLevel = level - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        const int32 next_width = std::max(1, mip_width / 2);
        const int32 next_height = std::max(1, mip_height / 2);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
        blit.srcOffsets[1] = {mip_width, mip_height, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        blit.dstOffsets[1] = {next_width, next_height, 1};
        vkCmdBlitImage(cmd, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        mip_width = next_width;
        mip_height = next_height;
    }

    barrier.subresourceRange.baseMipLevel = mip_levels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    device_->EndSingleTimeCommands(cmd);
    device_->DestroyBuffer(staging, staging_memory);

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = texture.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = mip_levels;
    view_info.subresourceRange.layerCount = 1;

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = descriptor_pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &texture_layout_;

    if (vkCreateImageView(vk, &view_info, nullptr, &texture.view) != VK_SUCCESS ||
        vkAllocateDescriptorSets(vk, &alloc, &texture.descriptor) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create view/descriptor for texture '{}' (limit {} textures)", name,
                      config_.max_textures);
        if (texture.view != VK_NULL_HANDLE) vkDestroyImageView(vk, texture.view, nullptr);
        device_->DestroyImage(texture.image, texture.memory);
        return TextureHandle();
    }

    VkDescriptorImageInfo image_desc{linear_sampler_, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = texture.descriptor;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_desc;
    vkUpdateDescriptorSets(vk, 1, &write, 0, nullptr);

    texture.alive = true;
    uint32 id;
    if (!free_textures_.empty()) {
        id = free_textures_.back();
        free_textures_.pop_back();
        textures_[id] = std::move(texture);
    } else {
        id = static_cast<uint32>(textures_.size());
        textures_.push_back(std::move(texture));
    }
    return TextureHandle(id);
}

TextureHandle SceneRenderer::LoadTexture(const String& path, bool srgb) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        DRX_LOG_ERROR("Failed to load texture '{}': {}", path, stbi_failure_reason());
        return TextureHandle();
    }

    TextureHandle handle = CreateTexture(static_cast<uint32>(width), static_cast<uint32>(height), pixels, srgb, true, path);
    stbi_image_free(pixels);
    return handle;
}

void SceneRenderer::DestroyTexture(TextureHandle handle) {
    if (!device_ || !handle.IsValid()) return;
    const uint32 id = handle.GetId();
    if (id == 0 || id >= textures_.size() || !textures_[id].alive) return;
    if (handle == white_texture_ || handle == particle_texture_ || handle == font_texture_) {
        DRX_LOG_WARNING("Built-in textures cannot be destroyed");
        return;
    }

    GpuTexture texture = std::move(textures_[id]);
    textures_[id] = GpuTexture{};

    HAL::GraphicsDevice* device = device_;
    VkDescriptorPool pool = descriptor_pool_;
    DeferDestroy([device, pool, texture]() mutable {
        VkDevice vk = device->GetDevice();
        if (texture.descriptor != VK_NULL_HANDLE) vkFreeDescriptorSets(vk, pool, 1, &texture.descriptor);
        if (texture.view != VK_NULL_HANDLE) vkDestroyImageView(vk, texture.view, nullptr);
        device->DestroyImage(texture.image, texture.memory);
    });
    free_textures_.push_back(id);
}

VkDescriptorSet SceneRenderer::GetTextureDescriptor(uint32 texture_id) const {
    if (texture_id > 0 && texture_id < textures_.size() && textures_[texture_id].alive) {
        return textures_[texture_id].descriptor;
    }
    return textures_[white_texture_.GetId()].descriptor;
}

// ============================================================================
// Frame
// ============================================================================

void SceneRenderer::BeginFrame(const CameraParams& camera, const EnvironmentSettings& environment) {
    if (!device_) return;

    if (swap_chain_generation_ != device_->GetSwapChainGeneration()) {
        RecreateSwapChainResources();
    }

    ProcessPendingDestroys(false);

    camera_ = camera;
    environment_ = environment;
    environment_.sun_direction = environment.sun_direction.LengthSquared() > 1e-8f
        ? environment.sun_direction.Normalized() : Math::Vector3::Up;
    light_view_projection_ = ComputeLightViewProjection();

    opaque_items_.clear();
    transparent_items_.clear();
    particle_vertices_.clear();
    particle_batches_.clear();
    line_vertices_.clear();
    ui_vertices_.clear();
    ui_batches_.clear();
    stats_ = RenderStats{};
    frame_begun_ = true;
}

void SceneRenderer::DrawMesh(MeshHandle mesh, const Math::Matrix4& transform, const Material& material) {
    if (!frame_begun_ || !IsMeshValid(mesh)) return;
    ++stats_.meshes_submitted;

    DrawItem item{mesh.GetId(), transform, material};
    if (material.color.w < 0.999f) {
        transparent_items_.push_back(item);
    } else {
        opaque_items_.push_back(item);
    }
}

Math::Matrix4 SceneRenderer::ComputeLightViewProjection() const {
    const float32 radius = std::max(1.0f, environment_.shadow_distance);
    const Math::Vector3 sun = environment_.sun_direction;

    // Camera forward from the view matrix (third row is -forward)
    const Math::Vector3 forward(-camera_.view[0][2], -camera_.view[1][2], -camera_.view[2][2]);
    const Math::Vector3 center = camera_.position + forward * (radius * 0.5f);

    const Math::Vector3 up = std::abs(sun.y) > 0.99f ? Math::Vector3(0, 0, 1) : Math::Vector3::Up;
    const Math::Matrix4 light_view = Math::Matrix4::LookAt(Math::Vector3::Zero, -sun, up);

    // Snap the shadow window to whole texels so the shadow does not shimmer when the camera moves
    Math::Vector3 center_ls = light_view.TransformPoint(center);
    const float32 texel = (2.0f * radius) / static_cast<float32>(config_.shadow_map_size);
    center_ls.x = std::floor(center_ls.x / texel) * texel;
    center_ls.y = std::floor(center_ls.y / texel) * texel;

    // Extend towards the sun so tall casters outside the view still cast shadows in
    const float32 near_distance = -center_ls.z - radius * 3.0f;
    const float32 far_distance = -center_ls.z + radius;
    const Math::Matrix4 projection = Math::Matrix4::OrthographicVulkan(
        center_ls.x - radius, center_ls.x + radius, center_ls.y - radius, center_ls.y + radius,
        near_distance, far_distance);

    return projection * light_view;
}

void SceneRenderer::UpdateFrameUniforms(FrameResources& frame) {
    FrameUniforms u{};
    const Math::Matrix4 view_projection = camera_.projection * camera_.view;
    u.view = camera_.view;
    u.projection = camera_.projection;
    u.view_projection = view_projection;
    u.inverse_view_projection = view_projection.Inverse();
    u.light_view_projection = light_view_projection_;
    const float64 seconds = Platform::GetTimeSeconds() - start_time_;
    u.camera_position = ToVec4(camera_.position, static_cast<float32>(std::fmod(seconds, 3600.0)));
    u.sun_direction = ToVec4(environment_.sun_direction, environment_.sun_intensity);
    u.sun_color = ToVec4(environment_.sun_color, std::clamp(environment_.shadow_strength, 0.0f, 1.0f));
    u.sky_zenith = ToVec4(environment_.sky_zenith, 1.0f);
    u.sky_horizon = ToVec4(environment_.sky_horizon, 1.0f);
    u.ground_color = ToVec4(environment_.ground_color, 1.0f);
    u.ambient_sky = ToVec4(environment_.ambient_sky, environment_.ambient_intensity);
    u.ambient_ground = ToVec4(environment_.ambient_ground, 1.0f);
    u.fog_color = ToVec4(environment_.fog_color, std::max(0.0f, environment_.fog_density));
    u.shadow_params = Math::Vector4(1.0f / static_cast<float32>(config_.shadow_map_size), environment_.shadow_bias,
                                    environment_.shadows_enabled ? 1.0f : 0.0f, 0.0f);
    u.exposure = Math::Vector4(environment_.exposure, 0.0f, 0.0f, 0.0f);

    std::memcpy(frame.uniforms.mapped, &u, sizeof(u));
}

void SceneRenderer::Render() {
    if (!device_ || !frame_begun_) return;
    frame_begun_ = false;

    if (device_->GetCurrentImageIndex() >= framebuffers_.size()) {
        DRX_LOG_ERROR("Swap chain image index out of range");
        return;
    }

    FrameResources& frame = frames_[device_->GetCurrentFrameIndex()];
    UpdateFrameUniforms(frame);

    // Upload dynamic geometry for this frame
    bool geometry_ok = true;
    if (!particle_vertices_.empty()) {
        const VkDeviceSize size = sizeof(ParticleVertex) * particle_vertices_.size();
        geometry_ok &= EnsureCapacity(frame.particles, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        if (frame.particles.mapped) std::memcpy(frame.particles.mapped, particle_vertices_.data(), static_cast<size_t>(size));
    }
    if (!line_vertices_.empty()) {
        const VkDeviceSize size = sizeof(LineVertex) * line_vertices_.size();
        geometry_ok &= EnsureCapacity(frame.lines, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        if (frame.lines.mapped) std::memcpy(frame.lines.mapped, line_vertices_.data(), static_cast<size_t>(size));
    }
    if (!ui_vertices_.empty()) {
        const VkDeviceSize size = sizeof(UIVertex) * ui_vertices_.size();
        geometry_ok &= EnsureCapacity(frame.ui, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        if (frame.ui.mapped) std::memcpy(frame.ui.mapped, ui_vertices_.data(), static_cast<size_t>(size));
    }
    if (!geometry_ok) {
        DRX_LOG_ERROR("Failed to allocate dynamic geometry buffers; skipping overlays this frame");
        particle_batches_.clear();
        line_vertices_.clear();
        ui_batches_.clear();
    }

    VkCommandBuffer cmd = device_->GetCurrentCommandBuffer();
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to begin command buffer");
        return;
    }

    RecordShadowPass(cmd, frame);
    RecordMainPass(cmd, frame);
    if (screenshot_.requested) {
        RecordScreenshotCopy(cmd);
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to record frame command buffer");
    }
    last_stats_ = stats_;
    ++frame_counter_;
}

void SceneRenderer::RecordShadowPass(VkCommandBuffer cmd, FrameResources& frame) {
    const uint32 size = config_.shadow_map_size;

    VkClearValue clear{};
    clear.depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = shadow_pass_;
    pass.framebuffer = shadow_framebuffer_;
    pass.renderArea.extent = {size, size};
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;

    vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);

    // The pass always runs so the shadow map is in a sampleable layout even with shadows off
    if (environment_.shadows_enabled && !opaque_items_.empty()) {
        VkViewport viewport{0.0f, 0.0f, static_cast<float32>(size), static_cast<float32>(size), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, {size, size}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &frame.frame_set, 0, nullptr);

        Plane light_frustum[6];
        ExtractFrustum(light_view_projection_, light_frustum);

        uint32 bound_mesh = 0;
        for (const auto& item : opaque_items_) {
            if (!item.material.cast_shadows) continue;
            const GpuMesh* mesh = GetMesh(item.mesh);
            if (!mesh || !IsVisible(light_frustum, mesh->bounds.Transformed(item.transform))) continue;

            if (bound_mesh != item.mesh) {
                VkDeviceSize offset = 0;
                {
                    const VkBuffer vertex_buffer = GetVertexBuffer(*mesh);
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &offset);
                }
                vkCmdBindIndexBuffer(cmd, mesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);
                bound_mesh = item.mesh;
            }

            DrawConstants constants{};
            constants.model = item.transform;
            vkCmdPushConstants(cmd, pipeline_layout_, kPushStages, 0, sizeof(DrawConstants), &constants);
            vkCmdDrawIndexed(cmd, mesh->index_count, 1, 0, 0, 0);
            ++stats_.shadow_casters;
            ++stats_.draw_calls;
        }
    }

    vkCmdEndRenderPass(cmd);
}

void SceneRenderer::RecordMainPass(VkCommandBuffer cmd, FrameResources& frame) {
    const VkExtent2D extent = device_->GetSwapChainExtent();

    VkClearValue clears[2] = {};
    clears[0].color = {{environment_.fog_color.x, environment_.fog_color.y, environment_.fog_color.z, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = main_pass_;
    pass.framebuffer = framebuffers_[device_->GetCurrentImageIndex()];
    pass.renderArea.extent = extent;
    pass.clearValueCount = 2;
    pass.pClearValues = clears;

    vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{0.0f, 0.0f, static_cast<float32>(extent.width), static_cast<float32>(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &frame.frame_set, 0, nullptr);

    // Sky
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sky_pipeline_);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    ++stats_.draw_calls;

    // Opaque meshes, culled and sorted to minimise state changes
    Plane frustum[6];
    ExtractFrustum(camera_.projection * camera_.view, frustum);

    auto draw_items = [&](Array<DrawItem>& items, bool transparent) {
        VkPipeline bound_pipeline = VK_NULL_HANDLE;
        uint32 bound_mesh = 0;
        uint32 bound_texture = std::numeric_limits<uint32>::max();

        for (const auto& item : items) {
            const GpuMesh* mesh = GetMesh(item.mesh);
            if (!mesh) continue;
            if (!IsVisible(frustum, mesh->bounds.Transformed(item.transform))) {
                ++stats_.meshes_culled;
                continue;
            }

            VkPipeline pipeline = transparent ? lit_transparent_pipeline_
                                : item.material.double_sided ? lit_double_sided_pipeline_ : lit_pipeline_;
            if (pipeline != bound_pipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                bound_pipeline = pipeline;
            }

            const uint32 texture = item.material.albedo.IsValid() ? item.material.albedo.GetId() : white_texture_.GetId();
            if (texture != bound_texture) {
                VkDescriptorSet set = GetTextureDescriptor(texture);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 1, 1, &set, 0, nullptr);
                bound_texture = texture;
            }

            if (item.mesh != bound_mesh) {
                VkDeviceSize offset = 0;
                {
                    const VkBuffer vertex_buffer = GetVertexBuffer(*mesh);
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &offset);
                }
                vkCmdBindIndexBuffer(cmd, mesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);
                bound_mesh = item.mesh;
            }

            DrawConstants constants{};
            constants.model = item.transform;
            constants.color = item.material.color;
            constants.params = Math::Vector4(item.material.roughness, item.material.metallic,
                                             item.material.emissive_strength, 0.0f);
            constants.emissive = ToVec4(item.material.emissive, 0.0f);
            vkCmdPushConstants(cmd, pipeline_layout_, kPushStages, 0, sizeof(DrawConstants), &constants);
            vkCmdDrawIndexed(cmd, mesh->index_count, 1, 0, 0, 0);

            ++stats_.draw_calls;
            stats_.triangles += mesh->index_count / 3;
        }
    };

    std::sort(opaque_items_.begin(), opaque_items_.end(), [](const DrawItem& a, const DrawItem& b) {
        if (a.material.double_sided != b.material.double_sided) return !a.material.double_sided;
        if (a.material.albedo.GetId() != b.material.albedo.GetId()) return a.material.albedo.GetId() < b.material.albedo.GetId();
        return a.mesh < b.mesh;
    });
    draw_items(opaque_items_, false);

    // Transparent meshes back to front
    const Math::Vector3 eye = camera_.position;
    std::sort(transparent_items_.begin(), transparent_items_.end(), [&eye](const DrawItem& a, const DrawItem& b) {
        const Math::Vector3 pa(a.transform[3][0], a.transform[3][1], a.transform[3][2]);
        const Math::Vector3 pb(b.transform[3][0], b.transform[3][1], b.transform[3][2]);
        return Math::Vector3::DistanceSquared(pa, eye) > Math::Vector3::DistanceSquared(pb, eye);
    });
    draw_items(transparent_items_, true);

    // Particles
    if (!particle_batches_.empty() && frame.particles.buffer != VK_NULL_HANDLE) {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &frame.particles.buffer, &offset);
        for (const auto& batch : particle_batches_) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              batch.additive ? particle_additive_pipeline_ : particle_alpha_pipeline_);
            VkDescriptorSet set = GetTextureDescriptor(batch.texture);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 1, 1, &set, 0, nullptr);
            vkCmdDraw(cmd, batch.vertex_count, 1, batch.first_vertex, 0);
            ++stats_.draw_calls;
        }
    }

    // Debug lines
    if (!line_vertices_.empty() && frame.lines.buffer != VK_NULL_HANDLE) {
        VkDeviceSize offset = 0;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, line_pipeline_);
        vkCmdBindVertexBuffers(cmd, 0, 1, &frame.lines.buffer, &offset);
        vkCmdDraw(cmd, static_cast<uint32>(line_vertices_.size()), 1, 0, 0);
        ++stats_.draw_calls;
    }

    // UI overlay
    if (!ui_batches_.empty() && frame.ui.buffer != VK_NULL_HANDLE) {
        VkDeviceSize offset = 0;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_);
        vkCmdBindVertexBuffers(cmd, 0, 1, &frame.ui.buffer, &offset);

        UIConstants constants{};
        constants.transform = Math::Vector4(2.0f / static_cast<float32>(extent.width),
                                            2.0f / static_cast<float32>(extent.height), -1.0f, -1.0f);

        for (const auto& batch : ui_batches_) {
            const uint32 texture = batch.texture == 0 ? font_texture_.GetId() : batch.texture;
            VkDescriptorSet set = GetTextureDescriptor(texture);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 1, 1, &set, 0, nullptr);
            constants.mode = Math::Vector4(batch.rgba ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
            vkCmdPushConstants(cmd, pipeline_layout_, kPushStages, 0, sizeof(UIConstants), &constants);
            vkCmdDraw(cmd, batch.vertex_count, 1, batch.first_vertex, 0);
            ++stats_.draw_calls;
        }
    }

    vkCmdEndRenderPass(cmd);
}

// ============================================================================
// Screenshots
// ============================================================================

bool SceneRenderer::RequestScreenshot(const String& png_path) {
    if (!device_) return false;
    if (!device_->SwapChainSupportsCopy()) {
        DRX_LOG_ERROR("Screenshots are not supported by this swap chain");
        return false;
    }
    screenshot_.path = png_path;
    screenshot_.requested = true;
    screenshot_.recorded = false;
    return true;
}

void SceneRenderer::RecordScreenshotCopy(VkCommandBuffer cmd) {
    const VkExtent2D extent = device_->GetSwapChainExtent();
    const VkDeviceSize size = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
    if (!EnsureCapacity(screenshot_.buffer, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        screenshot_.requested = false;
        return;
    }

    VkImage image = device_->GetSwapChainImage(device_->GetCurrentImageIndex());

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, screenshot_.buffer.buffer, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    screenshot_.width = extent.width;
    screenshot_.height = extent.height;
    screenshot_.requested = false;
    screenshot_.recorded = true;
}

bool SceneRenderer::FinishScreenshot() {
    if (!device_ || !screenshot_.recorded) return false;
    screenshot_.recorded = false;

    vkDeviceWaitIdle(device_->GetDevice());

    const VkFormat format = device_->GetSwapChainFormat();
    const bool bgr = format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM;
    const size_t pixel_count = static_cast<size_t>(screenshot_.width) * screenshot_.height;

    Array<uint8> pixels(pixel_count * 4);
    std::memcpy(pixels.data(), screenshot_.buffer.mapped, pixels.size());
    for (size_t i = 0; i < pixel_count; ++i) {
        if (bgr) std::swap(pixels[i * 4 + 0], pixels[i * 4 + 2]);
        pixels[i * 4 + 3] = 255;
    }

    const int ok = stbi_write_png(screenshot_.path.c_str(), static_cast<int>(screenshot_.width),
                                  static_cast<int>(screenshot_.height), 4, pixels.data(),
                                  static_cast<int>(screenshot_.width * 4));
    if (!ok) {
        DRX_LOG_ERROR("Failed to write screenshot {}", screenshot_.path);
        return false;
    }
    DRX_LOG_INFO("Screenshot saved: {} ({}x{})", screenshot_.path, screenshot_.width, screenshot_.height);
    return true;
}

} // namespace DRX::Renderer

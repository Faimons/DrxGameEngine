/**
 * DRX Engine - Forward Renderer Implementation
 */

#include "Renderer/Public/Renderer.h"
#include "Core/Public/Log.h"
#include "Core/Public/Platform.h"

#include "Shaders/forward_basic.vert.h"
#include "Shaders/forward_basic.frag.h"

#include <cstddef>
#include <cstring>

namespace DRX::Renderer {

namespace {

// Demo triangle (counter-clockwise when seen from +Z)
const Vertex kTriangleVertices[] = {
    // Position                Normal               Color                TexCoord
    {{ 0.0f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.5f, 0.0f}},
    {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{ 0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
};

constexpr float32 kFieldOfView = 3.14159265358979323846f / 4.0f;
constexpr float32 kNearPlane = 0.1f;
constexpr float32 kFarPlane = 100.0f;

VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, size_t size_bytes) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size_bytes;
    info.pCode = code;

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return module;
}

Math::Matrix4 MakeProjection(VkExtent2D extent) {
    const float32 aspect = extent.height > 0
        ? static_cast<float32>(extent.width) / static_cast<float32>(extent.height)
        : 1.0f;
    return Math::Matrix4::PerspectiveVulkan(kFieldOfView, aspect, kNearPlane, kFarPlane);
}

} // namespace

ForwardRenderer::ForwardRenderer() = default;

ForwardRenderer::~ForwardRenderer() {
    Shutdown();
}

bool ForwardRenderer::Initialize(HAL::GraphicsDevice* device) {
    if (!device) {
        DRX_LOG_ERROR("ForwardRenderer requires a graphics device");
        return false;
    }
    device_ = device;

    camera_position_ = Math::Vector3(0.0f, 0.0f, 2.0f);
    view_matrix_ = Math::Matrix4::LookAt(camera_position_, Math::Vector3::Zero, Math::Vector3::Up);
    projection_matrix_ = MakeProjection(device_->GetSwapChainExtent());
    start_time_ = Platform::GetTimeSeconds();

    const bool ok =
        CreateRenderPass() &&
        CreateFramebuffers() &&
        CreateVertexBuffer() &&
        CreateUniformBuffers() &&
        CreateDescriptorPool() &&
        CreateDescriptorSets() &&
        CreatePipeline();

    if (!ok) {
        DRX_LOG_ERROR("Forward renderer initialization failed");
        Shutdown();
        return false;
    }

    DRX_LOG_INFO("Forward renderer initialized");
    return true;
}

void ForwardRenderer::Shutdown() {
    if (!device_) return;

    VkDevice vk_device = device_->GetDevice();
    vkDeviceWaitIdle(vk_device);

    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(vk_device, pipeline_, nullptr);
    if (pipeline_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(vk_device, pipeline_layout_, nullptr);
    if (descriptor_pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(vk_device, descriptor_pool_, nullptr);
    if (descriptor_set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(vk_device, descriptor_set_layout_, nullptr);

    for (size_t i = 0; i < uniform_buffers_.size(); ++i) {
        if (uniform_buffers_[i] != VK_NULL_HANDLE) vkDestroyBuffer(vk_device, uniform_buffers_[i], nullptr);
        if (uniform_buffers_memory_[i] != VK_NULL_HANDLE) vkFreeMemory(vk_device, uniform_buffers_memory_[i], nullptr);
    }

    if (vertex_buffer_ != VK_NULL_HANDLE) vkDestroyBuffer(vk_device, vertex_buffer_, nullptr);
    if (vertex_buffer_memory_ != VK_NULL_HANDLE) vkFreeMemory(vk_device, vertex_buffer_memory_, nullptr);

    CleanupFramebuffers();
    if (render_pass_ != VK_NULL_HANDLE) vkDestroyRenderPass(vk_device, render_pass_, nullptr);

    pipeline_ = VK_NULL_HANDLE;
    pipeline_layout_ = VK_NULL_HANDLE;
    descriptor_pool_ = VK_NULL_HANDLE;
    descriptor_set_layout_ = VK_NULL_HANDLE;
    descriptor_sets_.clear();
    uniform_buffers_.clear();
    uniform_buffers_memory_.clear();
    uniform_buffers_mapped_.clear();
    vertex_buffer_ = VK_NULL_HANDLE;
    vertex_buffer_memory_ = VK_NULL_HANDLE;
    render_pass_ = VK_NULL_HANDLE;
    frame_active_ = false;

    device_ = nullptr;
    DRX_LOG_INFO("Forward renderer shutdown");
}

bool ForwardRenderer::CreateRenderPass() {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = device_->GetSwapChainFormat();
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &color_attachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    if (vkCreateRenderPass(device_->GetDevice(), &info, nullptr, &render_pass_) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create render pass");
        return false;
    }
    return true;
}

bool ForwardRenderer::CreateFramebuffers() {
    const auto& image_views = device_->GetSwapChainImageViews();
    const VkExtent2D extent = device_->GetSwapChainExtent();
    framebuffers_.assign(image_views.size(), VK_NULL_HANDLE);
    swap_chain_generation_ = device_->GetSwapChainGeneration();

    for (size_t i = 0; i < image_views.size(); ++i) {
        VkImageView attachments[] = {image_views[i]};

        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = render_pass_;
        info.attachmentCount = 1;
        info.pAttachments = attachments;
        info.width = extent.width;
        info.height = extent.height;
        info.layers = 1;

        if (vkCreateFramebuffer(device_->GetDevice(), &info, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            DRX_LOG_ERROR("Failed to create framebuffer {}", i);
            return false;
        }
    }
    return true;
}

bool ForwardRenderer::CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                       VkBuffer& buffer, VkDeviceMemory& memory) {
    VkDevice vk_device = device_->GetDevice();

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vk_device, &buffer_info, nullptr, &buffer) != VK_SUCCESS) {
        buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(vk_device, buffer, &requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = device_->FindMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(vk_device, &alloc_info, nullptr, &memory) != VK_SUCCESS) {
        memory = VK_NULL_HANDLE;
        return false;
    }

    return vkBindBufferMemory(vk_device, buffer, memory, 0) == VK_SUCCESS;
}

bool ForwardRenderer::CreateVertexBuffer() {
    const VkDeviceSize buffer_size = sizeof(kTriangleVertices);
    if (!CreateHostBuffer(buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer_, vertex_buffer_memory_)) {
        DRX_LOG_ERROR("Failed to create vertex buffer");
        return false;
    }

    void* data = nullptr;
    if (vkMapMemory(device_->GetDevice(), vertex_buffer_memory_, 0, buffer_size, 0, &data) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to map vertex buffer");
        return false;
    }
    std::memcpy(data, kTriangleVertices, static_cast<size_t>(buffer_size));
    vkUnmapMemory(device_->GetDevice(), vertex_buffer_memory_);
    return true;
}

bool ForwardRenderer::CreateUniformBuffers() {
    const VkDeviceSize buffer_size = sizeof(UniformBufferObject);
    const uint32 frame_count = device_->GetMaxFramesInFlight();

    uniform_buffers_.assign(frame_count, VK_NULL_HANDLE);
    uniform_buffers_memory_.assign(frame_count, VK_NULL_HANDLE);
    uniform_buffers_mapped_.assign(frame_count, nullptr);

    for (uint32 i = 0; i < frame_count; ++i) {
        if (!CreateHostBuffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              uniform_buffers_[i], uniform_buffers_memory_[i])) {
            DRX_LOG_ERROR("Failed to create uniform buffer {}", i);
            return false;
        }
        if (vkMapMemory(device_->GetDevice(), uniform_buffers_memory_[i], 0, buffer_size, 0,
                        &uniform_buffers_mapped_[i]) != VK_SUCCESS) {
            DRX_LOG_ERROR("Failed to map uniform buffer {}", i);
            return false;
        }
    }
    return true;
}

bool ForwardRenderer::CreateDescriptorPool() {
    const uint32 frame_count = device_->GetMaxFramesInFlight();

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_size.descriptorCount = frame_count;

    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.poolSizeCount = 1;
    info.pPoolSizes = &pool_size;
    info.maxSets = frame_count;

    if (vkCreateDescriptorPool(device_->GetDevice(), &info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create descriptor pool");
        return false;
    }
    return true;
}

bool ForwardRenderer::CreateDescriptorSets() {
    VkDescriptorSetLayoutBinding ubo_binding{};
    ubo_binding.binding = 0;
    ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_binding.descriptorCount = 1;
    ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &ubo_binding;

    if (vkCreateDescriptorSetLayout(device_->GetDevice(), &layout_info, nullptr, &descriptor_set_layout_) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create descriptor set layout");
        return false;
    }

    const uint32 frame_count = device_->GetMaxFramesInFlight();
    Array<VkDescriptorSetLayout> layouts(frame_count, descriptor_set_layout_);

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = descriptor_pool_;
    alloc_info.descriptorSetCount = frame_count;
    alloc_info.pSetLayouts = layouts.data();

    descriptor_sets_.assign(frame_count, VK_NULL_HANDLE);
    if (vkAllocateDescriptorSets(device_->GetDevice(), &alloc_info, descriptor_sets_.data()) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to allocate descriptor sets");
        return false;
    }

    for (uint32 i = 0; i < frame_count; ++i) {
        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = uniform_buffers_[i];
        buffer_info.offset = 0;
        buffer_info.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptor_sets_[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &buffer_info;

        vkUpdateDescriptorSets(device_->GetDevice(), 1, &write, 0, nullptr);
    }
    return true;
}

bool ForwardRenderer::CreatePipeline() {
    VkDevice vk_device = device_->GetDevice();

    VkShaderModule vert_module = CreateShaderModule(vk_device, kShader_forward_basic_vert, sizeof(kShader_forward_basic_vert));
    VkShaderModule frag_module = CreateShaderModule(vk_device, kShader_forward_basic_frag, sizeof(kShader_forward_basic_frag));
    if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
        DRX_LOG_ERROR("Failed to create forward shader modules");
        if (vert_module != VK_NULL_HANDLE) vkDestroyShaderModule(vk_device, vert_module, nullptr);
        if (frag_module != VK_NULL_HANDLE) vkDestroyShaderModule(vk_device, frag_module, nullptr);
        return false;
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

    const auto binding = Vertex::GetBindingDescription();
    const auto attributes = Vertex::GetAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = static_cast<uint32>(attributes.size());
    vertex_input.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport and scissor are dynamic so swap chain resizes need no pipeline rebuild
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // the demo triangle spins and is seen from both sides
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &descriptor_set_layout_;

    bool ok = vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &pipeline_layout_) == VK_SUCCESS;
    if (ok) {
        VkGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = stages;
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pColorBlendState = &color_blending;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = pipeline_layout_;
        pipeline_info.renderPass = render_pass_;
        pipeline_info.subpass = 0;

        ok = vkCreateGraphicsPipelines(vk_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_) == VK_SUCCESS;
    } else {
        pipeline_layout_ = VK_NULL_HANDLE;
    }

    vkDestroyShaderModule(vk_device, vert_module, nullptr);
    vkDestroyShaderModule(vk_device, frag_module, nullptr);

    if (!ok) {
        pipeline_ = VK_NULL_HANDLE;
        DRX_LOG_ERROR("Failed to create forward pipeline");
    }
    return ok;
}

void ForwardRenderer::CleanupFramebuffers() {
    if (!device_) return;

    for (auto framebuffer : framebuffers_) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_->GetDevice(), framebuffer, nullptr);
        }
    }
    framebuffers_.clear();
}

void ForwardRenderer::BeginFrame() {
    frame_active_ = false;
    if (!device_ || pipeline_ == VK_NULL_HANDLE) return;

    // The device recreates its swap chain on its own; rebuild framebuffers to match
    if (swap_chain_generation_ != device_->GetSwapChainGeneration()) {
        OnResize(device_->GetSwapChainExtent().width, device_->GetSwapChainExtent().height);
    }

    const uint32 image_index = device_->GetCurrentImageIndex();
    if (image_index >= framebuffers_.size()) {
        DRX_LOG_ERROR("Swap chain image index {} out of range", image_index);
        return;
    }

    time_ = static_cast<float32>(Platform::GetTimeSeconds() - start_time_);
    UpdateUniformBuffer(device_->GetCurrentFrameIndex());

    VkCommandBuffer cmd = device_->GetCurrentCommandBuffer();

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to begin command buffer");
        return;
    }

    const VkExtent2D extent = device_->GetSwapChainExtent();

    VkRenderPassBeginInfo rp_info{};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_info.renderPass = render_pass_;
    rp_info.framebuffer = framebuffers_[image_index];
    rp_info.renderArea.offset = {0, 0};
    rp_info.renderArea.extent = extent;

    VkClearValue clear_color{};
    clear_color.color = {{0.1f, 0.1f, 0.15f, 1.0f}};
    rp_info.clearValueCount = 1;
    rp_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport viewport{};
    viewport.width = static_cast<float32>(extent.width);
    viewport.height = static_cast<float32>(extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_,
        0, 1, &descriptor_sets_[device_->GetCurrentFrameIndex()], 0, nullptr);

    frame_active_ = true;
}

void ForwardRenderer::EndFrame() {
    if (!frame_active_) return;

    VkCommandBuffer cmd = device_->GetCurrentCommandBuffer();
    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to record command buffer");
    }
    frame_active_ = false;
}

void ForwardRenderer::DrawTriangle() {
    if (!frame_active_) return;

    VkCommandBuffer cmd = device_->GetCurrentCommandBuffer();
    VkBuffer buffers[] = {vertex_buffer_};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void ForwardRenderer::SetCamera(const Math::Matrix4& view, const Math::Matrix4& projection) {
    view_matrix_ = view;
    projection_matrix_ = projection;
}

void ForwardRenderer::SetCameraPosition(const Math::Vector3& pos) {
    camera_position_ = pos;
}

void ForwardRenderer::UpdateUniformBuffer(uint32 frame_index) {
    if (frame_index >= uniform_buffers_mapped_.size() || !uniform_buffers_mapped_[frame_index]) return;

    UniformBufferObject ubo{};
    ubo.model = Math::Matrix4::RotationY(time_);
    ubo.view = view_matrix_;
    ubo.projection = projection_matrix_;
    ubo.camera_pos = camera_position_;
    ubo.time = time_;

    std::memcpy(uniform_buffers_mapped_[frame_index], &ubo, sizeof(ubo));
}

void ForwardRenderer::OnResize(uint32 width, uint32 height) {
    if (!device_ || width == 0 || height == 0) return;

    projection_matrix_ = MakeProjection(device_->GetSwapChainExtent());

    vkDeviceWaitIdle(device_->GetDevice());
    CleanupFramebuffers();
    if (!CreateFramebuffers()) {
        DRX_LOG_ERROR("Failed to recreate framebuffers after resize");
    }
}

} // namespace DRX::Renderer

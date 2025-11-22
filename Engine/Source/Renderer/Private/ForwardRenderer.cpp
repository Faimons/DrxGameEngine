/**
 * DRX Engine - Forward Renderer Implementation
 */

#include "Renderer/Public/Renderer.h"
#include "Core/Public/Log.h"
#include "Core/Public/Platform.h"

#include <cstring>

namespace DRX::Renderer {

// Triangle vertices
static const Array<Vertex> kTriangleVertices = {
    // Position                    Normal                Color               TexCoord
    {{ 0.0f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.5f, 0.0f}},
    {{ 0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}
};

// Embedded SPIR-V shaders (simple passthrough)
// These would normally be loaded from files
static const uint32 kVertexShaderSPIRV[] = {
    // Basic vertex shader SPIR-V binary
    0x07230203, 0x00010000, 0x000d000a, 0x00000036,
    0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x000b000f, 0x00000000, 0x00000004, 0x6e69616d,
    0x00000000, 0x0000000d, 0x00000012, 0x0000001c,
    0x0000001d, 0x00000024, 0x00000025, 0x00030003,
    0x00000002, 0x000001c2, 0x00040005, 0x00000004,
    0x6e69616d, 0x00000000, 0x00060005, 0x0000000b,
    0x505f6c67, 0x65567265, 0x78657472, 0x00000000,
    0x00060006, 0x0000000b, 0x00000000, 0x505f6c67,
    0x7469736f, 0x006e6f69, 0x00030005, 0x0000000d,
    0x00000000, 0x00050005, 0x00000012, 0x6f506e69,
    0x69746973, 0x00006e6f, 0x00050005, 0x0000001c,
    0x43747561, 0x726f6c6f, 0x00000000, 0x00050005,
    0x0000001d, 0x6f436e69, 0x00726f6c, 0x00050005,
    0x00000024, 0x4374756f, 0x64726f6f, 0x00000000,
    0x00050005, 0x00000025, 0x65546e69, 0x6f6f4378,
    0x00006472, 0x00050048, 0x0000000b, 0x00000000,
    0x0000000b, 0x00000000, 0x00030047, 0x0000000b,
    0x00000002, 0x00040047, 0x00000012, 0x0000001e,
    0x00000000, 0x00040047, 0x0000001c, 0x0000001e,
    0x00000000, 0x00040047, 0x0000001d, 0x0000001e,
    0x00000002, 0x00040047, 0x00000024, 0x0000001e,
    0x00000001, 0x00040047, 0x00000025, 0x0000001e,
    0x00000003, 0x00020013, 0x00000002, 0x00030021,
    0x00000003, 0x00000002, 0x00030016, 0x00000006,
    0x00000020, 0x00040017, 0x00000007, 0x00000006,
    0x00000004, 0x0003001e, 0x0000000b, 0x00000007,
    0x00040020, 0x0000000c, 0x00000003, 0x0000000b,
    0x0004003b, 0x0000000c, 0x0000000d, 0x00000003,
    0x00040015, 0x0000000e, 0x00000020, 0x00000001,
    0x0004002b, 0x0000000e, 0x0000000f, 0x00000000,
    0x00040017, 0x00000010, 0x00000006, 0x00000003,
    0x00040020, 0x00000011, 0x00000001, 0x00000010,
    0x0004003b, 0x00000011, 0x00000012, 0x00000001,
    0x0004002b, 0x00000006, 0x00000014, 0x00000000,
    0x0004002b, 0x00000006, 0x00000015, 0x3f800000,
    0x00040020, 0x00000019, 0x00000003, 0x00000007,
    0x00040020, 0x0000001b, 0x00000003, 0x00000010,
    0x0004003b, 0x0000001b, 0x0000001c, 0x00000003,
    0x0004003b, 0x00000011, 0x0000001d, 0x00000001,
    0x00040017, 0x00000022, 0x00000006, 0x00000002,
    0x00040020, 0x00000023, 0x00000003, 0x00000022,
    0x0004003b, 0x00000023, 0x00000024, 0x00000003,
    0x00040020, 0x00000026, 0x00000001, 0x00000022,
    0x0004003b, 0x00000026, 0x00000025, 0x00000001,
    0x00050036, 0x00000002, 0x00000004, 0x00000000,
    0x00000003, 0x000200f8, 0x00000005, 0x0004003d,
    0x00000010, 0x00000013, 0x00000012, 0x00050051,
    0x00000006, 0x00000016, 0x00000013, 0x00000000,
    0x00050051, 0x00000006, 0x00000017, 0x00000013,
    0x00000001, 0x00050051, 0x00000006, 0x00000018,
    0x00000013, 0x00000002, 0x00070050, 0x00000007,
    0x0000001a, 0x00000016, 0x00000017, 0x00000018,
    0x00000015, 0x00050041, 0x00000019, 0x0000001e,
    0x0000000d, 0x0000000f, 0x0003003e, 0x0000001e,
    0x0000001a, 0x0004003d, 0x00000010, 0x0000001f,
    0x0000001d, 0x0003003e, 0x0000001c, 0x0000001f,
    0x0004003d, 0x00000022, 0x00000027, 0x00000025,
    0x0003003e, 0x00000024, 0x00000027, 0x000100fd,
    0x00010038
};

static const uint32 kFragmentShaderSPIRV[] = {
    // Basic fragment shader SPIR-V binary
    0x07230203, 0x00010000, 0x000d000a, 0x00000013,
    0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0007000f, 0x00000004, 0x00000004, 0x6e69616d,
    0x00000000, 0x00000009, 0x0000000c, 0x00030010,
    0x00000004, 0x00000007, 0x00030003, 0x00000002,
    0x000001c2, 0x00040005, 0x00000004, 0x6e69616d,
    0x00000000, 0x00050005, 0x00000009, 0x4374756f,
    0x726f6c6f, 0x00000000, 0x00050005, 0x0000000c,
    0x43747561, 0x726f6c6f, 0x00000000, 0x00040047,
    0x00000009, 0x0000001e, 0x00000000, 0x00040047,
    0x0000000c, 0x0000001e, 0x00000000, 0x00020013,
    0x00000002, 0x00030021, 0x00000003, 0x00000002,
    0x00030016, 0x00000006, 0x00000020, 0x00040017,
    0x00000007, 0x00000006, 0x00000004, 0x00040020,
    0x00000008, 0x00000003, 0x00000007, 0x0004003b,
    0x00000008, 0x00000009, 0x00000003, 0x00040017,
    0x0000000a, 0x00000006, 0x00000003, 0x00040020,
    0x0000000b, 0x00000001, 0x0000000a, 0x0004003b,
    0x0000000b, 0x0000000c, 0x00000001, 0x0004002b,
    0x00000006, 0x0000000e, 0x3f800000, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003,
    0x000200f8, 0x00000005, 0x0004003d, 0x0000000a,
    0x0000000d, 0x0000000c, 0x00050051, 0x00000006,
    0x0000000f, 0x0000000d, 0x00000000, 0x00050051,
    0x00000006, 0x00000010, 0x0000000d, 0x00000001,
    0x00050051, 0x00000006, 0x00000011, 0x0000000d,
    0x00000002, 0x00070050, 0x00000007, 0x00000012,
    0x0000000f, 0x00000010, 0x00000011, 0x0000000e,
    0x0003003e, 0x00000009, 0x00000012, 0x000100fd,
    0x00010038
};

ForwardRenderer::ForwardRenderer() {}

ForwardRenderer::~ForwardRenderer() {
    Shutdown();
}

bool ForwardRenderer::Initialize(HAL::GraphicsDevice* device) {
    device_ = device;

    view_matrix_ = Math::Matrix4::Identity();
    projection_matrix_ = Math::Matrix4::Perspective(
        3.14159f / 4.0f,
        static_cast<float32>(device->GetSwapChainExtent().width) /
        static_cast<float32>(device->GetSwapChainExtent().height),
        0.1f, 100.0f
    );

    if (!CreateRenderPass()) return false;
    if (!CreateFramebuffers()) return false;
    if (!CreateVertexBuffer()) return false;
    if (!CreateUniformBuffers()) return false;
    if (!CreateDescriptorPool()) return false;
    if (!CreateDescriptorSets()) return false;
    if (!CreatePipeline()) return false;

    DRX_LOG_INFO("Forward renderer initialized");
    return true;
}

void ForwardRenderer::Shutdown() {
    if (!device_) return;

    VkDevice vk_device = device_->GetDevice();

    vkDestroyPipeline(vk_device, pipeline_, nullptr);
    vkDestroyPipelineLayout(vk_device, pipeline_layout_, nullptr);
    vkDestroyDescriptorPool(vk_device, descriptor_pool_, nullptr);
    vkDestroyDescriptorSetLayout(vk_device, descriptor_set_layout_, nullptr);

    for (size_t i = 0; i < uniform_buffers_.size(); ++i) {
        vkDestroyBuffer(vk_device, uniform_buffers_[i], nullptr);
        vkFreeMemory(vk_device, uniform_buffers_memory_[i], nullptr);
    }

    vkDestroyBuffer(vk_device, vertex_buffer_, nullptr);
    vkFreeMemory(vk_device, vertex_buffer_memory_, nullptr);

    CleanupFramebuffers();
    vkDestroyRenderPass(vk_device, render_pass_, nullptr);

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
    framebuffers_.resize(image_views.size());

    VkExtent2D extent = device_->GetSwapChainExtent();

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
            DRX_LOG_ERROR("Failed to create framebuffer");
            return false;
        }
    }

    return true;
}

bool ForwardRenderer::CreateVertexBuffer() {
    VkDeviceSize buffer_size = sizeof(Vertex) * kTriangleVertices.size();

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_->GetDevice(), &buffer_info, nullptr, &vertex_buffer_) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(device_->GetDevice(), vertex_buffer_, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = device_->FindMemoryType(
        mem_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (vkAllocateMemory(device_->GetDevice(), &alloc_info, nullptr, &vertex_buffer_memory_) != VK_SUCCESS) {
        return false;
    }

    vkBindBufferMemory(device_->GetDevice(), vertex_buffer_, vertex_buffer_memory_, 0);

    void* data;
    vkMapMemory(device_->GetDevice(), vertex_buffer_memory_, 0, buffer_size, 0, &data);
    memcpy(data, kTriangleVertices.data(), buffer_size);
    vkUnmapMemory(device_->GetDevice(), vertex_buffer_memory_);

    return true;
}

bool ForwardRenderer::CreateUniformBuffers() {
    VkDeviceSize buffer_size = sizeof(UniformBufferObject);

    uint32 frame_count = 2; // max frames in flight
    uniform_buffers_.resize(frame_count);
    uniform_buffers_memory_.resize(frame_count);
    uniform_buffers_mapped_.resize(frame_count);

    for (size_t i = 0; i < frame_count; ++i) {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = buffer_size;
        info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(device_->GetDevice(), &info, nullptr, &uniform_buffers_[i]);

        VkMemoryRequirements mem_req;
        vkGetBufferMemoryRequirements(device_->GetDevice(), uniform_buffers_[i], &mem_req);

        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = mem_req.size;
        alloc.memoryTypeIndex = device_->FindMemoryType(
            mem_req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        vkAllocateMemory(device_->GetDevice(), &alloc, nullptr, &uniform_buffers_memory_[i]);
        vkBindBufferMemory(device_->GetDevice(), uniform_buffers_[i], uniform_buffers_memory_[i], 0);
        vkMapMemory(device_->GetDevice(), uniform_buffers_memory_[i], 0, buffer_size, 0, &uniform_buffers_mapped_[i]);
    }

    return true;
}

bool ForwardRenderer::CreateDescriptorPool() {
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_size.descriptorCount = 2;

    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.poolSizeCount = 1;
    info.pPoolSizes = &pool_size;
    info.maxSets = 2;

    return vkCreateDescriptorPool(device_->GetDevice(), &info, nullptr, &descriptor_pool_) == VK_SUCCESS;
}

bool ForwardRenderer::CreateDescriptorSets() {
    // Create layout
    VkDescriptorSetLayoutBinding ubo_binding{};
    ubo_binding.binding = 0;
    ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_binding.descriptorCount = 1;
    ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &ubo_binding;

    vkCreateDescriptorSetLayout(device_->GetDevice(), &layout_info, nullptr, &descriptor_set_layout_);

    // Allocate sets
    Array<VkDescriptorSetLayout> layouts(2, descriptor_set_layout_);
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = descriptor_pool_;
    alloc_info.descriptorSetCount = 2;
    alloc_info.pSetLayouts = layouts.data();

    descriptor_sets_.resize(2);
    vkAllocateDescriptorSets(device_->GetDevice(), &alloc_info, descriptor_sets_.data());

    // Update sets
    for (size_t i = 0; i < 2; ++i) {
        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = uniform_buffers_[i];
        buffer_info.offset = 0;
        buffer_info.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptor_sets_[i];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &buffer_info;

        vkUpdateDescriptorSets(device_->GetDevice(), 1, &write, 0, nullptr);
    }

    return true;
}

bool ForwardRenderer::CreatePipeline() {
    // Create shader modules from embedded SPIR-V
    VkShaderModuleCreateInfo vert_info{};
    vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_info.codeSize = sizeof(kVertexShaderSPIRV);
    vert_info.pCode = kVertexShaderSPIRV;

    VkShaderModule vert_module;
    vkCreateShaderModule(device_->GetDevice(), &vert_info, nullptr, &vert_module);

    VkShaderModuleCreateInfo frag_info{};
    frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    frag_info.codeSize = sizeof(kFragmentShaderSPIRV);
    frag_info.pCode = kFragmentShaderSPIRV;

    VkShaderModule frag_module;
    vkCreateShaderModule(device_->GetDevice(), &frag_info, nullptr, &frag_module);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_module;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_module;
    stages[1].pName = "main";

    // Vertex input
    auto binding = Vertex::GetBindingDescription();
    auto attributes = Vertex::GetAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = static_cast<uint32>(attributes.size());
    vertex_input.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkExtent2D extent = device_->GetSwapChainExtent();
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float32>(extent.width);
    viewport.height = static_cast<float32>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &descriptor_set_layout_;

    vkCreatePipelineLayout(device_->GetDevice(), &layout_info, nullptr, &pipeline_layout_);

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
    pipeline_info.layout = pipeline_layout_;
    pipeline_info.renderPass = render_pass_;
    pipeline_info.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device_->GetDevice(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_);

    vkDestroyShaderModule(device_->GetDevice(), vert_module, nullptr);
    vkDestroyShaderModule(device_->GetDevice(), frag_module, nullptr);

    return result == VK_SUCCESS;
}

void ForwardRenderer::CleanupFramebuffers() {
    for (auto fb : framebuffers_) {
        vkDestroyFramebuffer(device_->GetDevice(), fb, nullptr);
    }
    framebuffers_.clear();
}

void ForwardRenderer::BeginFrame() {
    time_ += 0.016f;
    UpdateUniformBuffer(device_->GetCurrentFrameIndex());

    VkCommandBuffer cmd = device_->GetCurrentCommandBuffer();

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &begin_info);

    VkRenderPassBeginInfo rp_info{};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_info.renderPass = render_pass_;
    rp_info.framebuffer = framebuffers_[device_->GetCurrentImageIndex()];
    rp_info.renderArea.offset = {0, 0};
    rp_info.renderArea.extent = device_->GetSwapChainExtent();

    VkClearValue clear_color = {{{0.1f, 0.1f, 0.15f, 1.0f}}};
    rp_info.clearValueCount = 1;
    rp_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_,
        0, 1, &descriptor_sets_[device_->GetCurrentFrameIndex()], 0, nullptr);
}

void ForwardRenderer::EndFrame() {
    VkCommandBuffer cmd = device_->GetCurrentCommandBuffer();
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

void ForwardRenderer::DrawTriangle() {
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
    UniformBufferObject ubo{};
    ubo.model = Math::Matrix4::RotationY(time_);
    ubo.view = view_matrix_;
    ubo.projection = projection_matrix_;
    ubo.camera_pos = camera_position_;
    ubo.time = time_;

    memcpy(uniform_buffers_mapped_[frame_index], &ubo, sizeof(ubo));
}

void ForwardRenderer::OnResize(uint32 width, uint32 height) {
    projection_matrix_ = Math::Matrix4::Perspective(
        3.14159f / 4.0f,
        static_cast<float32>(width) / static_cast<float32>(height),
        0.1f, 100.0f
    );

    CleanupFramebuffers();
    CreateFramebuffers();
}

} // namespace DRX::Renderer

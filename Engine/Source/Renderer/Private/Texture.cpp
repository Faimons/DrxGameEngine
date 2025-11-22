/**
 * DRX Engine - Texture System Implementation
 */

#include "Renderer/Public/Texture.h"
#include "Core/Public/Log.h"

#include <algorithm>
#include <cstring>

// STB Image for texture loading
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace DRX::Renderer {

// ============================================================================
// Texture Implementation
// ============================================================================

Texture::Texture() = default;

Texture::~Texture() {
    Destroy();
}

bool Texture::Create(HAL::GraphicsDevice* device, const TextureDesc& desc) {
    device_ = device;
    desc_ = desc;

    if (!CreateImage()) {
        DRX_LOG_ERROR("Failed to create texture image: {}", desc.name);
        return false;
    }

    if (!CreateImageView()) {
        DRX_LOG_ERROR("Failed to create texture image view: {}", desc.name);
        return false;
    }

    if (!CreateSampler()) {
        DRX_LOG_ERROR("Failed to create texture sampler: {}", desc.name);
        return false;
    }

    DRX_LOG_INFO("Created texture: {} ({}x{})", desc.name, desc.width, desc.height);
    return true;
}

bool Texture::CreateFromFile(HAL::GraphicsDevice* device, const String& path) {
    device_ = device;

    int width, height, channels;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        DRX_LOG_ERROR("Failed to load texture: {}", path);
        return false;
    }

    desc_.width = static_cast<uint32>(width);
    desc_.height = static_cast<uint32>(height);
    desc_.format = TextureFormat::RGBA8;
    desc_.type = TextureType::Texture2D;
    desc_.name = path;
    desc_.mip_levels = desc_.generate_mipmaps ? CalculateMipLevels(desc_.width, desc_.height) : 1;

    VkDeviceSize image_size = desc_.width * desc_.height * 4;

    // Create staging buffer
    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = image_size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkDevice vk_device = device_->GetDevice();

    if (vkCreateBuffer(vk_device, &buffer_info, nullptr, &staging_buffer) != VK_SUCCESS) {
        stbi_image_free(pixels);
        return false;
    }

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(vk_device, staging_buffer, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = device_->FindMemoryType(
        mem_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (vkAllocateMemory(vk_device, &alloc_info, nullptr, &staging_memory) != VK_SUCCESS) {
        vkDestroyBuffer(vk_device, staging_buffer, nullptr);
        stbi_image_free(pixels);
        return false;
    }

    vkBindBufferMemory(vk_device, staging_buffer, staging_memory, 0);

    void* data;
    vkMapMemory(vk_device, staging_memory, 0, image_size, 0, &data);
    std::memcpy(data, pixels, static_cast<size_t>(image_size));
    vkUnmapMemory(vk_device, staging_memory);

    stbi_image_free(pixels);

    // Create image
    if (!CreateImage()) {
        vkDestroyBuffer(vk_device, staging_buffer, nullptr);
        vkFreeMemory(vk_device, staging_memory, nullptr);
        return false;
    }

    // Transition and copy
    TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy buffer to image
    VkCommandBuffer cmd = device_->BeginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {desc_.width, desc_.height, 1};

    vkCmdCopyBufferToImage(cmd, staging_buffer, image_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    device_->EndSingleTimeCommands(cmd);

    // Generate mipmaps or transition to shader read
    if (desc_.generate_mipmaps && desc_.mip_levels > 1) {
        GenerateMipmaps();
    } else {
        TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // Cleanup staging
    vkDestroyBuffer(vk_device, staging_buffer, nullptr);
    vkFreeMemory(vk_device, staging_memory, nullptr);

    if (!CreateImageView()) {
        return false;
    }

    if (!CreateSampler()) {
        return false;
    }

    DRX_LOG_INFO("Loaded texture: {} ({}x{}, {} mips)", path, desc_.width, desc_.height, desc_.mip_levels);
    return true;
}

bool Texture::CreateFromMemory(HAL::GraphicsDevice* device, const TextureDesc& desc, const void* data) {
    device_ = device;
    desc_ = desc;

    if (!CreateImage()) {
        return false;
    }

    if (data) {
        SetData(data, desc_.width * desc_.height * GetBytesPerPixel(desc_.format));
    }

    if (!CreateImageView()) {
        return false;
    }

    if (!CreateSampler()) {
        return false;
    }

    return true;
}

void Texture::Destroy() {
    if (!device_) return;

    VkDevice vk_device = device_->GetDevice();

    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(vk_device, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }

    if (image_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vk_device, image_view_, nullptr);
        image_view_ = VK_NULL_HANDLE;
    }

    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(vk_device, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }

    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }

    device_ = nullptr;
}

void Texture::SetData(const void* data, uint32 size, uint32 mip_level, uint32 layer) {
    if (!device_ || !image_) return;

    VkDevice vk_device = device_->GetDevice();
    VkDeviceSize buffer_size = size;

    // Create staging buffer
    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(vk_device, &buffer_info, nullptr, &staging_buffer);

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

    void* mapped;
    vkMapMemory(vk_device, staging_memory, 0, buffer_size, 0, &mapped);
    std::memcpy(mapped, data, size);
    vkUnmapMemory(vk_device, staging_memory);

    // Copy to image
    TransitionLayout(current_layout_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkCommandBuffer cmd = device_->BeginSingleTimeCommands();

    uint32 mip_width = std::max(1u, desc_.width >> mip_level);
    uint32 mip_height = std::max(1u, desc_.height >> mip_level);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = mip_level;
    region.imageSubresource.baseArrayLayer = layer;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {mip_width, mip_height, 1};

    vkCmdCopyBufferToImage(cmd, staging_buffer, image_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    device_->EndSingleTimeCommands(cmd);

    TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(vk_device, staging_buffer, nullptr);
    vkFreeMemory(vk_device, staging_memory, nullptr);
}

VkDescriptorImageInfo Texture::GetDescriptorInfo() const {
    VkDescriptorImageInfo info{};
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    info.imageView = image_view_;
    info.sampler = sampler_;
    return info;
}

uint32 Texture::CalculateMipLevels(uint32 width, uint32 height) {
    return static_cast<uint32>(std::floor(std::log2(std::max(width, height)))) + 1;
}

VkFormat Texture::ToVulkanFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::R8: return VK_FORMAT_R8_UNORM;
        case TextureFormat::RG8: return VK_FORMAT_R8G8_UNORM;
        case TextureFormat::RGB8: return VK_FORMAT_R8G8B8_UNORM;
        case TextureFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TextureFormat::RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case TextureFormat::Depth24Stencil8: return VK_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::Depth32F: return VK_FORMAT_D32_SFLOAT;
        case TextureFormat::BC1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case TextureFormat::BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
        case TextureFormat::BC5: return VK_FORMAT_BC5_UNORM_BLOCK;
        case TextureFormat::BC7: return VK_FORMAT_BC7_UNORM_BLOCK;
        default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

uint32 Texture::GetBytesPerPixel(TextureFormat format) {
    switch (format) {
        case TextureFormat::R8: return 1;
        case TextureFormat::RG8: return 2;
        case TextureFormat::RGB8: return 3;
        case TextureFormat::RGBA8: return 4;
        case TextureFormat::RGBA16F: return 8;
        case TextureFormat::RGBA32F: return 16;
        case TextureFormat::Depth24Stencil8: return 4;
        case TextureFormat::Depth32F: return 4;
        default: return 4;
    }
}

bool Texture::CreateImage() {
    VkDevice vk_device = device_->GetDevice();

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = desc_.width;
    image_info.extent.height = desc_.height;
    image_info.extent.depth = 1;
    image_info.mipLevels = desc_.mip_levels;
    image_info.arrayLayers = desc_.array_layers;
    image_info.format = ToVulkanFormat(desc_.format);
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;

    if (desc_.type == TextureType::TextureCube) {
        image_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        image_info.arrayLayers = 6;
    }

    if (vkCreateImage(vk_device, &image_info, nullptr, &image_) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(vk_device, image_, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = device_->FindMemoryType(
        mem_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if (vkAllocateMemory(vk_device, &alloc_info, nullptr, &memory_) != VK_SUCCESS) {
        return false;
    }

    vkBindImageMemory(vk_device, image_, memory_, 0);
    current_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    return true;
}

bool Texture::CreateImageView() {
    VkDevice vk_device = device_->GetDevice();

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image_;
    view_info.format = ToVulkanFormat(desc_.format);
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = desc_.mip_levels;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = desc_.array_layers;

    switch (desc_.type) {
        case TextureType::Texture2D:
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            break;
        case TextureType::TextureCube:
            view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            view_info.subresourceRange.layerCount = 6;
            break;
        case TextureType::Texture2DArray:
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            break;
        case TextureType::Texture3D:
            view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
            break;
    }

    return vkCreateImageView(vk_device, &view_info, nullptr, &image_view_) == VK_SUCCESS;
}

bool Texture::CreateSampler() {
    VkDevice vk_device = device_->GetDevice();

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    switch (desc_.filter) {
        case TextureFilter::Nearest:
            sampler_info.magFilter = VK_FILTER_NEAREST;
            sampler_info.minFilter = VK_FILTER_NEAREST;
            sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        case TextureFilter::Linear:
            sampler_info.magFilter = VK_FILTER_LINEAR;
            sampler_info.minFilter = VK_FILTER_LINEAR;
            sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        case TextureFilter::Trilinear:
            sampler_info.magFilter = VK_FILTER_LINEAR;
            sampler_info.minFilter = VK_FILTER_LINEAR;
            sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            break;
        case TextureFilter::Anisotropic:
            sampler_info.magFilter = VK_FILTER_LINEAR;
            sampler_info.minFilter = VK_FILTER_LINEAR;
            sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            sampler_info.anisotropyEnable = VK_TRUE;
            sampler_info.maxAnisotropy = 16.0f;
            break;
    }

    switch (desc_.wrap) {
        case TextureWrap::Repeat:
            sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            break;
        case TextureWrap::MirroredRepeat:
            sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            break;
        case TextureWrap::ClampToEdge:
            sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            break;
        case TextureWrap::ClampToBorder:
            sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            break;
    }

    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = static_cast<float>(desc_.mip_levels);

    return vkCreateSampler(vk_device, &sampler_info, nullptr, &sampler_) == VK_SUCCESS;
}

void Texture::TransitionLayout(VkImageLayout old_layout, VkImageLayout new_layout) {
    VkCommandBuffer cmd = device_->BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = desc_.mip_levels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = desc_.array_layers;

    VkPipelineStageFlags source_stage;
    VkPipelineStageFlags dest_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dest_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dest_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        source_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dest_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, source_stage, dest_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    device_->EndSingleTimeCommands(cmd);
    current_layout_ = new_layout;
}

void Texture::GenerateMipmaps() {
    VkCommandBuffer cmd = device_->BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image_;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32 mip_width = static_cast<int32>(desc_.width);
    int32 mip_height = static_cast<int32>(desc_.height);

    for (uint32 i = 1; i < desc_.mip_levels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mip_width, mip_height, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {mip_width > 1 ? mip_width / 2 : 1,
                              mip_height > 1 ? mip_height / 2 : 1, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &barrier);

        if (mip_width > 1) mip_width /= 2;
        if (mip_height > 1) mip_height /= 2;
    }

    barrier.subresourceRange.baseMipLevel = desc_.mip_levels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    device_->EndSingleTimeCommands(cmd);
    current_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

// ============================================================================
// Texture Manager Implementation
// ============================================================================

TextureManager& TextureManager::Instance() {
    static TextureManager instance;
    return instance;
}

void TextureManager::Initialize(HAL::GraphicsDevice* device) {
    device_ = device;
    CreateDefaultTextures();
    DRX_LOG_INFO("TextureManager initialized");
}

void TextureManager::Shutdown() {
    textures_.clear();
    white_texture_.reset();
    black_texture_.reset();
    normal_texture_.reset();
    device_ = nullptr;
    DRX_LOG_INFO("TextureManager shutdown");
}

SharedPtr<Texture> TextureManager::Load(const String& path) {
    auto it = textures_.find(path);
    if (it != textures_.end()) {
        return it->second;
    }

    auto texture = MakeShared<Texture>();
    if (!texture->CreateFromFile(device_, path)) {
        return nullptr;
    }

    textures_[path] = texture;
    return texture;
}

SharedPtr<Texture> TextureManager::Get(const String& name) {
    auto it = textures_.find(name);
    return it != textures_.end() ? it->second : nullptr;
}

SharedPtr<Texture> TextureManager::Create(const TextureDesc& desc, const void* data) {
    auto texture = MakeShared<Texture>();
    if (!texture->CreateFromMemory(device_, desc, data)) {
        return nullptr;
    }

    textures_[desc.name] = texture;
    return texture;
}

void TextureManager::Unload(const String& name) {
    textures_.erase(name);
}

void TextureManager::UnloadAll() {
    textures_.clear();
}

SharedPtr<Texture> TextureManager::GetWhiteTexture() {
    return white_texture_;
}

SharedPtr<Texture> TextureManager::GetBlackTexture() {
    return black_texture_;
}

SharedPtr<Texture> TextureManager::GetNormalTexture() {
    return normal_texture_;
}

void TextureManager::CreateDefaultTextures() {
    // White texture (1x1)
    {
        TextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        desc.format = TextureFormat::RGBA8;
        desc.generate_mipmaps = false;
        desc.name = "__white";

        uint32 white = 0xFFFFFFFF;
        white_texture_ = Create(desc, &white);
    }

    // Black texture (1x1)
    {
        TextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        desc.format = TextureFormat::RGBA8;
        desc.generate_mipmaps = false;
        desc.name = "__black";

        uint32 black = 0xFF000000;
        black_texture_ = Create(desc, &black);
    }

    // Default normal map (1x1, pointing up)
    {
        TextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        desc.format = TextureFormat::RGBA8;
        desc.generate_mipmaps = false;
        desc.name = "__normal";

        // Normal pointing up: (0.5, 0.5, 1.0) encoded
        uint32 normal = 0xFFFF8080;  // RGBA: 128, 128, 255, 255
        normal_texture_ = Create(desc, &normal);
    }
}

} // namespace DRX::Renderer

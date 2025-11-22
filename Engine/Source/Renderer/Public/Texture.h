#pragma once

/**
 * DRX Engine - Texture System
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Platform.h"
#include "HAL/Public/GraphicsDevice.h"

#include <vulkan/vulkan.h>

namespace DRX::Renderer {

// ============================================================================
// Texture Format
// ============================================================================

enum class TextureFormat {
    R8,
    RG8,
    RGB8,
    RGBA8,
    RGBA16F,
    RGBA32F,
    Depth24Stencil8,
    Depth32F,
    BC1,  // DXT1
    BC3,  // DXT5
    BC5,  // Normal maps
    BC7   // High quality
};

enum class TextureFilter {
    Nearest,
    Linear,
    Trilinear,
    Anisotropic
};

enum class TextureWrap {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder
};

enum class TextureType {
    Texture2D,
    TextureCube,
    Texture2DArray,
    Texture3D
};

// ============================================================================
// Texture Descriptor
// ============================================================================

struct TextureDesc {
    uint32 width = 1;
    uint32 height = 1;
    uint32 depth = 1;
    uint32 mip_levels = 1;
    uint32 array_layers = 1;
    TextureFormat format = TextureFormat::RGBA8;
    TextureType type = TextureType::Texture2D;
    TextureFilter filter = TextureFilter::Linear;
    TextureWrap wrap = TextureWrap::Repeat;
    bool generate_mipmaps = true;
    String name;
};

// ============================================================================
// Texture Class
// ============================================================================

class DRX_API Texture : public NonCopyable {
public:
    Texture();
    ~Texture();

    bool Create(HAL::GraphicsDevice* device, const TextureDesc& desc);
    bool CreateFromFile(HAL::GraphicsDevice* device, const String& path);
    bool CreateFromMemory(HAL::GraphicsDevice* device, const TextureDesc& desc, const void* data);
    void Destroy();

    void SetData(const void* data, uint32 size, uint32 mip_level = 0, uint32 layer = 0);

    // Getters
    uint32 GetWidth() const { return desc_.width; }
    uint32 GetHeight() const { return desc_.height; }
    uint32 GetMipLevels() const { return desc_.mip_levels; }
    TextureFormat GetFormat() const { return desc_.format; }
    TextureType GetType() const { return desc_.type; }
    const String& GetName() const { return desc_.name; }

    VkImage GetImage() const { return image_; }
    VkImageView GetImageView() const { return image_view_; }
    VkSampler GetSampler() const { return sampler_; }
    VkDescriptorImageInfo GetDescriptorInfo() const;

    bool IsValid() const { return image_ != VK_NULL_HANDLE; }

    // Static utilities
    static uint32 CalculateMipLevels(uint32 width, uint32 height);
    static VkFormat ToVulkanFormat(TextureFormat format);
    static uint32 GetBytesPerPixel(TextureFormat format);

private:
    bool CreateImage();
    bool CreateImageView();
    bool CreateSampler();
    void TransitionLayout(VkImageLayout old_layout, VkImageLayout new_layout);
    void GenerateMipmaps();

    HAL::GraphicsDevice* device_ = nullptr;
    TextureDesc desc_;

    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkImageLayout current_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
};

// ============================================================================
// Texture Manager
// ============================================================================

class DRX_API TextureManager {
public:
    static TextureManager& Instance();

    void Initialize(HAL::GraphicsDevice* device);
    void Shutdown();

    SharedPtr<Texture> Load(const String& path);
    SharedPtr<Texture> Get(const String& name);
    SharedPtr<Texture> Create(const TextureDesc& desc, const void* data = nullptr);

    void Unload(const String& name);
    void UnloadAll();

    // Default textures
    SharedPtr<Texture> GetWhiteTexture();
    SharedPtr<Texture> GetBlackTexture();
    SharedPtr<Texture> GetNormalTexture();

private:
    TextureManager() = default;

    void CreateDefaultTextures();

    HAL::GraphicsDevice* device_ = nullptr;
    HashMap<String, SharedPtr<Texture>> textures_;

    SharedPtr<Texture> white_texture_;
    SharedPtr<Texture> black_texture_;
    SharedPtr<Texture> normal_texture_;
};

} // namespace DRX::Renderer

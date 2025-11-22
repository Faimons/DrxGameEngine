#pragma once

/**
 * DRX Engine - PBR Material System
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Vector4.h"
#include "Texture.h"

#include <vulkan/vulkan.h>

namespace DRX::Renderer {

// ============================================================================
// Material Properties
// ============================================================================

struct PBRMaterialProperties {
    Math::Vector4 base_color = {1.0f, 1.0f, 1.0f, 1.0f};
    float32 metallic = 0.0f;
    float32 roughness = 0.5f;
    float32 ao = 1.0f;
    float32 emissive_strength = 0.0f;
    Math::Vector3 emissive_color = {0.0f, 0.0f, 0.0f};
    float32 normal_strength = 1.0f;
    float32 alpha_cutoff = 0.5f;
    float32 ior = 1.5f;  // Index of refraction
    float32 padding[2];
};

enum class BlendMode {
    Opaque,
    AlphaBlend,
    AlphaCutout,
    Additive
};

enum class CullMode {
    None,
    Front,
    Back
};

// ============================================================================
// Material Class
// ============================================================================

class DRX_API Material : public NonCopyable {
public:
    Material();
    ~Material();

    bool Create(HAL::GraphicsDevice* device, const String& name);
    void Destroy();

    // Textures
    void SetAlbedoTexture(SharedPtr<Texture> texture);
    void SetNormalTexture(SharedPtr<Texture> texture);
    void SetMetallicRoughnessTexture(SharedPtr<Texture> texture);
    void SetAOTexture(SharedPtr<Texture> texture);
    void SetEmissiveTexture(SharedPtr<Texture> texture);

    SharedPtr<Texture> GetAlbedoTexture() const { return albedo_texture_; }
    SharedPtr<Texture> GetNormalTexture() const { return normal_texture_; }
    SharedPtr<Texture> GetMetallicRoughnessTexture() const { return metallic_roughness_texture_; }
    SharedPtr<Texture> GetAOTexture() const { return ao_texture_; }
    SharedPtr<Texture> GetEmissiveTexture() const { return emissive_texture_; }

    // Properties
    void SetBaseColor(const Math::Vector4& color);
    void SetMetallic(float32 metallic);
    void SetRoughness(float32 roughness);
    void SetEmissive(const Math::Vector3& color, float32 strength = 1.0f);
    void SetNormalStrength(float32 strength);
    void SetAlphaCutoff(float32 cutoff);

    const PBRMaterialProperties& GetProperties() const { return properties_; }
    PBRMaterialProperties& GetProperties() { return properties_; }

    // Render state
    void SetBlendMode(BlendMode mode) { blend_mode_ = mode; }
    void SetCullMode(CullMode mode) { cull_mode_ = mode; }
    void SetDoubleSided(bool double_sided) { double_sided_ = double_sided; }

    BlendMode GetBlendMode() const { return blend_mode_; }
    CullMode GetCullMode() const { return cull_mode_; }
    bool IsDoubleSided() const { return double_sided_; }
    bool IsTransparent() const { return blend_mode_ != BlendMode::Opaque; }

    // Binding
    void Bind(VkCommandBuffer cmd, VkPipelineLayout layout);
    void UpdateDescriptorSet();

    VkDescriptorSet GetDescriptorSet() const { return descriptor_set_; }
    const String& GetName() const { return name_; }

    // Uniform buffer
    VkBuffer GetUniformBuffer() const { return uniform_buffer_; }

private:
    void CreateUniformBuffer();
    void CreateDescriptorSet();
    void UpdateUniformBuffer();

    HAL::GraphicsDevice* device_ = nullptr;
    String name_;

    // Properties
    PBRMaterialProperties properties_;
    BlendMode blend_mode_ = BlendMode::Opaque;
    CullMode cull_mode_ = CullMode::Back;
    bool double_sided_ = false;
    bool dirty_ = true;

    // Textures
    SharedPtr<Texture> albedo_texture_;
    SharedPtr<Texture> normal_texture_;
    SharedPtr<Texture> metallic_roughness_texture_;
    SharedPtr<Texture> ao_texture_;
    SharedPtr<Texture> emissive_texture_;

    // Vulkan resources
    VkBuffer uniform_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uniform_buffer_memory_ = VK_NULL_HANDLE;
    void* uniform_buffer_mapped_ = nullptr;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
};

// ============================================================================
// Material Manager
// ============================================================================

class DRX_API MaterialManager {
public:
    static MaterialManager& Instance();

    void Initialize(HAL::GraphicsDevice* device);
    void Shutdown();

    SharedPtr<Material> Create(const String& name);
    SharedPtr<Material> Get(const String& name);
    SharedPtr<Material> GetDefault();

    void Remove(const String& name);
    void Clear();

    VkDescriptorSetLayout GetDescriptorSetLayout() const { return descriptor_set_layout_; }
    VkDescriptorPool GetDescriptorPool() const { return descriptor_pool_; }

private:
    MaterialManager() = default;

    void CreateDescriptorSetLayout();
    void CreateDescriptorPool();
    void CreateDefaultMaterial();

    HAL::GraphicsDevice* device_ = nullptr;
    HashMap<String, SharedPtr<Material>> materials_;
    SharedPtr<Material> default_material_;

    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
};

} // namespace DRX::Renderer

/**
 * DRX Engine - PBR Material System Implementation
 */

#include "Renderer/Public/Material.h"
#include "Core/Public/Log.h"

#include <cstring>

namespace DRX::Renderer {

// ============================================================================
// Material Implementation
// ============================================================================

Material::Material() = default;

Material::~Material() {
    Destroy();
}

bool Material::Create(HAL::GraphicsDevice* device, const String& name) {
    device_ = device;
    name_ = name;

    CreateUniformBuffer();
    CreateDescriptorSet();

    // Set default textures
    auto& tex_mgr = TextureManager::Instance();
    albedo_texture_ = tex_mgr.GetWhiteTexture();
    normal_texture_ = tex_mgr.GetNormalTexture();
    metallic_roughness_texture_ = tex_mgr.GetWhiteTexture();
    ao_texture_ = tex_mgr.GetWhiteTexture();
    emissive_texture_ = tex_mgr.GetBlackTexture();

    UpdateDescriptorSet();
    UpdateUniformBuffer();

    DRX_LOG_INFO("Created material: {}", name);
    return true;
}

void Material::Destroy() {
    if (!device_) return;

    VkDevice vk_device = device_->GetDevice();

    if (uniform_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk_device, uniform_buffer_, nullptr);
        uniform_buffer_ = VK_NULL_HANDLE;
    }

    if (uniform_buffer_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk_device, uniform_buffer_memory_, nullptr);
        uniform_buffer_memory_ = VK_NULL_HANDLE;
    }

    albedo_texture_.reset();
    normal_texture_.reset();
    metallic_roughness_texture_.reset();
    ao_texture_.reset();
    emissive_texture_.reset();

    device_ = nullptr;
}

void Material::SetAlbedoTexture(SharedPtr<Texture> texture) {
    albedo_texture_ = texture ? texture : TextureManager::Instance().GetWhiteTexture();
    dirty_ = true;
}

void Material::SetNormalTexture(SharedPtr<Texture> texture) {
    normal_texture_ = texture ? texture : TextureManager::Instance().GetNormalTexture();
    dirty_ = true;
}

void Material::SetMetallicRoughnessTexture(SharedPtr<Texture> texture) {
    metallic_roughness_texture_ = texture ? texture : TextureManager::Instance().GetWhiteTexture();
    dirty_ = true;
}

void Material::SetAOTexture(SharedPtr<Texture> texture) {
    ao_texture_ = texture ? texture : TextureManager::Instance().GetWhiteTexture();
    dirty_ = true;
}

void Material::SetEmissiveTexture(SharedPtr<Texture> texture) {
    emissive_texture_ = texture ? texture : TextureManager::Instance().GetBlackTexture();
    dirty_ = true;
}

void Material::SetBaseColor(const Math::Vector4& color) {
    properties_.base_color = color;
    dirty_ = true;
}

void Material::SetMetallic(float32 metallic) {
    properties_.metallic = metallic;
    dirty_ = true;
}

void Material::SetRoughness(float32 roughness) {
    properties_.roughness = roughness;
    dirty_ = true;
}

void Material::SetEmissive(const Math::Vector3& color, float32 strength) {
    properties_.emissive_color = color;
    properties_.emissive_strength = strength;
    dirty_ = true;
}

void Material::SetNormalStrength(float32 strength) {
    properties_.normal_strength = strength;
    dirty_ = true;
}

void Material::SetAlphaCutoff(float32 cutoff) {
    properties_.alpha_cutoff = cutoff;
    dirty_ = true;
}

void Material::Bind(VkCommandBuffer cmd, VkPipelineLayout layout) {
    if (dirty_) {
        UpdateUniformBuffer();
        UpdateDescriptorSet();
        dirty_ = false;
    }

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1,
                            &descriptor_set_, 0, nullptr);
}

void Material::UpdateDescriptorSet() {
    if (!descriptor_set_) return;

    VkDevice vk_device = device_->GetDevice();

    Array<VkWriteDescriptorSet> writes;

    // Uniform buffer
    VkDescriptorBufferInfo buffer_info{};
    buffer_info.buffer = uniform_buffer_;
    buffer_info.offset = 0;
    buffer_info.range = sizeof(PBRMaterialProperties);

    VkWriteDescriptorSet buffer_write{};
    buffer_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    buffer_write.dstSet = descriptor_set_;
    buffer_write.dstBinding = 0;
    buffer_write.dstArrayElement = 0;
    buffer_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    buffer_write.descriptorCount = 1;
    buffer_write.pBufferInfo = &buffer_info;
    writes.push_back(buffer_write);

    // Textures
    VkDescriptorImageInfo albedo_info = albedo_texture_->GetDescriptorInfo();
    VkDescriptorImageInfo normal_info = normal_texture_->GetDescriptorInfo();
    VkDescriptorImageInfo metallic_roughness_info = metallic_roughness_texture_->GetDescriptorInfo();
    VkDescriptorImageInfo ao_info = ao_texture_->GetDescriptorInfo();
    VkDescriptorImageInfo emissive_info = emissive_texture_->GetDescriptorInfo();

    auto add_texture_write = [&](uint32 binding, VkDescriptorImageInfo* info) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptor_set_;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = info;
        writes.push_back(write);
    };

    add_texture_write(1, &albedo_info);
    add_texture_write(2, &normal_info);
    add_texture_write(3, &metallic_roughness_info);
    add_texture_write(4, &ao_info);
    add_texture_write(5, &emissive_info);

    vkUpdateDescriptorSets(vk_device, static_cast<uint32>(writes.size()), writes.data(), 0, nullptr);
}

void Material::CreateUniformBuffer() {
    VkDevice vk_device = device_->GetDevice();

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = sizeof(PBRMaterialProperties);
    buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(vk_device, &buffer_info, nullptr, &uniform_buffer_);

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(vk_device, uniform_buffer_, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = device_->FindMemoryType(
        mem_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    vkAllocateMemory(vk_device, &alloc_info, nullptr, &uniform_buffer_memory_);
    vkBindBufferMemory(vk_device, uniform_buffer_, uniform_buffer_memory_, 0);

    vkMapMemory(vk_device, uniform_buffer_memory_, 0, sizeof(PBRMaterialProperties), 0, &uniform_buffer_mapped_);
}

void Material::CreateDescriptorSet() {
    VkDevice vk_device = device_->GetDevice();

    auto& mat_mgr = MaterialManager::Instance();

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = mat_mgr.GetDescriptorPool();
    alloc_info.descriptorSetCount = 1;
    VkDescriptorSetLayout layout = mat_mgr.GetDescriptorSetLayout();
    alloc_info.pSetLayouts = &layout;

    vkAllocateDescriptorSets(vk_device, &alloc_info, &descriptor_set_);
}

void Material::UpdateUniformBuffer() {
    if (uniform_buffer_mapped_) {
        std::memcpy(uniform_buffer_mapped_, &properties_, sizeof(PBRMaterialProperties));
    }
}

// ============================================================================
// Material Manager Implementation
// ============================================================================

MaterialManager& MaterialManager::Instance() {
    static MaterialManager instance;
    return instance;
}

void MaterialManager::Initialize(HAL::GraphicsDevice* device) {
    device_ = device;

    CreateDescriptorSetLayout();
    CreateDescriptorPool();
    CreateDefaultMaterial();

    DRX_LOG_INFO("MaterialManager initialized");
}

void MaterialManager::Shutdown() {
    materials_.clear();
    default_material_.reset();

    VkDevice vk_device = device_->GetDevice();

    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vk_device, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }

    if (descriptor_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vk_device, descriptor_set_layout_, nullptr);
        descriptor_set_layout_ = VK_NULL_HANDLE;
    }

    device_ = nullptr;
    DRX_LOG_INFO("MaterialManager shutdown");
}

SharedPtr<Material> MaterialManager::Create(const String& name) {
    auto material = MakeShared<Material>();
    if (!material->Create(device_, name)) {
        return nullptr;
    }

    materials_[name] = material;
    return material;
}

SharedPtr<Material> MaterialManager::Get(const String& name) {
    auto it = materials_.find(name);
    return it != materials_.end() ? it->second : nullptr;
}

SharedPtr<Material> MaterialManager::GetDefault() {
    return default_material_;
}

void MaterialManager::Remove(const String& name) {
    materials_.erase(name);
}

void MaterialManager::Clear() {
    materials_.clear();
}

void MaterialManager::CreateDescriptorSetLayout() {
    VkDevice vk_device = device_->GetDevice();

    Array<VkDescriptorSetLayoutBinding> bindings;

    // Uniform buffer for material properties
    VkDescriptorSetLayoutBinding ubo_binding{};
    ubo_binding.binding = 0;
    ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_binding.descriptorCount = 1;
    ubo_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back(ubo_binding);

    // Texture samplers (albedo, normal, metallic-roughness, ao, emissive)
    for (uint32 i = 1; i <= 5; i++) {
        VkDescriptorSetLayoutBinding sampler_binding{};
        sampler_binding.binding = i;
        sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sampler_binding.descriptorCount = 1;
        sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(sampler_binding);
    }

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32>(bindings.size());
    layout_info.pBindings = bindings.data();

    vkCreateDescriptorSetLayout(vk_device, &layout_info, nullptr, &descriptor_set_layout_);
}

void MaterialManager::CreateDescriptorPool() {
    VkDevice vk_device = device_->GetDevice();

    const uint32 max_materials = 1000;

    Array<VkDescriptorPoolSize> pool_sizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, max_materials},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_materials * 5}
    };

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = static_cast<uint32>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    pool_info.maxSets = max_materials;

    vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &descriptor_pool_);
}

void MaterialManager::CreateDefaultMaterial() {
    default_material_ = Create("__default");
    default_material_->SetBaseColor({0.8f, 0.8f, 0.8f, 1.0f});
    default_material_->SetMetallic(0.0f);
    default_material_->SetRoughness(0.5f);
}

} // namespace DRX::Renderer

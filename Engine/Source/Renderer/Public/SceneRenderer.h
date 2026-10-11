#pragma once

/**
 * DRX Engine - Scene Renderer
 * Forward renderer: directional sun with PCF shadows, hemispheric ambient, sky, fog,
 * textured meshes, particles, debug lines and a 2D UI/text overlay.
 *
 * Frame flow (driven by the application):
 *   GraphicsDevice::BeginFrame()
 *   SceneRenderer::BeginFrame(camera, environment)
 *   SceneRenderer::DrawMesh / DrawParticles / DrawUI ...
 *   SceneRenderer::Render()           // records all passes into the current command buffer
 *   GraphicsDevice::EndFrame()
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Quaternion.h"
#include "HAL/Public/GraphicsDevice.h"
#include "Renderer/Public/RenderTypes.h"
#include "Renderer/Public/Font.h"

#include <vulkan/vulkan.h>

namespace DRX::Particles { class ParticleEmitter; class ParticleSystem; }
namespace DRX::UI { struct DrawCommand; }

namespace DRX::Renderer {

struct SceneRendererConfig {
    uint32 shadow_map_size = 2048;
    uint32 max_textures = 512;
    Array<float32> font_sizes = {16.0f, 28.0f, 56.0f};
};

class DRX_API SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    bool Initialize(HAL::GraphicsDevice* device, const SceneRendererConfig& config = {});
    void Shutdown();
    bool IsInitialized() const { return device_ != nullptr; }

    // ------------------------------------------------------------------
    // Resources (destruction is deferred until the GPU no longer uses them)
    // ------------------------------------------------------------------
    MeshHandle CreateMesh(const MeshData& data, const String& name = {});
    // Mesh whose vertices change every frame (skinned characters). One host visible vertex buffer per
    // frame in flight; the index buffer is fixed.
    MeshHandle CreateDynamicMesh(const MeshData& data, const String& name = {});
    // Replaces the vertices of a dynamic mesh (same count). Call between BeginFrame and the draw calls.
    bool UpdateDynamicMesh(MeshHandle mesh, const Array<Vertex>& vertices);
    void DestroyMesh(MeshHandle mesh);
    AABB GetMeshBounds(MeshHandle mesh) const;
    bool IsMeshValid(MeshHandle mesh) const;

    // RGBA8 pixels, tightly packed. srgb = colour data (albedo), false = linear data
    TextureHandle CreateTexture(uint32 width, uint32 height, const uint8* rgba_pixels, bool srgb = true,
                                bool generate_mips = true, const String& name = {});
    TextureHandle LoadTexture(const String& path, bool srgb = true);
    void DestroyTexture(TextureHandle texture);
    TextureHandle GetWhiteTexture() const { return white_texture_; }
    TextureHandle GetSoftParticleTexture() const { return particle_texture_; }

    // ------------------------------------------------------------------
    // Frame submission
    // ------------------------------------------------------------------
    void BeginFrame(const CameraParams& camera, const EnvironmentSettings& environment);

    void DrawMesh(MeshHandle mesh, const Math::Matrix4& transform, const Material& material = {});
    void DrawParticles(const Particles::ParticleEmitter& emitter);
    void DrawParticles(const Particles::ParticleSystem& system);

    void DrawLine(const Math::Vector3& a, const Math::Vector3& b, const Math::Vector4& color);
    void DrawWireBox(const AABB& box, const Math::Vector4& color);
    void DrawWireSphere(const Math::Vector3& center, float32 radius, const Math::Vector4& color, uint32 segments = 24);
    void DrawGrid(const Math::Vector3& center, float32 size, float32 spacing, const Math::Vector4& color);

    // UI commands are in logical (window) pixels; scaled to the framebuffer automatically
    void DrawUI(const Array<UI::DrawCommand>& commands, float32 logical_width, float32 logical_height);

    void Render();

    // Captures the next rendered frame; call FinishScreenshot() after GraphicsDevice::EndFrame()
    bool RequestScreenshot(const String& png_path);
    bool HasPendingScreenshot() const { return screenshot_.recorded; }
    bool FinishScreenshot();

    // ------------------------------------------------------------------
    // Utilities
    // ------------------------------------------------------------------
    const FontAtlas& GetFont() const { return font_; }
    float32 MeasureText(const String& text, float32 pixel_size) const { return font_.MeasureText(text, pixel_size); }
    // Statistics of the last rendered frame
    const RenderStats& GetStats() const { return last_stats_; }
    VkExtent2D GetFramebufferExtent() const;

private:
    struct GpuMesh {
        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
        VkBuffer index_buffer = VK_NULL_HANDLE;
        VkDeviceMemory index_memory = VK_NULL_HANDLE;
        uint32 index_count = 0;
        AABB bounds;
        String name;
        bool alive = false;
        // Dynamic meshes: one mapped vertex buffer per frame in flight
        uint32 vertex_count = 0;
        Array<VkBuffer> frame_buffers;
        Array<VkDeviceMemory> frame_memory;
        Array<void*> frame_mapped;
    };
    VkBuffer GetVertexBuffer(const GpuMesh& mesh) const;
    uint32 StoreMesh(GpuMesh&& mesh);

    struct GpuTexture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        uint32 width = 0;
        uint32 height = 0;
        String name;
        bool alive = false;
    };

    struct DrawItem {
        uint32 mesh = 0;
        Math::Matrix4 transform;
        Material material;
    };

    struct ParticleBatch {
        uint32 first_vertex = 0;
        uint32 vertex_count = 0;
        uint32 texture = 0;
        bool additive = false;
    };

    struct UIBatch {
        uint32 first_vertex = 0;
        uint32 vertex_count = 0;
        uint32 texture = 0;     // 0 = font atlas
        bool rgba = false;      // true = sample all channels, false = alpha from red (font/solid)
    };

    struct ParticleVertex {
        Math::Vector3 position;
        Math::Vector2 tex_coord;
        Math::Vector4 color;
    };

    struct LineVertex {
        Math::Vector3 position;
        Math::Vector4 color;
    };

    struct UIVertex {
        Math::Vector2 position;
        Math::Vector2 tex_coord;
        Math::Vector4 color;
    };

    // Host-visible buffer that grows on demand (one per frame in flight)
    struct DynamicBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize capacity = 0;
    };

    struct FrameResources {
        DynamicBuffer uniforms;
        DynamicBuffer particles;
        DynamicBuffer lines;
        DynamicBuffer ui;
        VkDescriptorSet frame_set = VK_NULL_HANDLE;
    };

    struct PendingDestroy {
        uint64 frame = 0;
        Function<void()> destroy;
    };

    // Setup
    bool CreateDescriptorLayouts();
    bool CreateSamplers();
    bool CreateShadowResources();
    bool CreateMainRenderPass();
    bool CreateDepthResources();
    bool CreateFramebuffers();
    bool CreatePipelines();
    bool CreateFrameResources();
    bool CreateDefaultTextures();
    void DestroySwapChainResources();
    void RecreateSwapChainResources();

    VkPipeline CreatePipeline(const uint32_t* vert, size_t vert_size, const uint32_t* frag, size_t frag_size,
                              VkRenderPass pass, const VkPipelineVertexInputStateCreateInfo& vertex_input,
                              VkPrimitiveTopology topology, VkCullModeFlags cull, bool depth_test, bool depth_write,
                              int blend_mode, bool depth_bias);

    bool EnsureCapacity(DynamicBuffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage);
    void DestroyDynamicBuffer(DynamicBuffer& buffer);
    void ProcessPendingDestroys(bool force_all);
    void DeferDestroy(Function<void()> destroy);

    const GpuMesh* GetMesh(uint32 id) const;
    VkDescriptorSet GetTextureDescriptor(uint32 texture_id) const;

    // Per frame helpers
    void UpdateFrameUniforms(FrameResources& frame);
    Math::Matrix4 ComputeLightViewProjection() const;
    void BuildUIGeometry(const Array<UI::DrawCommand>& commands, float32 scale_x, float32 scale_y);
    void RecordShadowPass(VkCommandBuffer cmd, FrameResources& frame);
    void RecordMainPass(VkCommandBuffer cmd, FrameResources& frame);

    HAL::GraphicsDevice* device_ = nullptr;
    SceneRendererConfig config_;
    uint64 frame_counter_ = 0;
    float64 start_time_ = 0.0;
    uint64 swap_chain_generation_ = 0;

    // Descriptors
    VkDescriptorSetLayout frame_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout texture_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;

    VkSampler linear_sampler_ = VK_NULL_HANDLE;
    VkSampler shadow_sampler_ = VK_NULL_HANDLE;

    // Shadow map
    VkFormat shadow_format_ = VK_FORMAT_UNDEFINED;
    VkImage shadow_image_ = VK_NULL_HANDLE;
    VkDeviceMemory shadow_memory_ = VK_NULL_HANDLE;
    VkImageView shadow_view_ = VK_NULL_HANDLE;
    VkRenderPass shadow_pass_ = VK_NULL_HANDLE;
    VkFramebuffer shadow_framebuffer_ = VK_NULL_HANDLE;

    // Main pass
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    VkRenderPass main_pass_ = VK_NULL_HANDLE;
    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;
    Array<VkFramebuffer> framebuffers_;

    // Pipelines
    VkPipeline lit_pipeline_ = VK_NULL_HANDLE;
    VkPipeline lit_double_sided_pipeline_ = VK_NULL_HANDLE;
    VkPipeline lit_transparent_pipeline_ = VK_NULL_HANDLE;
    VkPipeline shadow_pipeline_ = VK_NULL_HANDLE;
    VkPipeline sky_pipeline_ = VK_NULL_HANDLE;
    VkPipeline particle_alpha_pipeline_ = VK_NULL_HANDLE;
    VkPipeline particle_additive_pipeline_ = VK_NULL_HANDLE;
    VkPipeline line_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ui_pipeline_ = VK_NULL_HANDLE;

    Array<FrameResources> frames_;

    // Resources
    Array<GpuMesh> meshes_;
    Array<uint32> free_meshes_;
    Array<GpuTexture> textures_;
    Array<uint32> free_textures_;
    Array<PendingDestroy> pending_destroys_;
    TextureHandle white_texture_;
    TextureHandle particle_texture_;
    TextureHandle font_texture_;
    FontAtlas font_;

    // Current frame
    bool frame_begun_ = false;
    CameraParams camera_;
    EnvironmentSettings environment_;
    Math::Matrix4 light_view_projection_;
    Array<DrawItem> opaque_items_;
    Array<DrawItem> transparent_items_;
    Array<ParticleVertex> particle_vertices_;
    Array<ParticleBatch> particle_batches_;
    Array<LineVertex> line_vertices_;
    Array<UIVertex> ui_vertices_;
    Array<UIBatch> ui_batches_;
    Array<GlyphQuad> glyph_scratch_;

    struct Screenshot {
        String path;
        bool requested = false;
        bool recorded = false;
        uint32 width = 0;
        uint32 height = 0;
        DynamicBuffer buffer;
    } screenshot_;
    void RecordScreenshotCopy(VkCommandBuffer cmd);
    RenderStats stats_;
    RenderStats last_stats_;
};

} // namespace DRX::Renderer

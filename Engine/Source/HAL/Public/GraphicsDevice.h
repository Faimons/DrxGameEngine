#pragma once

/**
 * DRX Engine - Graphics Device Interface
 * Vulkan-based graphics abstraction
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Platform.h"
#include "Window.h"

#include <vulkan/vulkan.h>

namespace DRX::HAL {

// ============================================================================
// Forward Declarations
// ============================================================================

class Window;

// ============================================================================
// Graphics Configuration
// ============================================================================

struct GraphicsConfig {
    bool enable_validation = true;
    bool enable_ray_tracing = false;
    uint32 max_frames_in_flight = 2;
    bool vsync = true;
};

// ============================================================================
// Queue Family Indices
// ============================================================================

struct QueueFamilyIndices {
    Optional<uint32> graphics_family;
    Optional<uint32> present_family;
    Optional<uint32> compute_family;
    Optional<uint32> transfer_family;

    bool IsComplete() const {
        return graphics_family.has_value() && present_family.has_value();
    }
};

// ============================================================================
// Swap Chain Support Details
// ============================================================================

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    Array<VkSurfaceFormatKHR> formats;
    Array<VkPresentModeKHR> present_modes;
};

// ============================================================================
// Graphics Device
// ============================================================================

class DRX_API GraphicsDevice : public NonCopyable {
public:
    GraphicsDevice();
    ~GraphicsDevice();

    bool Initialize(Window* window, const GraphicsConfig& config);
    void Shutdown();

    // Frame management
    bool BeginFrame();
    void EndFrame();

    void WaitIdle();

    // Getters
    VkInstance GetInstance() const { return instance_; }
    VkPhysicalDevice GetPhysicalDevice() const { return physical_device_; }
    VkDevice GetDevice() const { return device_; }
    VkQueue GetGraphicsQueue() const { return graphics_queue_; }
    VkQueue GetPresentQueue() const { return present_queue_; }
    VkCommandPool GetCommandPool() const { return command_pool_; }

    VkSwapchainKHR GetSwapChain() const { return swap_chain_; }
    VkFormat GetSwapChainFormat() const { return swap_chain_format_; }
    VkExtent2D GetSwapChainExtent() const { return swap_chain_extent_; }
    const Array<VkImageView>& GetSwapChainImageViews() const { return swap_chain_image_views_; }

    uint32 GetCurrentFrameIndex() const { return current_frame_; }
    uint32 GetMaxFramesInFlight() const { return max_frames_in_flight_; }
    // Incremented every time the swap chain (and its image views) is recreated
    uint64 GetSwapChainGeneration() const { return swap_chain_generation_; }
    // Requests a swap chain rebuild at the next present (window resize)
    void NotifyResized() { framebuffer_resized_ = true; }
    uint32 GetCurrentImageIndex() const { return current_image_index_; }
    VkCommandBuffer GetCurrentCommandBuffer() const { return command_buffers_[current_frame_]; }

    const QueueFamilyIndices& GetQueueFamilies() const { return queue_families_; }

    // Swap chain recreation
    void RecreateSwapChain();

    // Utilities
    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer command_buffer);

    uint32 FindMemoryType(uint32 type_filter, VkMemoryPropertyFlags properties);

private:
    bool CreateInstance();
    bool SetupDebugMessenger();
    bool CreateSurface();
    bool PickPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateSwapChain();
    bool CreateImageViews();
    bool CreateCommandPool();
    bool CreateCommandBuffers();
    bool CreateSyncObjects();

    void CleanupSwapChain();

    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
    SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
    bool IsDeviceSuitable(VkPhysicalDevice device);
    bool CheckDeviceExtensionSupport(VkPhysicalDevice device);

    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const Array<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR ChooseSwapPresentMode(const Array<VkPresentModeKHR>& modes);
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    // Core Vulkan objects
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    // Queues
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    QueueFamilyIndices queue_families_;

    // Swap chain
    VkSwapchainKHR swap_chain_ = VK_NULL_HANDLE;
    Array<VkImage> swap_chain_images_;
    Array<VkImageView> swap_chain_image_views_;
    VkFormat swap_chain_format_;
    VkExtent2D swap_chain_extent_;

    // Command buffers
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    Array<VkCommandBuffer> command_buffers_;

    // Synchronization
    Array<VkSemaphore> image_available_semaphores_;
    Array<VkSemaphore> render_finished_semaphores_;
    Array<VkFence> in_flight_fences_;

    // State
    uint32 current_frame_ = 0;
    uint32 current_image_index_ = 0;
    uint32 max_frames_in_flight_ = 2;
    uint64 swap_chain_generation_ = 0;
    bool framebuffer_resized_ = false;

    // Configuration
    GraphicsConfig config_;
    Window* window_ = nullptr;
};

} // namespace DRX::HAL

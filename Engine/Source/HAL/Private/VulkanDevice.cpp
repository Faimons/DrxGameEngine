/**
 * DRX Engine - Vulkan Graphics Device Implementation
 */

#include "HAL/Public/GraphicsDevice.h"
#include "Core/Public/Log.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <set>
#include <algorithm>
#include <limits>

namespace DRX::HAL {

namespace {
    const Array<const char*> kValidationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    const Array<const char*> kDeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
        void* user_data)
    {
        DRX_UNUSED(type);
        DRX_UNUSED(user_data);

        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            DRX_LOG_ERROR("Vulkan: %s", callback_data->pMessage);
        } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            DRX_LOG_WARNING("Vulkan: %s", callback_data->pMessage);
        } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
            DRX_LOG_DEBUG("Vulkan: %s", callback_data->pMessage);
        }
        return VK_FALSE;
    }

    VkResult CreateDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDebugUtilsMessengerEXT* pDebugMessenger)
    {
        auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (func != nullptr) {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        }
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    void DestroyDebugUtilsMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT debugMessenger,
        const VkAllocationCallbacks* pAllocator)
    {
        auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (func != nullptr) {
            func(instance, debugMessenger, pAllocator);
        }
    }
}

GraphicsDevice::GraphicsDevice() {}

GraphicsDevice::~GraphicsDevice() {
    Shutdown();
}

bool GraphicsDevice::Initialize(Window* window, const GraphicsConfig& config) {
    window_ = window;
    config_ = config;
    max_frames_in_flight_ = config.max_frames_in_flight;

    if (!CreateInstance()) return false;
    if (config_.enable_validation && !SetupDebugMessenger()) return false;
    if (!CreateSurface()) return false;
    if (!PickPhysicalDevice()) return false;
    if (!CreateLogicalDevice()) return false;
    if (!CreateSwapChain()) return false;
    if (!CreateImageViews()) return false;
    if (!CreateCommandPool()) return false;
    if (!CreateCommandBuffers()) return false;
    if (!CreateSyncObjects()) return false;

    DRX_LOG_INFO("Vulkan graphics device initialized");
    return true;
}

void GraphicsDevice::Shutdown() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);

        CleanupSwapChain();

        for (size_t i = 0; i < max_frames_in_flight_; ++i) {
            vkDestroySemaphore(device_, render_finished_semaphores_[i], nullptr);
            vkDestroySemaphore(device_, image_available_semaphores_[i], nullptr);
            vkDestroyFence(device_, in_flight_fences_[i], nullptr);
        }

        vkDestroyCommandPool(device_, command_pool_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }

    if (debug_messenger_ != VK_NULL_HANDLE) {
        DestroyDebugUtilsMessengerEXT(instance_, debug_messenger_, nullptr);
    }

    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }

    DRX_LOG_INFO("Vulkan graphics device shutdown");
}

bool GraphicsDevice::CreateInstance() {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "DRX Engine Application";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "DRX Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;

    auto extensions = Window::GetRequiredVulkanExtensions();

    if (config_.enable_validation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
    if (config_.enable_validation) {
        create_info.enabledLayerCount = static_cast<uint32>(kValidationLayers.size());
        create_info.ppEnabledLayerNames = kValidationLayers.data();

        debug_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug_create_info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug_create_info.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug_create_info.pfnUserCallback = DebugCallback;

        create_info.pNext = &debug_create_info;
    } else {
        create_info.enabledLayerCount = 0;
    }

    VkResult result = vkCreateInstance(&create_info, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create Vulkan instance: %d", result);
        return false;
    }

    DRX_LOG_INFO("Vulkan instance created");
    return true;
}

bool GraphicsDevice::SetupDebugMessenger() {
    VkDebugUtilsMessengerCreateInfoEXT create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info.pfnUserCallback = DebugCallback;

    if (CreateDebugUtilsMessengerEXT(instance_, &create_info, nullptr, &debug_messenger_) != VK_SUCCESS) {
        DRX_LOG_WARNING("Failed to set up debug messenger");
        return false;
    }

    return true;
}

bool GraphicsDevice::CreateSurface() {
    VkResult result = glfwCreateWindowSurface(instance_, window_->GetNativeHandle(), nullptr, &surface_);
    if (result != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create window surface: %d", result);
        return false;
    }
    return true;
}

bool GraphicsDevice::PickPhysicalDevice() {
    uint32 device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);

    if (device_count == 0) {
        DRX_LOG_ERROR("No Vulkan-capable GPU found");
        return false;
    }

    Array<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());

    for (const auto& device : devices) {
        if (IsDeviceSuitable(device)) {
            physical_device_ = device;
            break;
        }
    }

    if (physical_device_ == VK_NULL_HANDLE) {
        DRX_LOG_ERROR("No suitable GPU found");
        return false;
    }

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical_device_, &properties);
    DRX_LOG_INFO("Selected GPU: %s", properties.deviceName);

    queue_families_ = FindQueueFamilies(physical_device_);

    return true;
}

bool GraphicsDevice::IsDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = FindQueueFamilies(device);

    bool extensions_supported = CheckDeviceExtensionSupport(device);

    bool swap_chain_adequate = false;
    if (extensions_supported) {
        SwapChainSupportDetails swap_chain_support = QuerySwapChainSupport(device);
        swap_chain_adequate = !swap_chain_support.formats.empty() && !swap_chain_support.present_modes.empty();
    }

    return indices.IsComplete() && extensions_supported && swap_chain_adequate;
}

bool GraphicsDevice::CheckDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32 extension_count;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);

    Array<VkExtensionProperties> available(extension_count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available.data());

    std::set<String> required(kDeviceExtensions.begin(), kDeviceExtensions.end());

    for (const auto& extension : available) {
        required.erase(extension.extensionName);
    }

    return required.empty();
}

QueueFamilyIndices GraphicsDevice::FindQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32 family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);

    Array<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());

    for (uint32 i = 0; i < family_count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics_family = i;
        }

        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &present_support);
        if (present_support) {
            indices.present_family = i;
        }

        if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            indices.compute_family = i;
        }

        if (families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
            indices.transfer_family = i;
        }

        if (indices.IsComplete()) {
            break;
        }
    }

    return indices;
}

SwapChainSupportDetails GraphicsDevice::QuerySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities);

    uint32 format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &format_count, nullptr);
    if (format_count != 0) {
        details.formats.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &format_count, details.formats.data());
    }

    uint32 mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &mode_count, nullptr);
    if (mode_count != 0) {
        details.present_modes.resize(mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &mode_count, details.present_modes.data());
    }

    return details;
}

bool GraphicsDevice::CreateLogicalDevice() {
    std::set<uint32> unique_families = {
        queue_families_.graphics_family.value(),
        queue_families_.present_family.value()
    };

    Array<VkDeviceQueueCreateInfo> queue_create_infos;
    float queue_priority = 1.0f;

    for (uint32 family : unique_families) {
        VkDeviceQueueCreateInfo queue_info{};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &queue_priority;
        queue_create_infos.push_back(queue_info);
    }

    VkPhysicalDeviceFeatures device_features{};
    device_features.samplerAnisotropy = VK_TRUE;
    device_features.fillModeNonSolid = VK_TRUE;

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = static_cast<uint32>(queue_create_infos.size());
    create_info.pQueueCreateInfos = queue_create_infos.data();
    create_info.pEnabledFeatures = &device_features;
    create_info.enabledExtensionCount = static_cast<uint32>(kDeviceExtensions.size());
    create_info.ppEnabledExtensionNames = kDeviceExtensions.data();

    if (config_.enable_validation) {
        create_info.enabledLayerCount = static_cast<uint32>(kValidationLayers.size());
        create_info.ppEnabledLayerNames = kValidationLayers.data();
    } else {
        create_info.enabledLayerCount = 0;
    }

    if (vkCreateDevice(physical_device_, &create_info, nullptr, &device_) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create logical device");
        return false;
    }

    vkGetDeviceQueue(device_, queue_families_.graphics_family.value(), 0, &graphics_queue_);
    vkGetDeviceQueue(device_, queue_families_.present_family.value(), 0, &present_queue_);

    DRX_LOG_INFO("Vulkan logical device created");
    return true;
}

VkSurfaceFormatKHR GraphicsDevice::ChooseSwapSurfaceFormat(const Array<VkSurfaceFormatKHR>& formats) {
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats[0];
}

VkPresentModeKHR GraphicsDevice::ChooseSwapPresentMode(const Array<VkPresentModeKHR>& modes) {
    if (!config_.vsync) {
        for (const auto& mode : modes) {
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                return mode;
            }
        }
    }

    for (const auto& mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D GraphicsDevice::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D extent = {
        window_->GetWidth(),
        window_->GetHeight()
    };

    extent.width = std::clamp(extent.width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height);

    return extent;
}

bool GraphicsDevice::CreateSwapChain() {
    SwapChainSupportDetails support = QuerySwapChainSupport(physical_device_);

    VkSurfaceFormatKHR surface_format = ChooseSwapSurfaceFormat(support.formats);
    VkPresentModeKHR present_mode = ChooseSwapPresentMode(support.present_modes);
    VkExtent2D extent = ChooseSwapExtent(support.capabilities);

    uint32 image_count = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && image_count > support.capabilities.maxImageCount) {
        image_count = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface_;
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32 queue_family_indices[] = {
        queue_families_.graphics_family.value(),
        queue_families_.present_family.value()
    };

    if (queue_families_.graphics_family != queue_families_.present_family) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_family_indices;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    create_info.preTransform = support.capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device_, &create_info, nullptr, &swap_chain_) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create swap chain");
        return false;
    }

    vkGetSwapchainImagesKHR(device_, swap_chain_, &image_count, nullptr);
    swap_chain_images_.resize(image_count);
    vkGetSwapchainImagesKHR(device_, swap_chain_, &image_count, swap_chain_images_.data());

    swap_chain_format_ = surface_format.format;
    swap_chain_extent_ = extent;

    DRX_LOG_INFO("Swap chain created: %ux%u, %u images", extent.width, extent.height, image_count);
    return true;
}

bool GraphicsDevice::CreateImageViews() {
    swap_chain_image_views_.resize(swap_chain_images_.size());

    for (size_t i = 0; i < swap_chain_images_.size(); ++i) {
        VkImageViewCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        create_info.image = swap_chain_images_[i];
        create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        create_info.format = swap_chain_format_;
        create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        create_info.subresourceRange.baseMipLevel = 0;
        create_info.subresourceRange.levelCount = 1;
        create_info.subresourceRange.baseArrayLayer = 0;
        create_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &create_info, nullptr, &swap_chain_image_views_[i]) != VK_SUCCESS) {
            DRX_LOG_ERROR("Failed to create image view");
            return false;
        }
    }

    return true;
}

bool GraphicsDevice::CreateCommandPool() {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_families_.graphics_family.value();

    if (vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to create command pool");
        return false;
    }

    return true;
}

bool GraphicsDevice::CreateCommandBuffers() {
    command_buffers_.resize(max_frames_in_flight_);

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = static_cast<uint32>(command_buffers_.size());

    if (vkAllocateCommandBuffers(device_, &alloc_info, command_buffers_.data()) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to allocate command buffers");
        return false;
    }

    return true;
}

bool GraphicsDevice::CreateSyncObjects() {
    image_available_semaphores_.resize(max_frames_in_flight_);
    render_finished_semaphores_.resize(max_frames_in_flight_);
    in_flight_fences_.resize(max_frames_in_flight_);

    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < max_frames_in_flight_; ++i) {
        if (vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_available_semaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &semaphore_info, nullptr, &render_finished_semaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(device_, &fence_info, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) {
            DRX_LOG_ERROR("Failed to create sync objects");
            return false;
        }
    }

    return true;
}

void GraphicsDevice::CleanupSwapChain() {
    for (auto view : swap_chain_image_views_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    swap_chain_image_views_.clear();

    if (swap_chain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swap_chain_, nullptr);
        swap_chain_ = VK_NULL_HANDLE;
    }
}

void GraphicsDevice::RecreateSwapChain() {
    int width = 0, height = 0;
    while (width == 0 || height == 0) {
        width = window_->GetWidth();
        height = window_->GetHeight();
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(device_);

    CleanupSwapChain();

    CreateSwapChain();
    CreateImageViews();
}

bool GraphicsDevice::BeginFrame() {
    vkWaitForFences(device_, 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        device_,
        swap_chain_,
        UINT64_MAX,
        image_available_semaphores_[current_frame_],
        VK_NULL_HANDLE,
        &current_image_index_
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapChain();
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        DRX_LOG_ERROR("Failed to acquire swap chain image");
        return false;
    }

    vkResetFences(device_, 1, &in_flight_fences_[current_frame_]);
    vkResetCommandBuffer(command_buffers_[current_frame_], 0);

    return true;
}

void GraphicsDevice::EndFrame() {
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore wait_semaphores[] = {image_available_semaphores_[current_frame_]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffers_[current_frame_];

    VkSemaphore signal_semaphores[] = {render_finished_semaphores_[current_frame_]};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    if (vkQueueSubmit(graphics_queue_, 1, &submit_info, in_flight_fences_[current_frame_]) != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to submit draw command buffer");
    }

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;

    VkSwapchainKHR swap_chains[] = {swap_chain_};
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swap_chains;
    present_info.pImageIndices = &current_image_index_;

    VkResult result = vkQueuePresentKHR(present_queue_, &present_info);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebuffer_resized_) {
        framebuffer_resized_ = false;
        RecreateSwapChain();
    } else if (result != VK_SUCCESS) {
        DRX_LOG_ERROR("Failed to present swap chain image");
    }

    current_frame_ = (current_frame_ + 1) % max_frames_in_flight_;
}

void GraphicsDevice::WaitIdle() {
    vkDeviceWaitIdle(device_);
}

VkCommandBuffer GraphicsDevice::BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = command_pool_;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    vkAllocateCommandBuffers(device_, &alloc_info, &command_buffer);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(command_buffer, &begin_info);

    return command_buffer;
}

void GraphicsDevice::EndSingleTimeCommands(VkCommandBuffer command_buffer) {
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue_);

    vkFreeCommandBuffers(device_, command_pool_, 1, &command_buffer);
}

uint32 GraphicsDevice::FindMemoryType(uint32 type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_properties);

    for (uint32 i = 0; i < mem_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) &&
            (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    DRX_LOG_ERROR("Failed to find suitable memory type");
    return 0;
}

} // namespace DRX::HAL

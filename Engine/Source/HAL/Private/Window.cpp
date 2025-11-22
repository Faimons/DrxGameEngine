/**
 * DRX Engine - Window Implementation
 */

#include "HAL/Public/Window.h"
#include "Core/Public/Log.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace DRX::HAL {

bool Window::glfw_initialized_ = false;

bool Window::InitializeGLFW() {
    if (glfw_initialized_) return true;

    if (!glfwInit()) {
        DRX_LOG_ERROR("Failed to initialize GLFW");
        return false;
    }

    glfwSetErrorCallback([](int error, const char* description) {
        DRX_LOG_ERROR("GLFW Error %d: %s", error, description);
    });

    glfw_initialized_ = true;
    DRX_LOG_INFO("GLFW initialized: %s", glfwGetVersionString());
    return true;
}

void Window::ShutdownGLFW() {
    if (glfw_initialized_) {
        glfwTerminate();
        glfw_initialized_ = false;
        DRX_LOG_INFO("GLFW shutdown");
    }
}

Array<const char*> Window::GetRequiredVulkanExtensions() {
    uint32 count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&count);

    Array<const char*> result;
    for (uint32 i = 0; i < count; ++i) {
        result.push_back(extensions[i]);
    }
    return result;
}

Window::Window() {}

Window::~Window() {
    Shutdown();
}

bool Window::Initialize(const WindowConfig& config) {
    if (!glfw_initialized_) {
        if (!InitializeGLFW()) {
            return false;
        }
    }

    // Configure GLFW for Vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, config.decorated ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, config.maximized ? GLFW_TRUE : GLFW_FALSE);

    // Create window
    GLFWmonitor* monitor = nullptr;
    if (config.fullscreen) {
        monitor = glfwGetPrimaryMonitor();
    }

    window_ = glfwCreateWindow(
        static_cast<int>(config.width),
        static_cast<int>(config.height),
        config.title.c_str(),
        monitor,
        nullptr
    );

    if (!window_) {
        DRX_LOG_ERROR("Failed to create GLFW window");
        return false;
    }

    width_ = config.width;
    height_ = config.height;
    fullscreen_ = config.fullscreen;

    // Set user pointer for callbacks
    glfwSetWindowUserPointer(window_, this);

    // Set callbacks
    glfwSetKeyCallback(window_, GLFWKeyCallback);
    glfwSetMouseButtonCallback(window_, GLFWMouseButtonCallback);
    glfwSetCursorPosCallback(window_, GLFWCursorPosCallback);
    glfwSetScrollCallback(window_, GLFWScrollCallback);
    glfwSetWindowSizeCallback(window_, GLFWWindowSizeCallback);
    glfwSetWindowCloseCallback(window_, GLFWWindowCloseCallback);
    glfwSetWindowFocusCallback(window_, GLFWWindowFocusCallback);
    glfwSetWindowIconifyCallback(window_, GLFWWindowIconifyCallback);

    DRX_LOG_INFO("Window created: %ux%u", width_, height_);
    return true;
}

void Window::Shutdown() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
        DRX_LOG_INFO("Window destroyed");
    }
}

void Window::PollEvents() {
    mouse_delta_x_ = 0;
    mouse_delta_y_ = 0;
    glfwPollEvents();
}

bool Window::ShouldClose() const {
    return window_ && glfwWindowShouldClose(window_);
}

void Window::Close() {
    if (window_) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

bool Window::IsKeyDown(KeyCode key) const {
    return window_ && glfwGetKey(window_, static_cast<int>(key)) == GLFW_PRESS;
}

bool Window::IsMouseButtonDown(MouseButton button) const {
    return window_ && glfwGetMouseButton(window_, static_cast<int>(button)) == GLFW_PRESS;
}

Math::Vector2 Window::GetMousePosition() const {
    double x, y;
    glfwGetCursorPos(window_, &x, &y);
    return Math::Vector2(static_cast<float32>(x), static_cast<float32>(y));
}

Math::Vector2 Window::GetMouseDelta() const {
    return Math::Vector2(static_cast<float32>(mouse_delta_x_), static_cast<float32>(mouse_delta_y_));
}

void Window::SetCursorVisible(bool visible) {
    if (window_) {
        glfwSetInputMode(window_, GLFW_CURSOR,
            visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
    }
}

void Window::SetCursorLocked(bool locked) {
    if (window_) {
        glfwSetInputMode(window_, GLFW_CURSOR,
            locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
}

void Window::SetTitle(const String& title) {
    if (window_) {
        glfwSetWindowTitle(window_, title.c_str());
    }
}

void Window::SetSize(uint32 width, uint32 height) {
    if (window_) {
        glfwSetWindowSize(window_, static_cast<int>(width), static_cast<int>(height));
    }
}

void Window::SetPosition(int32 x, int32 y) {
    if (window_) {
        glfwSetWindowPos(window_, x, y);
    }
}

void Window::Maximize() {
    if (window_) {
        glfwMaximizeWindow(window_);
    }
}

void Window::Minimize() {
    if (window_) {
        glfwIconifyWindow(window_);
    }
}

void Window::Restore() {
    if (window_) {
        glfwRestoreWindow(window_);
    }
}

void Window::SetFullscreen(bool fullscreen) {
    if (window_ && fullscreen_ != fullscreen) {
        GLFWmonitor* monitor = fullscreen ? glfwGetPrimaryMonitor() : nullptr;
        const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());

        if (fullscreen) {
            glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        } else {
            glfwSetWindowMonitor(window_, nullptr, 100, 100, width_, height_, 0);
        }

        fullscreen_ = fullscreen;
    }
}

// GLFW Callbacks
void Window::GLFWKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    DRX_UNUSED(scancode);
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->key_callback_) {
        KeyEvent event;
        event.key = static_cast<KeyCode>(key);
        event.action = static_cast<InputAction>(action);
        event.mods = mods;
        self->key_callback_(event);
    }
}

void Window::GLFWMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->mouse_button_callback_) {
        MouseButtonEvent event;
        event.button = static_cast<MouseButton>(button);
        event.action = static_cast<InputAction>(action);
        event.mods = mods;
        self->mouse_button_callback_(event);
    }
}

void Window::GLFWCursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self) {
        if (self->first_mouse_) {
            self->last_mouse_x_ = xpos;
            self->last_mouse_y_ = ypos;
            self->first_mouse_ = false;
        }

        self->mouse_delta_x_ = xpos - self->last_mouse_x_;
        self->mouse_delta_y_ = ypos - self->last_mouse_y_;
        self->last_mouse_x_ = xpos;
        self->last_mouse_y_ = ypos;

        if (self->mouse_move_callback_) {
            MouseMoveEvent event;
            event.x = xpos;
            event.y = ypos;
            event.delta_x = self->mouse_delta_x_;
            event.delta_y = self->mouse_delta_y_;
            self->mouse_move_callback_(event);
        }
    }
}

void Window::GLFWScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->scroll_callback_) {
        ScrollEvent event;
        event.x_offset = xoffset;
        event.y_offset = yoffset;
        self->scroll_callback_(event);
    }
}

void Window::GLFWWindowSizeCallback(GLFWwindow* window, int width, int height) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self) {
        self->width_ = static_cast<uint32>(width);
        self->height_ = static_cast<uint32>(height);
        self->minimized_ = (width == 0 || height == 0);

        if (self->resize_callback_) {
            ResizeEvent event;
            event.width = self->width_;
            event.height = self->height_;
            self->resize_callback_(event);
        }
    }
}

void Window::GLFWWindowCloseCallback(GLFWwindow* window) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->close_callback_) {
        self->close_callback_();
    }
}

void Window::GLFWWindowFocusCallback(GLFWwindow* window, int focused) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self) {
        self->focused_ = focused != 0;
    }
}

void Window::GLFWWindowIconifyCallback(GLFWwindow* window, int iconified) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self) {
        self->minimized_ = iconified != 0;
    }
}

} // namespace DRX::HAL

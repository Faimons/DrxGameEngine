#pragma once

/**
 * DRX Engine - Window Management
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Platform.h"
#include "Core/Public/Math/Vector2.h"

struct GLFWwindow;

namespace DRX::HAL {

// ============================================================================
// Window Configuration
// ============================================================================

struct WindowConfig {
    String title = "DRX Engine";
    uint32 width = 1280;
    uint32 height = 720;
    bool fullscreen = false;
    bool resizable = true;
    bool vsync = true;
    bool decorated = true;
    bool maximized = false;
};

// ============================================================================
// Window Events
// ============================================================================

enum class KeyCode : int32 {
    Unknown = -1,
    Space = 32,
    Apostrophe = 39,
    Comma = 44,
    Minus = 45,
    Period = 46,
    Slash = 47,

    // Numbers
    D0 = 48, D1 = 49, D2 = 50, D3 = 51, D4 = 52,
    D5 = 53, D6 = 54, D7 = 55, D8 = 56, D9 = 57,

    Semicolon = 59,
    Equal = 61,

    // Letters
    A = 65, B = 66, C = 67, D = 68, E = 69,
    F = 70, G = 71, H = 72, I = 73, J = 74,
    K = 75, L = 76, M = 77, N = 78, O = 79,
    P = 80, Q = 81, R = 82, S = 83, T = 84,
    U = 85, V = 86, W = 87, X = 88, Y = 89, Z = 90,

    LeftBracket = 91,
    Backslash = 92,
    RightBracket = 93,
    GraveAccent = 96,

    // Special keys
    Escape = 256,
    Enter = 257,
    Tab = 258,
    Backspace = 259,
    Insert = 260,
    Delete = 261,
    Right = 262,
    Left = 263,
    Down = 264,
    Up = 265,
    PageUp = 266,
    PageDown = 267,
    Home = 268,
    End = 269,
    CapsLock = 280,
    ScrollLock = 281,
    NumLock = 282,
    PrintScreen = 283,
    Pause = 284,

    // Function keys
    F1 = 290, F2 = 291, F3 = 292, F4 = 293, F5 = 294,
    F6 = 295, F7 = 296, F8 = 297, F9 = 298, F10 = 299,
    F11 = 300, F12 = 301,

    // Modifiers
    LeftShift = 340,
    LeftControl = 341,
    LeftAlt = 342,
    LeftSuper = 343,
    RightShift = 344,
    RightControl = 345,
    RightAlt = 346,
    RightSuper = 347,
    Menu = 348
};

enum class MouseButton : int32 {
    Left = 0,
    Right = 1,
    Middle = 2,
    Button4 = 3,
    Button5 = 4,
    Button6 = 5,
    Button7 = 6,
    Button8 = 7
};

enum class InputAction : int32 {
    Release = 0,
    Press = 1,
    Repeat = 2
};

struct KeyEvent {
    KeyCode key;
    InputAction action;
    int32 mods;
};

struct MouseButtonEvent {
    MouseButton button;
    InputAction action;
    int32 mods;
};

struct MouseMoveEvent {
    float64 x;
    float64 y;
    float64 delta_x;
    float64 delta_y;
};

struct ScrollEvent {
    float64 x_offset;
    float64 y_offset;
};

struct ResizeEvent {
    uint32 width;
    uint32 height;
};

// ============================================================================
// Window Class
// ============================================================================

class DRX_API Window : public NonCopyable {
public:
    Window();
    ~Window();

    bool Initialize(const WindowConfig& config);
    void Shutdown();

    void PollEvents();
    bool ShouldClose() const;
    void Close();

    // Getters
    uint32 GetWidth() const { return width_; }
    uint32 GetHeight() const { return height_; }
    float32 GetAspectRatio() const { return static_cast<float32>(width_) / height_; }
    bool IsMinimized() const { return minimized_; }
    bool IsFocused() const { return focused_; }
    GLFWwindow* GetNativeHandle() const { return window_; }

    // Input state
    bool IsKeyDown(KeyCode key) const;
    bool IsMouseButtonDown(MouseButton button) const;
    Math::Vector2 GetMousePosition() const;
    Math::Vector2 GetMouseDelta() const;

    // Cursor
    void SetCursorVisible(bool visible);
    void SetCursorLocked(bool locked);

    // Window manipulation
    void SetTitle(const String& title);
    void SetSize(uint32 width, uint32 height);
    void SetPosition(int32 x, int32 y);
    void Maximize();
    void Minimize();
    void Restore();
    void SetFullscreen(bool fullscreen);

    // Callbacks
    using KeyCallback = Function<void(const KeyEvent&)>;
    using MouseButtonCallback = Function<void(const MouseButtonEvent&)>;
    using MouseMoveCallback = Function<void(const MouseMoveEvent&)>;
    using ScrollCallback = Function<void(const ScrollEvent&)>;
    using ResizeCallback = Function<void(const ResizeEvent&)>;
    using CloseCallback = Function<void()>;

    void SetKeyCallback(KeyCallback callback) { key_callback_ = callback; }
    void SetMouseButtonCallback(MouseButtonCallback callback) { mouse_button_callback_ = callback; }
    void SetMouseMoveCallback(MouseMoveCallback callback) { mouse_move_callback_ = callback; }
    void SetScrollCallback(ScrollCallback callback) { scroll_callback_ = callback; }
    void SetResizeCallback(ResizeCallback callback) { resize_callback_ = callback; }
    void SetCloseCallback(CloseCallback callback) { close_callback_ = callback; }

    // Static
    static bool InitializeGLFW();
    static void ShutdownGLFW();
    static Array<const char*> GetRequiredVulkanExtensions();

private:
    static void GLFWKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void GLFWMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void GLFWCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void GLFWScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void GLFWWindowSizeCallback(GLFWwindow* window, int width, int height);
    static void GLFWWindowCloseCallback(GLFWwindow* window);
    static void GLFWWindowFocusCallback(GLFWwindow* window, int focused);
    static void GLFWWindowIconifyCallback(GLFWwindow* window, int iconified);

    GLFWwindow* window_ = nullptr;
    uint32 width_ = 0;
    uint32 height_ = 0;
    bool minimized_ = false;
    bool focused_ = true;
    bool fullscreen_ = false;

    float64 last_mouse_x_ = 0;
    float64 last_mouse_y_ = 0;
    float64 mouse_delta_x_ = 0;
    float64 mouse_delta_y_ = 0;
    bool first_mouse_ = true;

    KeyCallback key_callback_;
    MouseButtonCallback mouse_button_callback_;
    MouseMoveCallback mouse_move_callback_;
    ScrollCallback scroll_callback_;
    ResizeCallback resize_callback_;
    CloseCallback close_callback_;

    static bool glfw_initialized_;
};

} // namespace DRX::HAL

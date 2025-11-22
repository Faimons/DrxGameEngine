/**
 * DRX Engine - Input System
 * Keyboard, mouse, and gamepad input handling
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector2.h"

#include <functional>

struct GLFWwindow;

namespace DRX::Input {

/**
 * Key codes (matching GLFW)
 */
enum class KeyCode : int32 {
    Unknown = -1,
    Space = 32,
    Apostrophe = 39,
    Comma = 44,
    Minus = 45,
    Period = 46,
    Slash = 47,

    Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    Semicolon = 59,
    Equal = 61,

    A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    LeftBracket = 91,
    Backslash = 92,
    RightBracket = 93,
    GraveAccent = 96,

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

    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, F25,

    Keypad0 = 320, Keypad1, Keypad2, Keypad3, Keypad4,
    Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
    KeypadDecimal = 330,
    KeypadDivide = 331,
    KeypadMultiply = 332,
    KeypadSubtract = 333,
    KeypadAdd = 334,
    KeypadEnter = 335,
    KeypadEqual = 336,

    LeftShift = 340,
    LeftControl = 341,
    LeftAlt = 342,
    LeftSuper = 343,
    RightShift = 344,
    RightControl = 345,
    RightAlt = 346,
    RightSuper = 347,
    Menu = 348,

    MaxKeys = 349
};

/**
 * Mouse buttons
 */
enum class MouseButton : int32 {
    Left = 0,
    Right = 1,
    Middle = 2,
    Button4 = 3,
    Button5 = 4,
    Button6 = 5,
    Button7 = 6,
    Button8 = 7,
    MaxButtons = 8
};

/**
 * Gamepad buttons (Xbox layout)
 */
enum class GamepadButton : int32 {
    A = 0,          // Cross on PlayStation
    B = 1,          // Circle
    X = 2,          // Square
    Y = 3,          // Triangle
    LeftBumper = 4,
    RightBumper = 5,
    Back = 6,       // Select/Share
    Start = 7,      // Options
    Guide = 8,      // PS/Xbox button
    LeftThumb = 9,
    RightThumb = 10,
    DPadUp = 11,
    DPadRight = 12,
    DPadDown = 13,
    DPadLeft = 14,
    MaxButtons = 15
};

/**
 * Gamepad axes
 */
enum class GamepadAxis : int32 {
    LeftX = 0,
    LeftY = 1,
    RightX = 2,
    RightY = 3,
    LeftTrigger = 4,
    RightTrigger = 5,
    MaxAxes = 6
};

/**
 * Input action types for input binding
 */
enum class InputActionType : uint32 {
    Button,     // Digital on/off
    Axis,       // -1 to 1 value
    Axis2D,     // Vector2 value
    Trigger     // 0 to 1 value
};

/**
 * Input binding source
 */
struct InputBinding {
    enum class Type { Keyboard, MouseButton, MouseAxis, GamepadButton, GamepadAxis };

    Type type;
    int32 code;           // KeyCode, MouseButton, GamepadButton, or axis index
    float32 scale = 1.0f; // For axis scaling/inversion
    float32 deadzone = 0.1f;
};

/**
 * Input action definition
 */
struct InputAction {
    String name;
    InputActionType type;
    Array<InputBinding> bindings;

    // Current state
    bool pressed = false;
    bool just_pressed = false;
    bool just_released = false;
    float32 value = 0.0f;
    Math::Vector2 axis_2d;
};

/**
 * Input callbacks
 */
using KeyCallback = std::function<void(KeyCode key, bool pressed)>;
using MouseButtonCallback = std::function<void(MouseButton button, bool pressed)>;
using MouseMoveCallback = std::function<void(float64 x, float64 y)>;
using MouseScrollCallback = std::function<void(float64 x_offset, float64 y_offset)>;
using CharCallback = std::function<void(uint32 codepoint)>;
using GamepadCallback = std::function<void(int32 gamepad_id, bool connected)>;

/**
 * Gamepad state
 */
struct GamepadState {
    bool connected = false;
    String name;
    bool buttons[static_cast<int32>(GamepadButton::MaxButtons)] = {};
    bool buttons_prev[static_cast<int32>(GamepadButton::MaxButtons)] = {};
    float32 axes[static_cast<int32>(GamepadAxis::MaxAxes)] = {};
};

/**
 * Input System
 */
class InputSystem {
public:
    static InputSystem& Instance();

    bool Initialize(GLFWwindow* window);
    void Shutdown();
    void Update();  // Call at start of frame

    // Keyboard
    bool IsKeyDown(KeyCode key) const;
    bool IsKeyPressed(KeyCode key) const;   // Just pressed this frame
    bool IsKeyReleased(KeyCode key) const;  // Just released this frame
    bool IsAnyKeyDown() const;

    // Mouse
    bool IsMouseButtonDown(MouseButton button) const;
    bool IsMouseButtonPressed(MouseButton button) const;
    bool IsMouseButtonReleased(MouseButton button) const;

    Math::Vector2 GetMousePosition() const;
    Math::Vector2 GetMouseDelta() const;
    Math::Vector2 GetMouseScroll() const;

    void SetMousePosition(float64 x, float64 y);
    void SetCursorMode(bool visible, bool locked = false);
    bool IsCursorVisible() const { return cursor_visible_; }
    bool IsCursorLocked() const { return cursor_locked_; }

    // Gamepad
    bool IsGamepadConnected(int32 gamepad_id = 0) const;
    bool IsGamepadButtonDown(int32 gamepad_id, GamepadButton button) const;
    bool IsGamepadButtonPressed(int32 gamepad_id, GamepadButton button) const;
    bool IsGamepadButtonReleased(int32 gamepad_id, GamepadButton button) const;
    float32 GetGamepadAxis(int32 gamepad_id, GamepadAxis axis) const;
    Math::Vector2 GetGamepadLeftStick(int32 gamepad_id = 0) const;
    Math::Vector2 GetGamepadRightStick(int32 gamepad_id = 0) const;
    float32 GetGamepadLeftTrigger(int32 gamepad_id = 0) const;
    float32 GetGamepadRightTrigger(int32 gamepad_id = 0) const;
    void SetGamepadDeadzone(float32 deadzone) { gamepad_deadzone_ = deadzone; }
    void SetGamepadVibration(int32 gamepad_id, float32 left_motor, float32 right_motor);

    // Input actions (binding system)
    void RegisterAction(const String& name, InputActionType type);
    void BindKey(const String& action, KeyCode key);
    void BindMouseButton(const String& action, MouseButton button);
    void BindMouseAxis(const String& action, bool horizontal);
    void BindGamepadButton(const String& action, GamepadButton button);
    void BindGamepadAxis(const String& action, GamepadAxis axis, float32 scale = 1.0f);

    bool GetAction(const String& name) const;
    bool GetActionPressed(const String& name) const;
    bool GetActionReleased(const String& name) const;
    float32 GetActionValue(const String& name) const;
    Math::Vector2 GetActionAxis2D(const String& name) const;

    void ClearAction(const String& name);
    void ClearAllActions();

    // Callbacks
    void SetKeyCallback(KeyCallback callback) { key_callback_ = callback; }
    void SetMouseButtonCallback(MouseButtonCallback callback) { mouse_button_callback_ = callback; }
    void SetMouseMoveCallback(MouseMoveCallback callback) { mouse_move_callback_ = callback; }
    void SetMouseScrollCallback(MouseScrollCallback callback) { scroll_callback_ = callback; }
    void SetCharCallback(CharCallback callback) { char_callback_ = callback; }
    void SetGamepadCallback(GamepadCallback callback) { gamepad_callback_ = callback; }

    // Text input
    void BeginTextInput();
    void EndTextInput();
    bool IsTextInputActive() const { return text_input_active_; }
    const String& GetTextInput() const { return text_input_buffer_; }
    void ClearTextInput() { text_input_buffer_.clear(); }

private:
    InputSystem() = default;
    ~InputSystem() = default;
    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;

    void UpdateKeyboard();
    void UpdateMouse();
    void UpdateGamepads();
    void UpdateActions();

    float32 ApplyDeadzone(float32 value, float32 deadzone) const;

    // GLFW callbacks
    static void GLFWKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void GLFWMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void GLFWCursorPosCallback(GLFWwindow* window, double x, double y);
    static void GLFWScrollCallback(GLFWwindow* window, double x, double y);
    static void GLFWCharCallback(GLFWwindow* window, unsigned int codepoint);
    static void GLFWJoystickCallback(int jid, int event);

    GLFWwindow* window_ = nullptr;

    // Keyboard state
    bool keys_[static_cast<int32>(KeyCode::MaxKeys)] = {};
    bool keys_prev_[static_cast<int32>(KeyCode::MaxKeys)] = {};

    // Mouse state
    bool mouse_buttons_[static_cast<int32>(MouseButton::MaxButtons)] = {};
    bool mouse_buttons_prev_[static_cast<int32>(MouseButton::MaxButtons)] = {};
    Math::Vector2 mouse_position_;
    Math::Vector2 mouse_position_prev_;
    Math::Vector2 mouse_scroll_;
    bool cursor_visible_ = true;
    bool cursor_locked_ = false;
    bool first_mouse_update_ = true;

    // Gamepad state
    static constexpr int32 MAX_GAMEPADS = 4;
    GamepadState gamepads_[MAX_GAMEPADS];
    float32 gamepad_deadzone_ = 0.15f;

    // Input actions
    HashMap<String, InputAction> actions_;

    // Callbacks
    KeyCallback key_callback_;
    MouseButtonCallback mouse_button_callback_;
    MouseMoveCallback mouse_move_callback_;
    MouseScrollCallback scroll_callback_;
    CharCallback char_callback_;
    GamepadCallback gamepad_callback_;

    // Text input
    bool text_input_active_ = false;
    String text_input_buffer_;

    bool initialized_ = false;
};

} // namespace DRX::Input

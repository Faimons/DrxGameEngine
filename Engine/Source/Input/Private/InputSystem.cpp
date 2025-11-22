/**
 * DRX Engine - Input System Implementation
 */

#include "Input/Public/InputSystem.h"
#include "Core/Public/Log.h"

#include <GLFW/glfw3.h>
#include <cmath>

namespace DRX::Input {

// Static instance for GLFW callbacks
static InputSystem* g_input_system = nullptr;

InputSystem& InputSystem::Instance() {
    static InputSystem instance;
    return instance;
}

bool InputSystem::Initialize(GLFWwindow* window) {
    window_ = window;
    g_input_system = this;

    // Set GLFW callbacks
    glfwSetKeyCallback(window, GLFWKeyCallback);
    glfwSetMouseButtonCallback(window, GLFWMouseButtonCallback);
    glfwSetCursorPosCallback(window, GLFWCursorPosCallback);
    glfwSetScrollCallback(window, GLFWScrollCallback);
    glfwSetCharCallback(window, GLFWCharCallback);
    glfwSetJoystickCallback(GLFWJoystickCallback);

    // Get initial mouse position
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    mouse_position_ = {static_cast<float32>(x), static_cast<float32>(y)};
    mouse_position_prev_ = mouse_position_;

    // Check for connected gamepads
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (glfwJoystickPresent(i)) {
            gamepads_[i].connected = true;
            gamepads_[i].name = glfwGetJoystickName(i);
            DRX_LOG_INFO("Gamepad {} connected: {}", i, gamepads_[i].name);
        }
    }

    initialized_ = true;
    DRX_LOG_INFO("Input system initialized");
    return true;
}

void InputSystem::Shutdown() {
    if (window_) {
        glfwSetKeyCallback(window_, nullptr);
        glfwSetMouseButtonCallback(window_, nullptr);
        glfwSetCursorPosCallback(window_, nullptr);
        glfwSetScrollCallback(window_, nullptr);
        glfwSetCharCallback(window_, nullptr);
    }

    window_ = nullptr;
    g_input_system = nullptr;
    initialized_ = false;

    DRX_LOG_INFO("Input system shutdown");
}

void InputSystem::Update() {
    // Store previous state
    std::memcpy(keys_prev_, keys_, sizeof(keys_));
    std::memcpy(mouse_buttons_prev_, mouse_buttons_, sizeof(mouse_buttons_));
    mouse_position_prev_ = mouse_position_;

    // Reset scroll
    mouse_scroll_ = {0, 0};

    // Update gamepads
    UpdateGamepads();

    // Update input actions
    UpdateActions();

    first_mouse_update_ = false;
}

void InputSystem::UpdateKeyboard() {
    // Keyboard state is updated via callbacks
}

void InputSystem::UpdateMouse() {
    double x, y;
    glfwGetCursorPos(window_, &x, &y);
    mouse_position_ = {static_cast<float32>(x), static_cast<float32>(y)};
}

void InputSystem::UpdateGamepads() {
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (!gamepads_[i].connected) continue;

        // Store previous button state
        std::memcpy(gamepads_[i].buttons_prev, gamepads_[i].buttons, sizeof(gamepads_[i].buttons));

        GLFWgamepadstate state;
        if (glfwGetGamepadState(i, &state)) {
            // Update buttons
            for (int b = 0; b < static_cast<int>(GamepadButton::MaxButtons); b++) {
                gamepads_[i].buttons[b] = state.buttons[b] == GLFW_PRESS;
            }

            // Update axes
            for (int a = 0; a < static_cast<int>(GamepadAxis::MaxAxes); a++) {
                gamepads_[i].axes[a] = state.axes[a];
            }
        }
    }
}

void InputSystem::UpdateActions() {
    for (auto& [name, action] : actions_) {
        bool prev_pressed = action.pressed;
        action.pressed = false;
        action.value = 0.0f;
        action.axis_2d = {0, 0};

        for (const auto& binding : action.bindings) {
            switch (binding.type) {
                case InputBinding::Type::Keyboard: {
                    KeyCode key = static_cast<KeyCode>(binding.code);
                    if (IsKeyDown(key)) {
                        action.pressed = true;
                        action.value = 1.0f;
                    }
                    break;
                }
                case InputBinding::Type::MouseButton: {
                    MouseButton button = static_cast<MouseButton>(binding.code);
                    if (IsMouseButtonDown(button)) {
                        action.pressed = true;
                        action.value = 1.0f;
                    }
                    break;
                }
                case InputBinding::Type::MouseAxis: {
                    Math::Vector2 delta = GetMouseDelta();
                    if (binding.code == 0) {  // Horizontal
                        action.value += delta.x * binding.scale;
                    } else {  // Vertical
                        action.value += delta.y * binding.scale;
                    }
                    break;
                }
                case InputBinding::Type::GamepadButton: {
                    GamepadButton button = static_cast<GamepadButton>(binding.code);
                    if (IsGamepadButtonDown(0, button)) {
                        action.pressed = true;
                        action.value = 1.0f;
                    }
                    break;
                }
                case InputBinding::Type::GamepadAxis: {
                    GamepadAxis axis = static_cast<GamepadAxis>(binding.code);
                    float32 value = GetGamepadAxis(0, axis);
                    value = ApplyDeadzone(value, binding.deadzone);
                    action.value += value * binding.scale;
                    if (std::abs(value) > 0.5f) {
                        action.pressed = true;
                    }
                    break;
                }
            }
        }

        action.just_pressed = action.pressed && !prev_pressed;
        action.just_released = !action.pressed && prev_pressed;
    }
}

bool InputSystem::IsKeyDown(KeyCode key) const {
    int k = static_cast<int>(key);
    return k >= 0 && k < static_cast<int>(KeyCode::MaxKeys) && keys_[k];
}

bool InputSystem::IsKeyPressed(KeyCode key) const {
    int k = static_cast<int>(key);
    return k >= 0 && k < static_cast<int>(KeyCode::MaxKeys) && keys_[k] && !keys_prev_[k];
}

bool InputSystem::IsKeyReleased(KeyCode key) const {
    int k = static_cast<int>(key);
    return k >= 0 && k < static_cast<int>(KeyCode::MaxKeys) && !keys_[k] && keys_prev_[k];
}

bool InputSystem::IsAnyKeyDown() const {
    for (int i = 0; i < static_cast<int>(KeyCode::MaxKeys); i++) {
        if (keys_[i]) return true;
    }
    return false;
}

bool InputSystem::IsMouseButtonDown(MouseButton button) const {
    int b = static_cast<int>(button);
    return b >= 0 && b < static_cast<int>(MouseButton::MaxButtons) && mouse_buttons_[b];
}

bool InputSystem::IsMouseButtonPressed(MouseButton button) const {
    int b = static_cast<int>(button);
    return b >= 0 && b < static_cast<int>(MouseButton::MaxButtons) && mouse_buttons_[b] && !mouse_buttons_prev_[b];
}

bool InputSystem::IsMouseButtonReleased(MouseButton button) const {
    int b = static_cast<int>(button);
    return b >= 0 && b < static_cast<int>(MouseButton::MaxButtons) && !mouse_buttons_[b] && mouse_buttons_prev_[b];
}

Math::Vector2 InputSystem::GetMousePosition() const {
    return mouse_position_;
}

Math::Vector2 InputSystem::GetMouseDelta() const {
    if (first_mouse_update_) return {0, 0};
    return mouse_position_ - mouse_position_prev_;
}

Math::Vector2 InputSystem::GetMouseScroll() const {
    return mouse_scroll_;
}

void InputSystem::SetMousePosition(float64 x, float64 y) {
    glfwSetCursorPos(window_, x, y);
    mouse_position_ = {static_cast<float32>(x), static_cast<float32>(y)};
}

void InputSystem::SetCursorMode(bool visible, bool locked) {
    cursor_visible_ = visible;
    cursor_locked_ = locked;

    if (locked) {
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    } else if (!visible) {
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    } else {
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

bool InputSystem::IsGamepadConnected(int32 gamepad_id) const {
    return gamepad_id >= 0 && gamepad_id < MAX_GAMEPADS && gamepads_[gamepad_id].connected;
}

bool InputSystem::IsGamepadButtonDown(int32 gamepad_id, GamepadButton button) const {
    if (!IsGamepadConnected(gamepad_id)) return false;
    int b = static_cast<int>(button);
    return b >= 0 && b < static_cast<int>(GamepadButton::MaxButtons) && gamepads_[gamepad_id].buttons[b];
}

bool InputSystem::IsGamepadButtonPressed(int32 gamepad_id, GamepadButton button) const {
    if (!IsGamepadConnected(gamepad_id)) return false;
    int b = static_cast<int>(button);
    return b >= 0 && b < static_cast<int>(GamepadButton::MaxButtons) &&
           gamepads_[gamepad_id].buttons[b] && !gamepads_[gamepad_id].buttons_prev[b];
}

bool InputSystem::IsGamepadButtonReleased(int32 gamepad_id, GamepadButton button) const {
    if (!IsGamepadConnected(gamepad_id)) return false;
    int b = static_cast<int>(button);
    return b >= 0 && b < static_cast<int>(GamepadButton::MaxButtons) &&
           !gamepads_[gamepad_id].buttons[b] && gamepads_[gamepad_id].buttons_prev[b];
}

float32 InputSystem::GetGamepadAxis(int32 gamepad_id, GamepadAxis axis) const {
    if (!IsGamepadConnected(gamepad_id)) return 0.0f;
    int a = static_cast<int>(axis);
    if (a < 0 || a >= static_cast<int>(GamepadAxis::MaxAxes)) return 0.0f;
    return gamepads_[gamepad_id].axes[a];
}

Math::Vector2 InputSystem::GetGamepadLeftStick(int32 gamepad_id) const {
    return {
        ApplyDeadzone(GetGamepadAxis(gamepad_id, GamepadAxis::LeftX), gamepad_deadzone_),
        ApplyDeadzone(GetGamepadAxis(gamepad_id, GamepadAxis::LeftY), gamepad_deadzone_)
    };
}

Math::Vector2 InputSystem::GetGamepadRightStick(int32 gamepad_id) const {
    return {
        ApplyDeadzone(GetGamepadAxis(gamepad_id, GamepadAxis::RightX), gamepad_deadzone_),
        ApplyDeadzone(GetGamepadAxis(gamepad_id, GamepadAxis::RightY), gamepad_deadzone_)
    };
}

float32 InputSystem::GetGamepadLeftTrigger(int32 gamepad_id) const {
    float32 value = GetGamepadAxis(gamepad_id, GamepadAxis::LeftTrigger);
    return (value + 1.0f) * 0.5f;  // Convert from -1..1 to 0..1
}

float32 InputSystem::GetGamepadRightTrigger(int32 gamepad_id) const {
    float32 value = GetGamepadAxis(gamepad_id, GamepadAxis::RightTrigger);
    return (value + 1.0f) * 0.5f;
}

void InputSystem::SetGamepadVibration(int32 gamepad_id, float32 left_motor, float32 right_motor) {
    // GLFW doesn't support vibration directly
    // Would need platform-specific implementation
}

void InputSystem::RegisterAction(const String& name, InputActionType type) {
    InputAction action;
    action.name = name;
    action.type = type;
    actions_[name] = action;
}

void InputSystem::BindKey(const String& action, KeyCode key) {
    auto it = actions_.find(action);
    if (it == actions_.end()) return;

    InputBinding binding;
    binding.type = InputBinding::Type::Keyboard;
    binding.code = static_cast<int32>(key);
    it->second.bindings.push_back(binding);
}

void InputSystem::BindMouseButton(const String& action, MouseButton button) {
    auto it = actions_.find(action);
    if (it == actions_.end()) return;

    InputBinding binding;
    binding.type = InputBinding::Type::MouseButton;
    binding.code = static_cast<int32>(button);
    it->second.bindings.push_back(binding);
}

void InputSystem::BindMouseAxis(const String& action, bool horizontal) {
    auto it = actions_.find(action);
    if (it == actions_.end()) return;

    InputBinding binding;
    binding.type = InputBinding::Type::MouseAxis;
    binding.code = horizontal ? 0 : 1;
    it->second.bindings.push_back(binding);
}

void InputSystem::BindGamepadButton(const String& action, GamepadButton button) {
    auto it = actions_.find(action);
    if (it == actions_.end()) return;

    InputBinding binding;
    binding.type = InputBinding::Type::GamepadButton;
    binding.code = static_cast<int32>(button);
    it->second.bindings.push_back(binding);
}

void InputSystem::BindGamepadAxis(const String& action, GamepadAxis axis, float32 scale) {
    auto it = actions_.find(action);
    if (it == actions_.end()) return;

    InputBinding binding;
    binding.type = InputBinding::Type::GamepadAxis;
    binding.code = static_cast<int32>(axis);
    binding.scale = scale;
    it->second.bindings.push_back(binding);
}

bool InputSystem::GetAction(const String& name) const {
    auto it = actions_.find(name);
    return it != actions_.end() && it->second.pressed;
}

bool InputSystem::GetActionPressed(const String& name) const {
    auto it = actions_.find(name);
    return it != actions_.end() && it->second.just_pressed;
}

bool InputSystem::GetActionReleased(const String& name) const {
    auto it = actions_.find(name);
    return it != actions_.end() && it->second.just_released;
}

float32 InputSystem::GetActionValue(const String& name) const {
    auto it = actions_.find(name);
    return it != actions_.end() ? it->second.value : 0.0f;
}

Math::Vector2 InputSystem::GetActionAxis2D(const String& name) const {
    auto it = actions_.find(name);
    return it != actions_.end() ? it->second.axis_2d : Math::Vector2{0, 0};
}

void InputSystem::ClearAction(const String& name) {
    actions_.erase(name);
}

void InputSystem::ClearAllActions() {
    actions_.clear();
}

void InputSystem::BeginTextInput() {
    text_input_active_ = true;
    text_input_buffer_.clear();
}

void InputSystem::EndTextInput() {
    text_input_active_ = false;
}

float32 InputSystem::ApplyDeadzone(float32 value, float32 deadzone) const {
    if (std::abs(value) < deadzone) {
        return 0.0f;
    }
    // Remap to full range
    float32 sign = value > 0 ? 1.0f : -1.0f;
    return sign * (std::abs(value) - deadzone) / (1.0f - deadzone);
}

// GLFW Callbacks
void InputSystem::GLFWKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (!g_input_system || key < 0 || key >= static_cast<int>(KeyCode::MaxKeys)) return;

    bool pressed = action != GLFW_RELEASE;
    g_input_system->keys_[key] = pressed;

    if (g_input_system->key_callback_) {
        g_input_system->key_callback_(static_cast<KeyCode>(key), pressed);
    }
}

void InputSystem::GLFWMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (!g_input_system || button < 0 || button >= static_cast<int>(MouseButton::MaxButtons)) return;

    bool pressed = action != GLFW_RELEASE;
    g_input_system->mouse_buttons_[button] = pressed;

    if (g_input_system->mouse_button_callback_) {
        g_input_system->mouse_button_callback_(static_cast<MouseButton>(button), pressed);
    }
}

void InputSystem::GLFWCursorPosCallback(GLFWwindow* window, double x, double y) {
    if (!g_input_system) return;

    g_input_system->mouse_position_ = {static_cast<float32>(x), static_cast<float32>(y)};

    if (g_input_system->mouse_move_callback_) {
        g_input_system->mouse_move_callback_(x, y);
    }
}

void InputSystem::GLFWScrollCallback(GLFWwindow* window, double x, double y) {
    if (!g_input_system) return;

    g_input_system->mouse_scroll_ = {static_cast<float32>(x), static_cast<float32>(y)};

    if (g_input_system->scroll_callback_) {
        g_input_system->scroll_callback_(x, y);
    }
}

void InputSystem::GLFWCharCallback(GLFWwindow* window, unsigned int codepoint) {
    if (!g_input_system) return;

    if (g_input_system->text_input_active_) {
        // Simple ASCII handling for now
        if (codepoint < 128) {
            g_input_system->text_input_buffer_ += static_cast<char>(codepoint);
        }
    }

    if (g_input_system->char_callback_) {
        g_input_system->char_callback_(codepoint);
    }
}

void InputSystem::GLFWJoystickCallback(int jid, int event) {
    if (!g_input_system || jid < 0 || jid >= MAX_GAMEPADS) return;

    if (event == GLFW_CONNECTED) {
        g_input_system->gamepads_[jid].connected = true;
        g_input_system->gamepads_[jid].name = glfwGetJoystickName(jid);
        DRX_LOG_INFO("Gamepad {} connected: {}", jid, g_input_system->gamepads_[jid].name);

        if (g_input_system->gamepad_callback_) {
            g_input_system->gamepad_callback_(jid, true);
        }
    } else if (event == GLFW_DISCONNECTED) {
        g_input_system->gamepads_[jid].connected = false;
        DRX_LOG_INFO("Gamepad {} disconnected", jid);

        if (g_input_system->gamepad_callback_) {
            g_input_system->gamepad_callback_(jid, false);
        }
    }
}

} // namespace DRX::Input

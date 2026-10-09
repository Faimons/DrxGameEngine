/**
 * DRX Engine - Input System Implementation
 */

#include "Input/Public/InputSystem.h"
#include "Core/Public/Log.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace DRX::Input {

// Static instance for GLFW callbacks
static InputSystem* g_input_system = nullptr;

InputSystem& InputSystem::Instance() {
    static InputSystem instance;
    return instance;
}

bool InputSystem::Initialize(GLFWwindow* window) {
    if (!window) {
        DRX_LOG_ERROR("InputSystem requires a window");
        return false;
    }
    if (initialized_) {
        Shutdown();
    }

    window_ = window;
    g_input_system = this;

    // Chain to callbacks installed earlier (HAL::Window, ImGui) instead of replacing them
    previous_.key = glfwSetKeyCallback(window, GLFWKeyCallback);
    previous_.mouse_button = glfwSetMouseButtonCallback(window, GLFWMouseButtonCallback);
    previous_.cursor_pos = glfwSetCursorPosCallback(window, GLFWCursorPosCallback);
    previous_.scroll = glfwSetScrollCallback(window, GLFWScrollCallback);
    previous_.character = glfwSetCharCallback(window, GLFWCharCallback);
    glfwSetJoystickCallback(GLFWJoystickCallback);

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    mouse_position_ = {static_cast<float32>(x), static_cast<float32>(y)};
    last_cursor_position_ = mouse_position_;
    has_cursor_position_ = true;
    mouse_delta_ = {0, 0};

    for (int i = 0; i < MAX_GAMEPADS; i++) {
        gamepads_[i] = GamepadState{};
        if (glfwJoystickIsGamepad(i)) {
            gamepads_[i].connected = true;
            const char* name = glfwGetGamepadName(i);
            gamepads_[i].name = name ? name : "Gamepad";
            DRX_LOG_INFO("Gamepad {} connected: {}", i, gamepads_[i].name);
        }
    }

    initialized_ = true;
    DRX_LOG_INFO("Input system initialized");
    return true;
}

void InputSystem::Shutdown() {
    if (window_) {
        glfwSetKeyCallback(window_, previous_.key);
        glfwSetMouseButtonCallback(window_, previous_.mouse_button);
        glfwSetCursorPosCallback(window_, previous_.cursor_pos);
        glfwSetScrollCallback(window_, previous_.scroll);
        glfwSetCharCallback(window_, previous_.character);
        glfwSetJoystickCallback(nullptr);
    }

    previous_ = PreviousCallbacks{};
    window_ = nullptr;
    g_input_system = nullptr;
    initialized_ = false;
    std::fill(std::begin(keys_), std::end(keys_), false);
    std::fill(std::begin(keys_prev_), std::end(keys_prev_), false);
    std::fill(std::begin(mouse_buttons_), std::end(mouse_buttons_), false);
    std::fill(std::begin(mouse_buttons_prev_), std::end(mouse_buttons_prev_), false);
    actions_.clear();

    DRX_LOG_INFO("Input system shutdown");
}

void InputSystem::BeginFrame() {
    std::memcpy(keys_prev_, keys_, sizeof(keys_));
    std::memcpy(mouse_buttons_prev_, mouse_buttons_, sizeof(mouse_buttons_));
    for (auto& pad : gamepads_) {
        std::memcpy(pad.buttons_prev, pad.buttons, sizeof(pad.buttons));
    }

    mouse_delta_ = {0, 0};
    mouse_scroll_ = {0, 0};
}

void InputSystem::Update() {
    if (!initialized_) return;

    UpdateGamepads();
    UpdateActions();
}

void InputSystem::UpdateGamepads() {
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (!gamepads_[i].connected) continue;

        GLFWgamepadstate state;
        if (glfwGetGamepadState(i, &state)) {
            for (int b = 0; b < static_cast<int>(GamepadButton::MaxButtons); b++) {
                gamepads_[i].buttons[b] = state.buttons[b] == GLFW_PRESS;
            }
            for (int a = 0; a < static_cast<int>(GamepadAxis::MaxAxes); a++) {
                gamepads_[i].axes[a] = state.axes[a];
            }
        }
    }
}

void InputSystem::UpdateActions() {
    for (auto& [name, action] : actions_) {
        const bool prev_pressed = action.pressed;
        action.pressed = false;
        action.value = 0.0f;
        action.axis_2d = {0, 0};

        for (const auto& binding : action.bindings) {
            float32 contribution = 0.0f;
            switch (binding.type) {
                case InputBinding::Type::Keyboard:
                    contribution = IsKeyDown(static_cast<KeyCode>(binding.code)) ? 1.0f : 0.0f;
                    break;
                case InputBinding::Type::MouseButton:
                    contribution = IsMouseButtonDown(static_cast<MouseButton>(binding.code)) ? 1.0f : 0.0f;
                    break;
                case InputBinding::Type::MouseAxis:
                    contribution = binding.code == 0 ? mouse_delta_.x : mouse_delta_.y;
                    break;
                case InputBinding::Type::GamepadButton:
                    contribution = IsGamepadButtonDown(0, static_cast<GamepadButton>(binding.code)) ? 1.0f : 0.0f;
                    break;
                case InputBinding::Type::GamepadAxis:
                    contribution = ApplyDeadzone(GetGamepadAxis(0, static_cast<GamepadAxis>(binding.code)),
                                                 binding.deadzone);
                    break;
            }

            const float32 scaled = contribution * binding.scale;
            switch (action.type) {
                case InputActionType::Button:
                case InputActionType::Trigger:
                    action.value = std::max(action.value, std::abs(scaled));
                    break;
                case InputActionType::Axis:
                    action.value += scaled;
                    break;
                case InputActionType::Axis2D:
                    if (binding.component == 0) action.axis_2d.x += scaled;
                    else action.axis_2d.y += scaled;
                    break;
            }
        }

        if (action.type == InputActionType::Axis2D) {
            // Digital diagonals must not be faster than straight movement
            const float32 length = action.axis_2d.Length();
            if (length > 1.0f && action.bindings.size() > 0) {
                bool digital_only = std::all_of(action.bindings.begin(), action.bindings.end(),
                    [](const InputBinding& b) { return b.type != InputBinding::Type::MouseAxis; });
                if (digital_only) {
                    action.axis_2d = action.axis_2d / length;
                }
            }
            action.value = action.axis_2d.Length();
        } else if (action.type == InputActionType::Trigger) {
            action.value = std::clamp(action.value, 0.0f, 1.0f);
        }

        action.pressed = action.type == InputActionType::Axis
            ? std::abs(action.value) > 0.5f
            : action.value > 0.5f;
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
    return mouse_delta_;
}

Math::Vector2 InputSystem::GetMouseScroll() const {
    return mouse_scroll_;
}

void InputSystem::SetMousePosition(float64 x, float64 y) {
    if (!window_) return;
    glfwSetCursorPos(window_, x, y);
    mouse_position_ = {static_cast<float32>(x), static_cast<float32>(y)};
    last_cursor_position_ = mouse_position_;  // warping is not mouse movement
}

void InputSystem::SetCursorMode(bool visible, bool locked) {
    if (!window_) return;
    cursor_visible_ = visible;
    cursor_locked_ = locked;

    if (locked) {
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
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
    // GLFW exposes no rumble API; report once so callers know it has no effect
    static bool warned = false;
    if (!warned && IsGamepadConnected(gamepad_id) && (left_motor > 0.0f || right_motor > 0.0f)) {
        DRX_LOG_WARNING("Gamepad vibration is not supported by the GLFW backend");
        warned = true;
    }
}

void InputSystem::RegisterAction(const String& name, InputActionType type) {
    InputAction action;
    action.name = name;
    action.type = type;
    actions_[name] = action;
}

InputBinding* InputSystem::AddBinding(const String& action, InputBinding::Type type, int32 code) {
    auto it = actions_.find(action);
    if (it == actions_.end()) {
        DRX_LOG_WARNING("Cannot bind input to unknown action '{}'", action);
        return nullptr;
    }

    InputBinding binding;
    binding.type = type;
    binding.code = code;
    it->second.bindings.push_back(binding);
    return &it->second.bindings.back();
}

void InputSystem::BindKey(const String& action, KeyCode key) {
    AddBinding(action, InputBinding::Type::Keyboard, static_cast<int32>(key));
}

void InputSystem::BindKeyAxis(const String& action, KeyCode key, float32 scale, int32 component) {
    if (InputBinding* binding = AddBinding(action, InputBinding::Type::Keyboard, static_cast<int32>(key))) {
        binding->scale = scale;
        binding->component = component;
    }
}

void InputSystem::BindMouseButton(const String& action, MouseButton button) {
    AddBinding(action, InputBinding::Type::MouseButton, static_cast<int32>(button));
}

void InputSystem::BindMouseAxis(const String& action, bool horizontal, float32 scale, int32 component) {
    if (InputBinding* binding = AddBinding(action, InputBinding::Type::MouseAxis, horizontal ? 0 : 1)) {
        binding->scale = scale;
        binding->component = component;
    }
}

void InputSystem::BindGamepadButton(const String& action, GamepadButton button) {
    AddBinding(action, InputBinding::Type::GamepadButton, static_cast<int32>(button));
}

void InputSystem::BindGamepadAxis(const String& action, GamepadAxis axis, float32 scale, int32 component) {
    if (InputBinding* binding = AddBinding(action, InputBinding::Type::GamepadAxis, static_cast<int32>(axis))) {
        binding->scale = scale;
        binding->component = component;
    }
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
    InputSystem* self = g_input_system;
    if (self && self->previous_.key) {
        self->previous_.key(window, key, scancode, action, mods);
    }
    if (!self || key < 0 || key >= static_cast<int>(KeyCode::MaxKeys) || action == GLFW_REPEAT) return;

    const bool pressed = action == GLFW_PRESS;
    self->keys_[key] = pressed;

    if (self->text_input_active_ && pressed && key == GLFW_KEY_BACKSPACE && !self->text_input_buffer_.empty()) {
        // Remove one UTF-8 code point
        auto& text = self->text_input_buffer_;
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();
        if (!text.empty()) text.pop_back();
    }

    if (self->key_callback_) {
        self->key_callback_(static_cast<KeyCode>(key), pressed);
    }
}

void InputSystem::GLFWMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    InputSystem* self = g_input_system;
    if (self && self->previous_.mouse_button) {
        self->previous_.mouse_button(window, button, action, mods);
    }
    if (!self || button < 0 || button >= static_cast<int>(MouseButton::MaxButtons)) return;

    const bool pressed = action != GLFW_RELEASE;
    self->mouse_buttons_[button] = pressed;

    if (self->mouse_button_callback_) {
        self->mouse_button_callback_(static_cast<MouseButton>(button), pressed);
    }
}

void InputSystem::GLFWCursorPosCallback(GLFWwindow* window, double x, double y) {
    InputSystem* self = g_input_system;
    if (self && self->previous_.cursor_pos) {
        self->previous_.cursor_pos(window, x, y);
    }
    if (!self) return;

    const Math::Vector2 position = {static_cast<float32>(x), static_cast<float32>(y)};
    if (self->has_cursor_position_) {
        // Accumulate: several cursor events can arrive within one frame
        self->mouse_delta_ += position - self->last_cursor_position_;
    }
    self->last_cursor_position_ = position;
    self->has_cursor_position_ = true;
    self->mouse_position_ = position;

    if (self->mouse_move_callback_) {
        self->mouse_move_callback_(x, y);
    }
}

void InputSystem::GLFWScrollCallback(GLFWwindow* window, double x, double y) {
    InputSystem* self = g_input_system;
    if (self && self->previous_.scroll) {
        self->previous_.scroll(window, x, y);
    }
    if (!self) return;

    self->mouse_scroll_ += Math::Vector2(static_cast<float32>(x), static_cast<float32>(y));

    if (self->scroll_callback_) {
        self->scroll_callback_(x, y);
    }
}

void InputSystem::GLFWCharCallback(GLFWwindow* window, unsigned int codepoint) {
    InputSystem* self = g_input_system;
    if (self && self->previous_.character) {
        self->previous_.character(window, codepoint);
    }
    if (!self) return;

    if (self->text_input_active_ && codepoint >= 32 && codepoint != 127 && codepoint <= 0x10FFFF) {
        auto& out = self->text_input_buffer_;
        if (codepoint < 0x80) {
            out += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            out += static_cast<char>(0xC0 | (codepoint >> 6));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            out += static_cast<char>(0xE0 | (codepoint >> 12));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (codepoint >> 18));
            out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    }

    if (self->char_callback_) {
        self->char_callback_(codepoint);
    }
}

void InputSystem::GLFWJoystickCallback(int jid, int event) {
    if (!g_input_system || jid < 0 || jid >= MAX_GAMEPADS) return;

    if (event == GLFW_CONNECTED) {
        if (!glfwJoystickIsGamepad(jid)) return;  // only devices with a gamepad mapping
        g_input_system->gamepads_[jid].connected = true;
        const char* name = glfwGetGamepadName(jid);
        g_input_system->gamepads_[jid].name = name ? name : "Gamepad";
        DRX_LOG_INFO("Gamepad {} connected: {}", jid, g_input_system->gamepads_[jid].name);

        if (g_input_system->gamepad_callback_) {
            g_input_system->gamepad_callback_(jid, true);
        }
    } else if (event == GLFW_DISCONNECTED) {
        g_input_system->gamepads_[jid] = GamepadState{};
        DRX_LOG_INFO("Gamepad {} disconnected", jid);

        if (g_input_system->gamepad_callback_) {
            g_input_system->gamepad_callback_(jid, false);
        }
    }
}

} // namespace DRX::Input

/**
 * DRX Engine - UI System Implementation
 */

#include "UI/Public/UISystem.h"
#include <algorithm>
#include <cmath>

namespace DRX::UI {

// ============================================================================
// UIContext
// ============================================================================

UIContext& UIContext::Instance() {
    static UIContext instance;
    return instance;
}

void UIContext::Initialize(float32 screen_width, float32 screen_height) {
    screen_width_ = screen_width;
    screen_height_ = screen_height;

    // Initialize default style
    style_.background_color = Math::Vector4(0.2f, 0.2f, 0.2f, 0.9f);
    style_.text_color = Math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    style_.button_color = Math::Vector4(0.3f, 0.3f, 0.3f, 1.0f);
    style_.button_hover_color = Math::Vector4(0.4f, 0.4f, 0.4f, 1.0f);
    style_.button_active_color = Math::Vector4(0.5f, 0.5f, 0.5f, 1.0f);
    style_.input_color = Math::Vector4(0.15f, 0.15f, 0.15f, 1.0f);
    style_.slider_color = Math::Vector4(0.4f, 0.6f, 0.9f, 1.0f);
    style_.checkbox_color = Math::Vector4(0.4f, 0.6f, 0.9f, 1.0f);
    style_.border_color = Math::Vector4(0.5f, 0.5f, 0.5f, 1.0f);
    style_.padding = 8.0f;
    style_.spacing = 4.0f;
    style_.border_radius = 4.0f;
    style_.border_width = 1.0f;
    style_.font_size = 14.0f;
}

void UIContext::Shutdown() {
    draw_commands_.clear();
    id_stack_.clear();
    window_stack_.clear();
}

void UIContext::BeginFrame() {
    draw_commands_.clear();
    hot_id_ = 0;
}

void UIContext::EndFrame() {
    // Process and batch draw commands
    // Submit to renderer
}

void UIContext::Render() {
    // Render all draw commands
    for (const auto& cmd : draw_commands_) {
        switch (cmd.type) {
            case DrawCommandType::Rect:
                // Draw filled rectangle
                break;
            case DrawCommandType::RectOutline:
                // Draw rectangle outline
                break;
            case DrawCommandType::Text:
                // Draw text
                break;
            case DrawCommandType::Image:
                // Draw textured quad
                break;
            case DrawCommandType::Line:
                // Draw line
                break;
            case DrawCommandType::Circle:
                // Draw circle
                break;
            case DrawCommandType::Triangle:
                // Draw triangle
                break;
        }
    }
}

void UIContext::SetMousePosition(float32 x, float32 y) {
    mouse_x_ = x;
    mouse_y_ = y;
}

void UIContext::SetMouseButton(int32 button, bool pressed) {
    if (button >= 0 && button < 3) {
        if (pressed && !mouse_buttons_[button]) {
            mouse_clicked_[button] = true;
        } else {
            mouse_clicked_[button] = false;
        }
        mouse_buttons_[button] = pressed;
    }
}

void UIContext::SetMouseScroll(float32 scroll) {
    mouse_scroll_ = scroll;
}

void UIContext::SetKeyState(int32 key, bool pressed) {
    // Store key state for text input
}

void UIContext::AddCharacter(uint32 codepoint) {
    input_text_ += static_cast<char>(codepoint);
}

bool UIContext::BeginWindow(const String& title, Rect& rect, WindowFlags flags) {
    PushID(title);

    uint32 id = GetID(title);
    WindowState& state = window_states_[id];
    state.id = id;
    state.title = title;
    state.rect = rect;
    state.flags = flags;
    state.visible = true;

    window_stack_.push_back(&state);

    // Draw window background
    DrawRect(rect, style_.background_color);

    // Draw title bar
    if (!(flags & WindowFlags::NoTitleBar)) {
        Rect title_rect = {rect.x, rect.y, rect.width, 24.0f};
        DrawRect(title_rect, Math::Vector4(0.15f, 0.15f, 0.15f, 1.0f));
        DrawText(title, title_rect.x + style_.padding, title_rect.y + 4.0f, style_.text_color);

        // Close button
        if (!(flags & WindowFlags::NoClose)) {
            Rect close_rect = {rect.x + rect.width - 24.0f, rect.y, 24.0f, 24.0f};
            if (ButtonInternal("X", close_rect)) {
                state.visible = false;
            }
        }

        state.content_rect = {
            rect.x + style_.padding,
            rect.y + 24.0f + style_.padding,
            rect.width - style_.padding * 2,
            rect.height - 24.0f - style_.padding * 2
        };
    } else {
        state.content_rect = {
            rect.x + style_.padding,
            rect.y + style_.padding,
            rect.width - style_.padding * 2,
            rect.height - style_.padding * 2
        };
    }

    // Draw border
    DrawRectOutline(rect, style_.border_color);

    cursor_x_ = state.content_rect.x;
    cursor_y_ = state.content_rect.y;

    return state.visible;
}

void UIContext::EndWindow() {
    if (!window_stack_.empty()) {
        window_stack_.pop_back();
    }
    PopID();
}

void UIContext::Text(const String& text) {
    DrawText(text, cursor_x_, cursor_y_, style_.text_color);
    cursor_y_ += style_.font_size + style_.spacing;
}

void UIContext::TextColored(const String& text, const Math::Vector4& color) {
    DrawText(text, cursor_x_, cursor_y_, color);
    cursor_y_ += style_.font_size + style_.spacing;
}

bool UIContext::Button(const String& label) {
    float32 width = GetTextWidth(label) + style_.padding * 2;
    float32 height = style_.font_size + style_.padding * 2;
    Rect rect = {cursor_x_, cursor_y_, width, height};

    bool result = ButtonInternal(label, rect);

    cursor_y_ += height + style_.spacing;
    return result;
}

bool UIContext::ButtonInternal(const String& label, const Rect& rect) {
    uint32 id = GetID(label);

    bool hovered = IsMouseInRect(rect);
    bool pressed = hovered && mouse_buttons_[0];
    bool clicked = hovered && mouse_clicked_[0];

    Math::Vector4 color = style_.button_color;
    if (pressed) {
        color = style_.button_active_color;
        active_id_ = id;
    } else if (hovered) {
        color = style_.button_hover_color;
        hot_id_ = id;
    }

    DrawRect(rect, color);
    DrawRectOutline(rect, style_.border_color);

    float32 text_x = rect.x + (rect.width - GetTextWidth(label)) * 0.5f;
    float32 text_y = rect.y + (rect.height - style_.font_size) * 0.5f;
    DrawText(label, text_x, text_y, style_.text_color);

    return clicked;
}

bool UIContext::Checkbox(const String& label, bool& value) {
    uint32 id = GetID(label);
    float32 box_size = style_.font_size + 4.0f;
    Rect box_rect = {cursor_x_, cursor_y_, box_size, box_size};

    bool hovered = IsMouseInRect(box_rect);
    bool clicked = hovered && mouse_clicked_[0];

    if (clicked) {
        value = !value;
    }

    Math::Vector4 color = style_.input_color;
    if (hovered) {
        color = style_.button_hover_color;
    }

    DrawRect(box_rect, color);
    DrawRectOutline(box_rect, style_.border_color);

    if (value) {
        Rect check_rect = {
            box_rect.x + 4.0f,
            box_rect.y + 4.0f,
            box_rect.width - 8.0f,
            box_rect.height - 8.0f
        };
        DrawRect(check_rect, style_.checkbox_color);
    }

    DrawText(label, cursor_x_ + box_size + style_.spacing, cursor_y_ + 2.0f, style_.text_color);

    cursor_y_ += box_size + style_.spacing;
    return clicked;
}

bool UIContext::SliderFloat(const String& label, float32& value, float32 min_val, float32 max_val) {
    uint32 id = GetID(label);

    float32 slider_width = 150.0f;
    float32 slider_height = style_.font_size;
    Rect slider_rect = {cursor_x_, cursor_y_ + style_.font_size + 2.0f, slider_width, slider_height};

    DrawText(label, cursor_x_, cursor_y_, style_.text_color);

    bool hovered = IsMouseInRect(slider_rect);
    bool dragging = active_id_ == id;

    if (hovered && mouse_buttons_[0]) {
        active_id_ = id;
        dragging = true;
    }

    if (dragging) {
        if (mouse_buttons_[0]) {
            float32 t = (mouse_x_ - slider_rect.x) / slider_rect.width;
            t = std::clamp(t, 0.0f, 1.0f);
            value = min_val + t * (max_val - min_val);
        } else {
            active_id_ = 0;
        }
    }

    // Draw track
    DrawRect(slider_rect, style_.input_color);
    DrawRectOutline(slider_rect, style_.border_color);

    // Draw filled portion
    float32 t = (value - min_val) / (max_val - min_val);
    Rect fill_rect = {slider_rect.x, slider_rect.y, slider_rect.width * t, slider_rect.height};
    DrawRect(fill_rect, style_.slider_color);

    // Draw value
    char value_str[32];
    snprintf(value_str, sizeof(value_str), "%.2f", value);
    DrawText(value_str, slider_rect.x + slider_rect.width + style_.spacing, slider_rect.y, style_.text_color);

    cursor_y_ += slider_height + style_.font_size + style_.spacing * 2;
    return dragging;
}

bool UIContext::SliderInt(const String& label, int32& value, int32 min_val, int32 max_val) {
    float32 fvalue = static_cast<float32>(value);
    bool result = SliderFloat(label, fvalue, static_cast<float32>(min_val), static_cast<float32>(max_val));
    value = static_cast<int32>(fvalue);
    return result;
}

bool UIContext::InputText(const String& label, String& text, uint32 max_length) {
    uint32 id = GetID(label);

    float32 input_width = 200.0f;
    float32 input_height = style_.font_size + style_.padding * 2;
    Rect input_rect = {cursor_x_, cursor_y_ + style_.font_size + 2.0f, input_width, input_height};

    DrawText(label, cursor_x_, cursor_y_, style_.text_color);

    bool hovered = IsMouseInRect(input_rect);
    bool clicked = hovered && mouse_clicked_[0];
    bool focused = active_id_ == id;

    if (clicked) {
        active_id_ = id;
        focused = true;
    }

    Math::Vector4 bg_color = style_.input_color;
    if (focused) {
        bg_color = Math::Vector4(0.2f, 0.2f, 0.2f, 1.0f);
    }

    DrawRect(input_rect, bg_color);
    DrawRectOutline(input_rect, focused ? style_.slider_color : style_.border_color);

    // Handle text input
    if (focused && !input_text_.empty()) {
        if (text.length() < max_length) {
            text += input_text_;
        }
        input_text_.clear();
    }

    DrawText(text, input_rect.x + style_.padding, input_rect.y + style_.padding, style_.text_color);

    // Draw cursor when focused
    if (focused) {
        float32 cursor_x = input_rect.x + style_.padding + GetTextWidth(text);
        DrawRect({cursor_x, input_rect.y + 4.0f, 2.0f, input_height - 8.0f}, style_.text_color);
    }

    cursor_y_ += input_height + style_.font_size + style_.spacing * 2;
    return focused;
}

bool UIContext::InputFloat(const String& label, float32& value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.3f", value);
    String text = buffer;

    bool result = InputText(label, text, 64);

    if (result) {
        value = std::stof(text);
    }

    return result;
}

bool UIContext::ColorEdit(const String& label, Math::Vector4& color) {
    PushID(label);

    DrawText(label, cursor_x_, cursor_y_, style_.text_color);
    cursor_y_ += style_.font_size + style_.spacing;

    float32 prev_cursor = cursor_x_;
    bool changed = false;

    changed |= SliderFloat("R", color.x, 0.0f, 1.0f);
    changed |= SliderFloat("G", color.y, 0.0f, 1.0f);
    changed |= SliderFloat("B", color.z, 0.0f, 1.0f);
    changed |= SliderFloat("A", color.w, 0.0f, 1.0f);

    // Color preview
    Rect preview_rect = {prev_cursor + 200.0f, cursor_y_ - 80.0f, 40.0f, 40.0f};
    DrawRect(preview_rect, color);
    DrawRectOutline(preview_rect, style_.border_color);

    PopID();
    return changed;
}

bool UIContext::Combo(const String& label, int32& current_item, const Array<String>& items) {
    uint32 id = GetID(label);

    float32 combo_width = 150.0f;
    float32 combo_height = style_.font_size + style_.padding * 2;
    Rect combo_rect = {cursor_x_, cursor_y_ + style_.font_size + 2.0f, combo_width, combo_height};

    DrawText(label, cursor_x_, cursor_y_, style_.text_color);

    bool hovered = IsMouseInRect(combo_rect);
    bool clicked = hovered && mouse_clicked_[0];

    ComboState& state = combo_states_[id];

    if (clicked) {
        state.open = !state.open;
    }

    Math::Vector4 bg_color = style_.input_color;
    if (hovered || state.open) {
        bg_color = style_.button_hover_color;
    }

    DrawRect(combo_rect, bg_color);
    DrawRectOutline(combo_rect, style_.border_color);

    String display = current_item >= 0 && current_item < static_cast<int32>(items.size())
        ? items[current_item] : "";
    DrawText(display, combo_rect.x + style_.padding, combo_rect.y + style_.padding, style_.text_color);

    // Draw dropdown arrow
    DrawText("v", combo_rect.x + combo_rect.width - 16.0f, combo_rect.y + style_.padding, style_.text_color);

    bool changed = false;

    if (state.open) {
        float32 dropdown_y = combo_rect.y + combo_rect.height;
        for (size_t i = 0; i < items.size(); ++i) {
            Rect item_rect = {combo_rect.x, dropdown_y, combo_width, combo_height};
            bool item_hovered = IsMouseInRect(item_rect);
            bool item_clicked = item_hovered && mouse_clicked_[0];

            Math::Vector4 item_color = style_.input_color;
            if (item_hovered) {
                item_color = style_.button_hover_color;
            }
            if (static_cast<int32>(i) == current_item) {
                item_color = style_.slider_color;
            }

            DrawRect(item_rect, item_color);
            DrawText(items[i], item_rect.x + style_.padding, item_rect.y + style_.padding, style_.text_color);

            if (item_clicked) {
                current_item = static_cast<int32>(i);
                state.open = false;
                changed = true;
            }

            dropdown_y += combo_height;
        }
    }

    cursor_y_ += combo_height + style_.font_size + style_.spacing * 2;
    return changed;
}

void UIContext::Image(SharedPtr<Renderer::Texture> texture, const Math::Vector2& size) {
    Rect rect = {cursor_x_, cursor_y_, size.x, size.y};

    DrawCommand cmd;
    cmd.type = DrawCommandType::Image;
    cmd.rect = rect;
    cmd.color = Math::Vector4(1, 1, 1, 1);
    cmd.texture = texture;
    draw_commands_.push_back(cmd);

    cursor_y_ += size.y + style_.spacing;
}

void UIContext::ProgressBar(float32 progress, const Math::Vector2& size) {
    Rect rect = {cursor_x_, cursor_y_, size.x, size.y};

    DrawRect(rect, style_.input_color);

    Rect fill_rect = {rect.x, rect.y, rect.width * std::clamp(progress, 0.0f, 1.0f), rect.height};
    DrawRect(fill_rect, style_.slider_color);

    DrawRectOutline(rect, style_.border_color);

    char percent_str[16];
    snprintf(percent_str, sizeof(percent_str), "%.0f%%", progress * 100.0f);
    float32 text_x = rect.x + (rect.width - GetTextWidth(percent_str)) * 0.5f;
    float32 text_y = rect.y + (rect.height - style_.font_size) * 0.5f;
    DrawText(percent_str, text_x, text_y, style_.text_color);

    cursor_y_ += size.y + style_.spacing;
}

void UIContext::Separator() {
    if (!window_stack_.empty()) {
        WindowState* window = window_stack_.back();
        Rect rect = {
            window->content_rect.x,
            cursor_y_,
            window->content_rect.width,
            1.0f
        };
        DrawRect(rect, style_.border_color);
        cursor_y_ += style_.spacing * 2;
    }
}

void UIContext::Spacing() {
    cursor_y_ += style_.spacing * 2;
}

void UIContext::SameLine() {
    cursor_y_ -= style_.font_size + style_.padding * 2 + style_.spacing;
    cursor_x_ += 100.0f;  // Offset to the right
}

void UIContext::PushID(const String& id) {
    id_stack_.push_back(id);
}

void UIContext::PushID(int32 id) {
    id_stack_.push_back(std::to_string(id));
}

void UIContext::PopID() {
    if (!id_stack_.empty()) {
        id_stack_.pop_back();
    }
}

uint32 UIContext::GetID(const String& str) const {
    uint32 hash = 2166136261u;
    for (const auto& id : id_stack_) {
        for (char c : id) {
            hash ^= static_cast<uint32>(c);
            hash *= 16777619u;
        }
    }
    for (char c : str) {
        hash ^= static_cast<uint32>(c);
        hash *= 16777619u;
    }
    return hash;
}

void UIContext::DrawRect(const Rect& rect, const Math::Vector4& color) {
    DrawCommand cmd;
    cmd.type = DrawCommandType::Rect;
    cmd.rect = rect;
    cmd.color = color;
    draw_commands_.push_back(cmd);
}

void UIContext::DrawRectOutline(const Rect& rect, const Math::Vector4& color) {
    DrawCommand cmd;
    cmd.type = DrawCommandType::RectOutline;
    cmd.rect = rect;
    cmd.color = color;
    draw_commands_.push_back(cmd);
}

void UIContext::DrawText(const String& text, float32 x, float32 y, const Math::Vector4& color) {
    DrawCommand cmd;
    cmd.type = DrawCommandType::Text;
    cmd.rect = {x, y, 0, 0};
    cmd.color = color;
    cmd.text = text;
    draw_commands_.push_back(cmd);
}

void UIContext::DrawLine(const Math::Vector2& from, const Math::Vector2& to, const Math::Vector4& color) {
    DrawCommand cmd;
    cmd.type = DrawCommandType::Line;
    cmd.rect = {from.x, from.y, to.x, to.y};
    cmd.color = color;
    draw_commands_.push_back(cmd);
}

void UIContext::DrawCircle(const Math::Vector2& center, float32 radius, const Math::Vector4& color) {
    DrawCommand cmd;
    cmd.type = DrawCommandType::Circle;
    cmd.rect = {center.x - radius, center.y - radius, radius * 2, radius * 2};
    cmd.color = color;
    draw_commands_.push_back(cmd);
}

bool UIContext::IsMouseInRect(const Rect& rect) const {
    return mouse_x_ >= rect.x && mouse_x_ <= rect.x + rect.width &&
           mouse_y_ >= rect.y && mouse_y_ <= rect.y + rect.height;
}

float32 UIContext::GetTextWidth(const String& text) const {
    // Approximate width based on character count
    return text.length() * style_.font_size * 0.6f;
}

} // namespace DRX::UI

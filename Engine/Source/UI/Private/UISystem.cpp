/**
 * DRX Engine - UI System Implementation
 */

#include "UI/Public/UISystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace DRX::UI {

namespace {

size_t CountCodepoints(const String& text) {
    size_t count = 0;
    for (unsigned char c : text) {
        if ((c & 0xC0) != 0x80) {
            ++count;
        }
    }
    return count;
}

void AppendUtf8(String& out, uint32 codepoint) {
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

void PopUtf8(String& text) {
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) {
        text.pop_back();
    }
    if (!text.empty()) {
        text.pop_back();
    }
}

String FormatFloat(float32 value, int precision) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision, static_cast<double>(value));
    return buffer;
}

} // namespace

// ============================================================================
// Lifecycle
// ============================================================================

UIContext& UIContext::Instance() {
    static UIContext instance;
    return instance;
}

void UIContext::Initialize(float32 screen_width, float32 screen_height) {
    SetScreenSize(screen_width, screen_height);
    style_ = Style{};
}

void UIContext::Shutdown() {
    draw_commands_.clear();
    overlay_commands_.clear();
    id_stack_.clear();
    window_stack_.clear();
    window_states_.clear();
    combo_states_.clear();
    edit_buffers_.clear();
    active_id_ = 0;
    hot_id_ = 0;
    text_focus_ = false;
}

void UIContext::SetScreenSize(float32 width, float32 height) {
    screen_width_ = std::max(0.0f, width);
    screen_height_ = std::max(0.0f, height);
}

void UIContext::BeginFrame() {
    draw_commands_.clear();
    overlay_commands_.clear();
    drawing_overlay_ = false;
    hot_id_ = 0;
    want_capture_mouse_ = false;

    blocking_rect_ = next_blocking_rect_;
    has_blocking_rect_ = next_has_blocking_rect_;
    next_has_blocking_rect_ = false;

    cursor_x_ = 0.0f;
    cursor_y_ = 0.0f;
    line_start_x_ = 0.0f;
    last_item_rect_ = Rect{};
}

void UIContext::EndFrame() {
    // Unbalanced Begin/End calls must not leak into the next frame
    window_stack_.clear();
    id_stack_.clear();

    // Clicking empty space drops keyboard focus
    if (mouse_clicked_[0] && hot_id_ == 0 && text_focus_) {
        active_id_ = 0;
        text_focus_ = false;
    }

    if (has_blocking_rect_ && blocking_rect_.Contains(mouse_x_, mouse_y_)) {
        want_capture_mouse_ = true;
    }
    if (active_id_ != 0) {
        want_capture_mouse_ = true;
    }

    draw_commands_.insert(draw_commands_.end(),
                          std::make_move_iterator(overlay_commands_.begin()),
                          std::make_move_iterator(overlay_commands_.end()));
    overlay_commands_.clear();

    // Per-frame input events are consumed
    std::fill(std::begin(mouse_clicked_), std::end(mouse_clicked_), false);
    std::fill(std::begin(keys_pressed_), std::end(keys_pressed_), false);
    input_text_.clear();
    mouse_scroll_ = 0.0f;
}

// ============================================================================
// Input
// ============================================================================

void UIContext::SetMousePosition(float32 x, float32 y) {
    mouse_x_ = x;
    mouse_y_ = y;
}

void UIContext::SetMouseButton(int32 button, bool pressed) {
    if (button < 0 || button >= 3) return;

    if (pressed && !mouse_buttons_[button]) {
        mouse_clicked_[button] = true;
    }
    mouse_buttons_[button] = pressed;
}

void UIContext::SetMouseScroll(float32 scroll) {
    mouse_scroll_ += scroll;
}

void UIContext::SetKeyState(UIKey key, bool pressed) {
    const auto index = static_cast<size_t>(key);
    if (index < static_cast<size_t>(UIKey::Count) && pressed) {
        keys_pressed_[index] = true;
    }
}

void UIContext::AddCharacter(uint32 codepoint) {
    if (codepoint < 32 || codepoint == 127 || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        return;
    }
    AppendUtf8(input_text_, codepoint);
}

// ============================================================================
// Internal helpers
// ============================================================================

uint32 UIContext::GetID(const String& str) const {
    uint32 hash = 2166136261u;
    auto mix = [&hash](const String& s) {
        for (unsigned char c : s) {
            hash ^= c;
            hash *= 16777619u;
        }
        hash ^= 0xFFu;  // separator so "ab"+"c" != "a"+"bc"
        hash *= 16777619u;
    };
    for (const auto& id : id_stack_) {
        mix(id);
    }
    mix(str);
    return hash != 0 ? hash : 1u;
}

bool UIContext::IsMouseInRect(const Rect& rect) const {
    if (!drawing_overlay_ && has_blocking_rect_ && blocking_rect_.Contains(mouse_x_, mouse_y_)) {
        return false;
    }
    return rect.Contains(mouse_x_, mouse_y_);
}

bool UIContext::ButtonBehavior(uint32 id, const Rect& rect, bool& hovered, bool& held) {
    hovered = IsMouseInRect(rect);
    if (hovered) {
        hot_id_ = id;
        if (mouse_clicked_[0]) {
            active_id_ = id;
            text_focus_ = false;
        }
    }

    held = active_id_ == id && mouse_buttons_[0];

    bool pressed = false;
    if (active_id_ == id && !mouse_buttons_[0]) {
        pressed = hovered;  // click completes on release inside the widget
        active_id_ = 0;
    }
    return pressed;
}

void UIContext::AddItem(const Rect& rect) {
    last_item_rect_ = rect;
    cursor_x_ = line_start_x_;
    cursor_y_ = rect.y + rect.height + style_.spacing;
}

void UIContext::Emit(DrawCommand&& cmd) {
    if (drawing_overlay_) {
        overlay_commands_.push_back(std::move(cmd));
    } else {
        draw_commands_.push_back(std::move(cmd));
    }
}

bool UIContext::EditTextBuffer(String& text, uint32 max_length, bool& submitted) {
    bool changed = false;

    if (keys_pressed_[static_cast<size_t>(UIKey::Backspace)] && !text.empty()) {
        PopUtf8(text);
        changed = true;
    }

    if (!input_text_.empty()) {
        size_t i = 0;
        while (i < input_text_.size()) {
            // Copy one complete UTF-8 sequence at a time so limits never split a character
            size_t len = 1;
            const auto lead = static_cast<unsigned char>(input_text_[i]);
            if (lead >= 0xF0) len = 4;
            else if (lead >= 0xE0) len = 3;
            else if (lead >= 0xC0) len = 2;

            if (text.size() + len > max_length) break;
            text.append(input_text_, i, len);
            i += len;
            changed = true;
        }
        input_text_.clear();
    }

    submitted = keys_pressed_[static_cast<size_t>(UIKey::Enter)];
    return changed;
}

bool UIContext::TextField(uint32 id, const Rect& rect, String& text, uint32 max_length,
                          bool& submitted, bool& lost_focus) {
    submitted = false;
    lost_focus = false;

    const bool hovered = IsMouseInRect(rect);
    if (hovered) {
        hot_id_ = id;
    }

    const bool was_focused = active_id_ == id && text_focus_;
    if (mouse_clicked_[0]) {
        if (hovered) {
            active_id_ = id;
            text_focus_ = true;
        } else if (was_focused) {
            active_id_ = 0;
            text_focus_ = false;
        }
    }
    if (was_focused && keys_pressed_[static_cast<size_t>(UIKey::Escape)]) {
        active_id_ = 0;
        text_focus_ = false;
    }

    bool focused = active_id_ == id && text_focus_;
    bool changed = false;
    if (focused) {
        changed = EditTextBuffer(text, max_length, submitted);
        if (submitted) {
            active_id_ = 0;
            text_focus_ = false;
            focused = false;
        }
    }
    lost_focus = was_focused && !focused;

    DrawRect(rect, focused ? style_.input_focused_color : style_.input_color);
    DrawRectOutline(rect, focused ? style_.slider_color : style_.border_color, style_.border_width);
    DrawText(text, rect.x + style_.padding, rect.y + (rect.height - style_.font_size) * 0.5f, style_.text_color);

    if (focused) {
        const float32 caret_x = std::min(rect.x + style_.padding + GetTextWidth(text), rect.x + rect.width - 3.0f);
        DrawRect({caret_x, rect.y + 4.0f, 2.0f, rect.height - 8.0f}, style_.text_color);
    }

    return changed;
}

float32 UIContext::GetTextWidth(const String& text, float32 font_size) const {
    const float32 size = font_size > 0.0f ? font_size : style_.font_size;
    if (text_measure_) {
        return text_measure_(text, size);
    }
    return static_cast<float32>(CountCodepoints(text)) * size * 0.6f;
}

// ============================================================================
// Raw drawing
// ============================================================================

void UIContext::DrawRect(const Rect& rect, const Math::Vector4& color) {
    DrawCommand cmd;
    cmd.type = DrawCommandType::Rect;
    cmd.rect = rect;
    cmd.color = color;
    Emit(std::move(cmd));
}

void UIContext::DrawRectOutline(const Rect& rect, const Math::Vector4& color, float32 thickness) {
    DrawCommand cmd;
    cmd.type = DrawCommandType::RectOutline;
    cmd.rect = rect;
    cmd.color = color;
    cmd.thickness = thickness;
    Emit(std::move(cmd));
}

void UIContext::DrawText(const String& text, float32 x, float32 y, const Math::Vector4& color, float32 font_size) {
    if (text.empty()) return;

    DrawCommand cmd;
    cmd.type = DrawCommandType::Text;
    cmd.rect = {x, y, GetTextWidth(text, font_size), font_size > 0.0f ? font_size : style_.font_size};
    cmd.color = color;
    cmd.text = text;
    cmd.font_size = font_size > 0.0f ? font_size : style_.font_size;
    Emit(std::move(cmd));
}

void UIContext::DrawLine(const Math::Vector2& from, const Math::Vector2& to, const Math::Vector4& color, float32 thickness) {
    DrawCommand cmd;
    cmd.type = DrawCommandType::Line;
    cmd.rect = {from.x, from.y, to.x, to.y};
    cmd.color = color;
    cmd.thickness = thickness;
    Emit(std::move(cmd));
}

void UIContext::DrawCircle(const Math::Vector2& center, float32 radius, const Math::Vector4& color) {
    DrawCommand cmd;
    cmd.type = DrawCommandType::Circle;
    cmd.rect = {center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f};
    cmd.color = color;
    Emit(std::move(cmd));
}

void UIContext::DrawImage(const Rect& rect, SharedPtr<Renderer::Texture> texture, const Math::Vector4& tint) {
    DrawCommand cmd;
    cmd.type = DrawCommandType::Image;
    cmd.rect = rect;
    cmd.color = tint;
    cmd.texture = std::move(texture);
    Emit(std::move(cmd));
}

// ============================================================================
// Windows & Layout
// ============================================================================

bool UIContext::BeginWindow(const String& title, Rect& rect, WindowFlags flags) {
    const uint32 id = GetID(title);
    PushID(title);

    WindowState& state = window_states_[id];
    state.id = id;
    state.title = title;
    state.rect = rect;
    state.flags = flags;
    window_stack_.push_back(&state);

    if (!state.visible) {
        return false;
    }

    if (rect.Contains(mouse_x_, mouse_y_)) {
        want_capture_mouse_ = true;
    }

    if (!(flags & WindowFlags::NoBackground)) {
        DrawRect(rect, style_.background_color);
    }

    float32 top = rect.y;
    if (!(flags & WindowFlags::NoTitleBar)) {
        const Rect title_rect = {rect.x, rect.y, rect.width, style_.title_bar_height};
        DrawRect(title_rect, style_.title_bar_color);
        DrawText(title, title_rect.x + style_.padding,
                 title_rect.y + (title_rect.height - style_.font_size) * 0.5f, style_.text_color);

        if (!(flags & WindowFlags::NoClose)) {
            const Rect close_rect = {rect.x + rect.width - style_.title_bar_height, rect.y,
                                     style_.title_bar_height, style_.title_bar_height};
            bool hovered = false;
            bool held = false;
            if (ButtonBehavior(GetID("##close"), close_rect, hovered, held)) {
                state.visible = false;
            }
            if (hovered) {
                DrawRect(close_rect, held ? style_.button_active_color : style_.button_hover_color);
            }
            DrawText("X", close_rect.x + (close_rect.width - GetTextWidth("X")) * 0.5f,
                     close_rect.y + (close_rect.height - style_.font_size) * 0.5f, style_.text_color);
        }
        top += style_.title_bar_height;
    }

    if (!(flags & WindowFlags::NoBorder)) {
        DrawRectOutline(rect, style_.border_color, style_.border_width);
    }

    state.content_rect = {
        rect.x + style_.padding,
        top + style_.padding,
        std::max(0.0f, rect.width - style_.padding * 2.0f),
        std::max(0.0f, rect.y + rect.height - top - style_.padding * 2.0f)
    };

    cursor_x_ = state.content_rect.x;
    cursor_y_ = state.content_rect.y;
    line_start_x_ = state.content_rect.x;
    last_item_rect_ = {cursor_x_, cursor_y_, 0.0f, 0.0f};

    return state.visible;
}

void UIContext::EndWindow() {
    if (!window_stack_.empty()) {
        window_stack_.pop_back();
        PopID();
    }

    if (!window_stack_.empty()) {
        const WindowState* parent = window_stack_.back();
        line_start_x_ = parent->content_rect.x;
    } else {
        line_start_x_ = 0.0f;
    }
}

void UIContext::SameLine() {
    cursor_x_ = last_item_rect_.x + last_item_rect_.width + style_.spacing;
    cursor_y_ = last_item_rect_.y;
}

void UIContext::Separator() {
    const float32 width = window_stack_.empty() ? style_.widget_width : window_stack_.back()->content_rect.width;
    const Rect rect = {line_start_x_, cursor_y_ + style_.spacing, width, 1.0f};
    DrawRect(rect, style_.border_color);
    AddItem({line_start_x_, cursor_y_, width, style_.spacing * 2.0f + 1.0f});
}

void UIContext::Spacing() {
    AddItem({cursor_x_, cursor_y_, 0.0f, style_.spacing});
}

// ============================================================================
// Widgets
// ============================================================================

void UIContext::Text(const String& text) {
    TextColored(text, style_.text_color);
}

void UIContext::TextColored(const String& text, const Math::Vector4& color) {
    DrawText(text, cursor_x_, cursor_y_, color);
    AddItem({cursor_x_, cursor_y_, GetTextWidth(text), style_.font_size});
}

bool UIContext::Button(const String& label) {
    const uint32 id = GetID(label);
    const float32 width = GetTextWidth(label) + style_.padding * 2.0f;
    const float32 height = style_.font_size + style_.padding * 2.0f;
    const Rect rect = {cursor_x_, cursor_y_, width, height};

    bool hovered = false;
    bool held = false;
    const bool pressed = ButtonBehavior(id, rect, hovered, held);

    DrawRect(rect, held ? style_.button_active_color : hovered ? style_.button_hover_color : style_.button_color);
    DrawRectOutline(rect, style_.border_color, style_.border_width);
    DrawText(label, rect.x + (rect.width - GetTextWidth(label)) * 0.5f,
             rect.y + (rect.height - style_.font_size) * 0.5f, style_.text_color);

    AddItem(rect);
    return pressed;
}

bool UIContext::Checkbox(const String& label, bool& value) {
    const uint32 id = GetID(label);
    const float32 box_size = style_.font_size + 4.0f;
    const Rect box_rect = {cursor_x_, cursor_y_, box_size, box_size};
    const Rect hit_rect = {cursor_x_, cursor_y_, box_size + style_.spacing + GetTextWidth(label), box_size};

    bool hovered = false;
    bool held = false;
    const bool pressed = ButtonBehavior(id, hit_rect, hovered, held);
    if (pressed) {
        value = !value;
    }

    DrawRect(box_rect, hovered ? style_.button_hover_color : style_.input_color);
    DrawRectOutline(box_rect, style_.border_color, style_.border_width);
    if (value) {
        DrawRect({box_rect.x + 4.0f, box_rect.y + 4.0f, box_rect.width - 8.0f, box_rect.height - 8.0f},
                 style_.checkbox_color);
    }
    DrawText(label, box_rect.x + box_size + style_.spacing, box_rect.y + 2.0f, style_.text_color);

    AddItem(hit_rect);
    return pressed;
}

bool UIContext::SliderFloat(const String& label, float32& value, float32 min_val, float32 max_val) {
    const uint32 id = GetID(label);
    if (max_val < min_val) std::swap(min_val, max_val);
    const float32 range = max_val - min_val;

    DrawText(label, cursor_x_, cursor_y_, style_.text_color);
    const Rect track = {cursor_x_, cursor_y_ + style_.font_size + 2.0f, style_.widget_width, style_.font_size};

    bool hovered = false;
    bool held = false;
    ButtonBehavior(id, track, hovered, held);

    const float32 old_value = value;
    if (held && range > 0.0f) {
        const float32 t = std::clamp((mouse_x_ - track.x) / track.width, 0.0f, 1.0f);
        value = min_val + t * range;
    }
    if (hovered && mouse_scroll_ != 0.0f && range > 0.0f) {
        value += mouse_scroll_ * range * 0.05f;
    }
    value = std::clamp(value, min_val, max_val);

    DrawRect(track, style_.input_color);
    const float32 fill = range > 0.0f ? (value - min_val) / range : 0.0f;
    DrawRect({track.x, track.y, track.width * fill, track.height}, style_.slider_color);
    DrawRectOutline(track, style_.border_color, style_.border_width);
    DrawText(FormatFloat(value, 2), track.x + track.width + style_.spacing, track.y, style_.text_color);

    AddItem({cursor_x_, cursor_y_, track.width + style_.spacing + GetTextWidth(FormatFloat(value, 2)),
             track.y + track.height - cursor_y_});
    return value != old_value;
}

bool UIContext::SliderInt(const String& label, int32& value, int32 min_val, int32 max_val) {
    float32 fvalue = static_cast<float32>(value);
    SliderFloat(label, fvalue, static_cast<float32>(min_val), static_cast<float32>(max_val));
    const int32 new_value = static_cast<int32>(std::lround(fvalue));
    const bool changed = new_value != value;
    value = new_value;
    return changed;
}

bool UIContext::InputText(const String& label, String& text, uint32 max_length) {
    const uint32 id = GetID(label);

    DrawText(label, cursor_x_, cursor_y_, style_.text_color);
    const Rect field = {cursor_x_, cursor_y_ + style_.font_size + 2.0f, style_.widget_width + 50.0f,
                        style_.font_size + style_.padding * 2.0f};

    bool submitted = false;
    bool lost_focus = false;
    const bool changed = TextField(id, field, text, max_length, submitted, lost_focus);

    AddItem({cursor_x_, cursor_y_, field.width, field.y + field.height - cursor_y_});
    return changed;
}

bool UIContext::InputFloat(const String& label, float32& value) {
    const uint32 id = GetID(label);

    DrawText(label, cursor_x_, cursor_y_, style_.text_color);
    const Rect field = {cursor_x_, cursor_y_ + style_.font_size + 2.0f, style_.widget_width,
                        style_.font_size + style_.padding * 2.0f};

    const bool was_focused = active_id_ == id && text_focus_;
    auto buffer_it = edit_buffers_.find(id);
    String display = (was_focused && buffer_it != edit_buffers_.end()) ? buffer_it->second : FormatFloat(value, 3);

    bool submitted = false;
    bool lost_focus = false;
    TextField(id, field, display, 32, submitted, lost_focus);

    const bool focused = active_id_ == id && text_focus_;
    bool changed = false;
    if (focused) {
        edit_buffers_[id] = display;
    } else if (was_focused && (submitted || lost_focus)) {
        char* end = nullptr;
        const float32 parsed = std::strtof(display.c_str(), &end);
        if (end != display.c_str() && *end == '\0' && std::isfinite(parsed)) {
            changed = parsed != value;
            value = parsed;
        }
        edit_buffers_.erase(id);
    }

    AddItem({cursor_x_, cursor_y_, field.width, field.y + field.height - cursor_y_});
    return changed;
}

bool UIContext::ColorEdit(const String& label, Math::Vector4& color) {
    DrawText(label, cursor_x_, cursor_y_, style_.text_color);
    const float32 preview = style_.font_size;
    const Rect preview_rect = {cursor_x_ + GetTextWidth(label) + style_.spacing, cursor_y_, preview, preview};
    DrawRect(preview_rect, color);
    DrawRectOutline(preview_rect, style_.border_color, style_.border_width);
    AddItem({cursor_x_, cursor_y_, preview_rect.x + preview - cursor_x_, preview});

    PushID(label);
    bool changed = false;
    changed |= SliderFloat("R", color.x, 0.0f, 1.0f);
    changed |= SliderFloat("G", color.y, 0.0f, 1.0f);
    changed |= SliderFloat("B", color.z, 0.0f, 1.0f);
    changed |= SliderFloat("A", color.w, 0.0f, 1.0f);
    PopID();

    return changed;
}

bool UIContext::Combo(const String& label, int32& current_item, const Array<String>& items) {
    const uint32 id = GetID(label);
    ComboState& state = combo_states_[id];

    DrawText(label, cursor_x_, cursor_y_, style_.text_color);
    const float32 item_height = style_.font_size + style_.padding * 2.0f;
    const Rect header = {cursor_x_, cursor_y_ + style_.font_size + 2.0f, style_.widget_width, item_height};

    bool hovered = false;
    bool held = false;
    if (ButtonBehavior(id, header, hovered, held)) {
        state.open = !state.open;
    }

    DrawRect(header, (hovered || state.open) ? style_.button_hover_color : style_.input_color);
    DrawRectOutline(header, style_.border_color, style_.border_width);
    if (current_item >= 0 && current_item < static_cast<int32>(items.size())) {
        DrawText(items[static_cast<size_t>(current_item)], header.x + style_.padding,
                 header.y + style_.padding, style_.text_color);
    }
    DrawText("v", header.x + header.width - style_.padding - GetTextWidth("v"),
             header.y + style_.padding, style_.text_color);

    bool changed = false;
    if (state.open) {
        const Rect list = {header.x, header.y + header.height, header.width,
                           item_height * static_cast<float32>(items.size())};
        next_blocking_rect_ = list;
        next_has_blocking_rect_ = true;

        drawing_overlay_ = true;
        for (size_t i = 0; i < items.size(); ++i) {
            const Rect item_rect = {list.x, list.y + item_height * static_cast<float32>(i), list.width, item_height};
            const bool item_hovered = IsMouseInRect(item_rect);

            Math::Vector4 color = style_.input_color;
            if (static_cast<int32>(i) == current_item) color = style_.slider_color;
            if (item_hovered) color = style_.button_hover_color;

            DrawRect(item_rect, color);
            DrawText(items[i], item_rect.x + style_.padding, item_rect.y + style_.padding, style_.text_color);

            if (item_hovered && mouse_clicked_[0]) {
                changed = current_item != static_cast<int32>(i);
                current_item = static_cast<int32>(i);
                state.open = false;
            }
        }
        DrawRectOutline(list, style_.border_color, style_.border_width);
        drawing_overlay_ = false;

        // Clicking anywhere else closes the list
        if (mouse_clicked_[0] && !list.Contains(mouse_x_, mouse_y_) && !header.Contains(mouse_x_, mouse_y_)) {
            state.open = false;
        }
    }

    AddItem({cursor_x_, cursor_y_, header.width, header.y + header.height - cursor_y_});
    return changed;
}

void UIContext::Image(SharedPtr<Renderer::Texture> texture, const Math::Vector2& size) {
    const Rect rect = {cursor_x_, cursor_y_, size.x, size.y};
    DrawImage(rect, std::move(texture));
    AddItem(rect);
}

void UIContext::ProgressBar(float32 progress, const Math::Vector2& size) {
    const float32 width = size.x > 0.0f ? size.x : style_.widget_width;
    const float32 height = size.y > 0.0f ? size.y : style_.font_size + style_.padding;
    const Rect rect = {cursor_x_, cursor_y_, width, height};
    const float32 clamped = std::clamp(progress, 0.0f, 1.0f);

    DrawRect(rect, style_.input_color);
    DrawRect({rect.x, rect.y, rect.width * clamped, rect.height}, style_.slider_color);
    DrawRectOutline(rect, style_.border_color, style_.border_width);

    const String percent = FormatFloat(clamped * 100.0f, 0) + "%";
    DrawText(percent, rect.x + (rect.width - GetTextWidth(percent)) * 0.5f,
             rect.y + (rect.height - style_.font_size) * 0.5f, style_.text_color);

    AddItem(rect);
}

// ============================================================================
// ID stack
// ============================================================================

void UIContext::PushID(const String& id) {
    id_stack_.push_back(id);
}

void UIContext::PushID(int32 id) {
    id_stack_.push_back("#" + std::to_string(id));
}

void UIContext::PopID() {
    if (!id_stack_.empty()) {
        id_stack_.pop_back();
    }
}

} // namespace DRX::UI

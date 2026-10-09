/**
 * DRX Engine - UI System
 * Immediate mode game UI (HUD, menus). Produces draw commands for the renderer.
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector2.h"
#include "Core/Public/Math/Vector4.h"
#include "Renderer/Public/Texture.h"

namespace DRX::UI {

/**
 * Screen-space rectangle (pixels, origin top-left)
 */
struct Rect {
    float32 x = 0.0f;
    float32 y = 0.0f;
    float32 width = 0.0f;
    float32 height = 0.0f;

    bool Contains(float32 px, float32 py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};

/**
 * Draw commands consumed by the UI renderer
 */
enum class DrawCommandType : uint8 {
    Rect,
    RectOutline,
    Text,
    Image,
    Line,
    Circle
};

struct DrawCommand {
    DrawCommandType type = DrawCommandType::Rect;
    Rect rect;            // Line: (x0, y0, x1, y1)
    Math::Vector4 color = {1, 1, 1, 1};
    String text;
    float32 font_size = 14.0f;
    float32 thickness = 1.0f;
    SharedPtr<Renderer::Texture> texture;
};

/**
 * Window flags
 */
enum class WindowFlags : uint32 {
    None = 0,
    NoTitleBar = 1 << 0,
    NoClose = 1 << 1,
    NoBackground = 1 << 2,
    NoBorder = 1 << 3
};

inline WindowFlags operator|(WindowFlags a, WindowFlags b) {
    return static_cast<WindowFlags>(static_cast<uint32>(a) | static_cast<uint32>(b));
}

inline bool operator&(WindowFlags a, WindowFlags b) {
    return (static_cast<uint32>(a) & static_cast<uint32>(b)) != 0;
}

/**
 * Keys the UI reacts to
 */
enum class UIKey : uint8 {
    Backspace,
    Enter,
    Escape,
    Count
};

/**
 * Visual style
 */
struct Style {
    Math::Vector4 background_color = {0.2f, 0.2f, 0.2f, 0.9f};
    Math::Vector4 title_bar_color = {0.15f, 0.15f, 0.15f, 1.0f};
    Math::Vector4 text_color = {1.0f, 1.0f, 1.0f, 1.0f};
    Math::Vector4 button_color = {0.3f, 0.3f, 0.3f, 1.0f};
    Math::Vector4 button_hover_color = {0.4f, 0.4f, 0.4f, 1.0f};
    Math::Vector4 button_active_color = {0.5f, 0.5f, 0.5f, 1.0f};
    Math::Vector4 input_color = {0.15f, 0.15f, 0.15f, 1.0f};
    Math::Vector4 input_focused_color = {0.2f, 0.2f, 0.2f, 1.0f};
    Math::Vector4 slider_color = {0.4f, 0.6f, 0.9f, 1.0f};
    Math::Vector4 checkbox_color = {0.4f, 0.6f, 0.9f, 1.0f};
    Math::Vector4 border_color = {0.5f, 0.5f, 0.5f, 1.0f};

    float32 padding = 8.0f;
    float32 spacing = 4.0f;
    float32 border_width = 1.0f;
    float32 font_size = 14.0f;
    float32 title_bar_height = 24.0f;
    float32 widget_width = 150.0f;
};

/**
 * UI Context - immediate mode UI state
 */
class UIContext {
public:
    using TextMeasureFunc = Function<float32(const String& text, float32 font_size)>;

    static UIContext& Instance();

    void Initialize(float32 screen_width, float32 screen_height);
    void Shutdown();
    void SetScreenSize(float32 width, float32 height);

    void BeginFrame();
    void EndFrame();

    // Draw commands for the current frame (valid after EndFrame until next BeginFrame)
    const Array<DrawCommand>& GetDrawCommands() const { return draw_commands_; }

    // Input (forward from the platform layer)
    void SetMousePosition(float32 x, float32 y);
    void SetMouseButton(int32 button, bool pressed);
    void SetMouseScroll(float32 scroll);
    void SetKeyState(UIKey key, bool pressed);
    void AddCharacter(uint32 codepoint);

    bool WantCaptureMouse() const { return want_capture_mouse_; }
    bool WantCaptureKeyboard() const { return active_id_ != 0 && text_focus_; }

    // Windows
    bool BeginWindow(const String& title, Rect& rect, WindowFlags flags = WindowFlags::None);
    void EndWindow();

    // Layout
    void SameLine();
    void Separator();
    void Spacing();

    // Widgets
    void Text(const String& text);
    void TextColored(const String& text, const Math::Vector4& color);
    bool Button(const String& label);
    bool Checkbox(const String& label, bool& value);
    bool SliderFloat(const String& label, float32& value, float32 min_val, float32 max_val);
    bool SliderInt(const String& label, int32& value, int32 min_val, int32 max_val);
    bool InputText(const String& label, String& text, uint32 max_length = 256);
    bool InputFloat(const String& label, float32& value);
    bool ColorEdit(const String& label, Math::Vector4& color);
    bool Combo(const String& label, int32& current_item, const Array<String>& items);
    void Image(SharedPtr<Renderer::Texture> texture, const Math::Vector2& size);
    void ProgressBar(float32 progress, const Math::Vector2& size);

    // ID stack
    void PushID(const String& id);
    void PushID(int32 id);
    void PopID();

    // Raw drawing (HUD elements such as crosshairs)
    void DrawRect(const Rect& rect, const Math::Vector4& color);
    void DrawRectOutline(const Rect& rect, const Math::Vector4& color, float32 thickness = 1.0f);
    void DrawText(const String& text, float32 x, float32 y, const Math::Vector4& color, float32 font_size = 0.0f);
    void DrawLine(const Math::Vector2& from, const Math::Vector2& to, const Math::Vector4& color, float32 thickness = 1.0f);
    void DrawCircle(const Math::Vector2& center, float32 radius, const Math::Vector4& color);
    void DrawImage(const Rect& rect, SharedPtr<Renderer::Texture> texture, const Math::Vector4& tint = {1, 1, 1, 1});

    float32 GetTextWidth(const String& text, float32 font_size = 0.0f) const;
    void SetTextMeasureFunc(TextMeasureFunc func) { text_measure_ = std::move(func); }

    Style& GetStyle() { return style_; }
    float32 GetScreenWidth() const { return screen_width_; }
    float32 GetScreenHeight() const { return screen_height_; }

private:
    UIContext() = default;

    struct WindowState {
        uint32 id = 0;
        String title;
        Rect rect;
        Rect content_rect;
        WindowFlags flags = WindowFlags::None;
        bool visible = true;
    };

    struct ComboState {
        bool open = false;
    };

    uint32 GetID(const String& str) const;
    bool IsMouseInRect(const Rect& rect) const;
    bool ButtonBehavior(uint32 id, const Rect& rect, bool& hovered, bool& held);
    void AddItem(const Rect& rect);
    bool EditTextBuffer(String& text, uint32 max_length, bool& submitted);
    // Focusable text box; returns true when the text changed
    bool TextField(uint32 id, const Rect& rect, String& text, uint32 max_length,
                   bool& submitted, bool& lost_focus);
    void Emit(DrawCommand&& cmd);

    Style style_;
    TextMeasureFunc text_measure_;

    Array<DrawCommand> draw_commands_;
    Array<DrawCommand> overlay_commands_;
    bool drawing_overlay_ = false;

    float32 screen_width_ = 0.0f;
    float32 screen_height_ = 0.0f;

    // Input
    float32 mouse_x_ = 0.0f;
    float32 mouse_y_ = 0.0f;
    float32 mouse_scroll_ = 0.0f;
    bool mouse_buttons_[3] = {};
    bool mouse_clicked_[3] = {};
    bool keys_pressed_[static_cast<size_t>(UIKey::Count)] = {};
    String input_text_;

    // Interaction
    uint32 hot_id_ = 0;
    uint32 active_id_ = 0;
    bool text_focus_ = false;
    bool want_capture_mouse_ = false;
    Rect blocking_rect_;        // popup area from the previous frame
    Rect next_blocking_rect_;
    bool has_blocking_rect_ = false;
    bool next_has_blocking_rect_ = false;

    // Layout
    HashMap<uint32, WindowState> window_states_;
    Array<WindowState*> window_stack_;
    HashMap<uint32, ComboState> combo_states_;
    HashMap<uint32, String> edit_buffers_;
    Array<String> id_stack_;
    float32 cursor_x_ = 0.0f;
    float32 cursor_y_ = 0.0f;
    float32 line_start_x_ = 0.0f;
    Rect last_item_rect_;
};

} // namespace DRX::UI

/**
 * DRX Engine - UI System
 * Immediate mode and retained mode UI rendering
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector2.h"
#include "Core/Public/Math/Vector4.h"
#include "Renderer/Public/Texture.h"

namespace DRX::UI {

/**
 * UI Colors
 */
struct UIColors {
    Math::Vector4 text = {1, 1, 1, 1};
    Math::Vector4 text_disabled = {0.5f, 0.5f, 0.5f, 1};
    Math::Vector4 background = {0.1f, 0.1f, 0.1f, 0.9f};
    Math::Vector4 background_hovered = {0.15f, 0.15f, 0.15f, 1};
    Math::Vector4 background_active = {0.2f, 0.2f, 0.2f, 1};
    Math::Vector4 border = {0.3f, 0.3f, 0.3f, 1};
    Math::Vector4 button = {0.2f, 0.2f, 0.2f, 1};
    Math::Vector4 button_hovered = {0.3f, 0.3f, 0.3f, 1};
    Math::Vector4 button_active = {0.4f, 0.4f, 0.4f, 1};
    Math::Vector4 slider = {0.3f, 0.3f, 0.3f, 1};
    Math::Vector4 slider_grab = {0.5f, 0.5f, 0.5f, 1};
    Math::Vector4 check_mark = {0.3f, 0.7f, 0.3f, 1};
    Math::Vector4 header = {0.2f, 0.4f, 0.6f, 1};
    Math::Vector4 header_hovered = {0.3f, 0.5f, 0.7f, 1};
    Math::Vector4 header_active = {0.4f, 0.6f, 0.8f, 1};
};

/**
 * UI Style
 */
struct UIStyle {
    float32 window_padding_x = 8.0f;
    float32 window_padding_y = 8.0f;
    float32 window_rounding = 4.0f;
    float32 frame_padding_x = 4.0f;
    float32 frame_padding_y = 3.0f;
    float32 frame_rounding = 2.0f;
    float32 item_spacing_x = 8.0f;
    float32 item_spacing_y = 4.0f;
    float32 indent_spacing = 20.0f;
    float32 scrollbar_size = 14.0f;
    float32 scrollbar_rounding = 4.0f;
    float32 grab_min_size = 10.0f;
    float32 grab_rounding = 2.0f;
    float32 button_rounding = 4.0f;
    float32 font_size = 14.0f;

    UIColors colors;
};

/**
 * UI Rect
 */
struct UIRect {
    float32 x = 0, y = 0;
    float32 width = 0, height = 0;

    bool Contains(float32 px, float32 py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }

    UIRect Expand(float32 amount) const {
        return {x - amount, y - amount, width + amount * 2, height + amount * 2};
    }
};

/**
 * Font glyph
 */
struct FontGlyph {
    uint32 codepoint;
    float32 advance_x;
    float32 x0, y0, x1, y1;  // Quad corners
    float32 u0, v0, u1, v1;  // Texture coordinates
};

/**
 * Font
 */
class Font {
public:
    Font() = default;
    ~Font() = default;

    bool LoadFromFile(const String& path, float32 size);
    bool LoadFromMemory(const void* data, uint32 size, float32 font_size);

    const FontGlyph* GetGlyph(uint32 codepoint) const;
    float32 GetKerning(uint32 left, uint32 right) const;
    Math::Vector2 CalcTextSize(const String& text) const;

    float32 GetSize() const { return size_; }
    float32 GetLineHeight() const { return line_height_; }
    float32 GetAscent() const { return ascent_; }
    float32 GetDescent() const { return descent_; }

    SharedPtr<Renderer::Texture> GetAtlasTexture() const { return atlas_texture_; }

private:
    float32 size_ = 14.0f;
    float32 line_height_ = 0;
    float32 ascent_ = 0;
    float32 descent_ = 0;
    HashMap<uint32, FontGlyph> glyphs_;
    HashMap<uint64, float32> kerning_;
    SharedPtr<Renderer::Texture> atlas_texture_;
};

/**
 * Draw command for UI batching
 */
struct UIDrawCommand {
    enum class Type { Rect, RectRounded, Text, Texture, Line, Triangle };

    Type type;
    UIRect rect;
    Math::Vector4 color;
    SharedPtr<Renderer::Texture> texture;
    String text;
    Font* font = nullptr;
    float32 rounding = 0;
    uint32 corner_flags = 0xF;  // Which corners to round
    float32 thickness = 1.0f;  // For lines
    Array<Math::Vector2> points;  // For triangles/lines
};

/**
 * UI Draw List - batched draw commands
 */
class UIDrawList {
public:
    void Clear();

    void AddRect(const UIRect& rect, const Math::Vector4& color, float32 rounding = 0);
    void AddRectFilled(const UIRect& rect, const Math::Vector4& color, float32 rounding = 0);
    void AddText(const Math::Vector2& pos, const String& text, const Math::Vector4& color, Font* font);
    void AddTexture(const UIRect& rect, SharedPtr<Renderer::Texture> texture,
                    const Math::Vector4& tint = {1, 1, 1, 1});
    void AddLine(const Math::Vector2& a, const Math::Vector2& b, const Math::Vector4& color,
                 float32 thickness = 1.0f);
    void AddTriangle(const Math::Vector2& a, const Math::Vector2& b, const Math::Vector2& c,
                     const Math::Vector4& color);
    void AddTriangleFilled(const Math::Vector2& a, const Math::Vector2& b, const Math::Vector2& c,
                           const Math::Vector4& color);
    void AddCircle(const Math::Vector2& center, float32 radius, const Math::Vector4& color,
                   int32 segments = 12);
    void AddCircleFilled(const Math::Vector2& center, float32 radius, const Math::Vector4& color,
                         int32 segments = 12);

    void PushClipRect(const UIRect& rect);
    void PopClipRect();

    const Array<UIDrawCommand>& GetCommands() const { return commands_; }

private:
    Array<UIDrawCommand> commands_;
    Array<UIRect> clip_stack_;
};

/**
 * UI Window flags
 */
enum class UIWindowFlags : uint32 {
    None = 0,
    NoTitleBar = 1 << 0,
    NoResize = 1 << 1,
    NoMove = 1 << 2,
    NoScrollbar = 1 << 3,
    NoCollapse = 1 << 4,
    AlwaysAutoResize = 1 << 5,
    NoBackground = 1 << 6,
    NoSavedSettings = 1 << 7,
    MenuBar = 1 << 8,
    HorizontalScrollbar = 1 << 9,
    NoFocusOnAppearing = 1 << 10,
    NoBringToFrontOnFocus = 1 << 11,
    NoNav = 1 << 12
};

inline UIWindowFlags operator|(UIWindowFlags a, UIWindowFlags b) {
    return static_cast<UIWindowFlags>(static_cast<uint32>(a) | static_cast<uint32>(b));
}

inline bool operator&(UIWindowFlags a, UIWindowFlags b) {
    return (static_cast<uint32>(a) & static_cast<uint32>(b)) != 0;
}

/**
 * UI Context - immediate mode UI state
 */
class UIContext {
public:
    UIContext();
    ~UIContext();

    void BeginFrame(float32 width, float32 height, float32 delta_time);
    void EndFrame();

    // Windows
    bool BeginWindow(const String& title, bool* open = nullptr, UIWindowFlags flags = UIWindowFlags::None);
    void EndWindow();

    // Layout
    void SameLine(float32 offset = 0, float32 spacing = -1);
    void NewLine();
    void Separator();
    void Spacing();
    void Indent(float32 width = 0);
    void Unindent(float32 width = 0);
    void BeginGroup();
    void EndGroup();
    void SetNextItemWidth(float32 width);

    // Widgets
    void Text(const String& text);
    void TextColored(const Math::Vector4& color, const String& text);
    void TextWrapped(const String& text);
    bool Button(const String& label, const Math::Vector2& size = {0, 0});
    bool Checkbox(const String& label, bool* value);
    bool RadioButton(const String& label, bool active);
    bool RadioButton(const String& label, int32* value, int32 button_value);
    bool SliderFloat(const String& label, float32* value, float32 min, float32 max,
                     const String& format = "%.3f");
    bool SliderInt(const String& label, int32* value, int32 min, int32 max);
    bool DragFloat(const String& label, float32* value, float32 speed = 1.0f,
                   float32 min = 0, float32 max = 0);
    bool DragInt(const String& label, int32* value, float32 speed = 1.0f,
                 int32 min = 0, int32 max = 0);
    bool InputText(const String& label, String& text, uint32 max_length = 256);
    bool InputFloat(const String& label, float32* value, float32 step = 0, float32 step_fast = 0);
    bool InputInt(const String& label, int32* value, int32 step = 1, int32 step_fast = 100);
    bool ColorEdit3(const String& label, float32 color[3]);
    bool ColorEdit4(const String& label, float32 color[4]);
    bool ColorPicker3(const String& label, float32 color[3]);
    bool ColorPicker4(const String& label, float32 color[4]);
    void ProgressBar(float32 fraction, const Math::Vector2& size = {-1, 0});
    void Image(SharedPtr<Renderer::Texture> texture, const Math::Vector2& size);
    bool ImageButton(SharedPtr<Renderer::Texture> texture, const Math::Vector2& size);

    // Lists
    bool BeginCombo(const String& label, const String& preview);
    void EndCombo();
    bool Selectable(const String& label, bool selected = false);
    bool BeginListBox(const String& label, const Math::Vector2& size = {0, 0});
    void EndListBox();

    // Trees
    bool TreeNode(const String& label);
    void TreePop();
    bool CollapsingHeader(const String& label);

    // Menus
    bool BeginMenuBar();
    void EndMenuBar();
    bool BeginMenu(const String& label);
    void EndMenu();
    bool MenuItem(const String& label, const String& shortcut = "", bool* selected = nullptr);

    // Popups
    void OpenPopup(const String& id);
    bool BeginPopup(const String& id);
    bool BeginPopupModal(const String& title, bool* open = nullptr);
    void EndPopup();
    void CloseCurrentPopup();

    // Tooltips
    void SetTooltip(const String& text);
    void BeginTooltip();
    void EndTooltip();

    // ID stack
    void PushID(const String& id);
    void PushID(int32 id);
    void PopID();

    // Style
    void PushStyleColor(const Math::Vector4& color);
    void PopStyleColor(int32 count = 1);
    void PushStyleVar(float32 value);
    void PopStyleVar(int32 count = 1);

    UIStyle& GetStyle() { return style_; }
    void SetStyle(const UIStyle& style) { style_ = style; }

    // Input state
    bool IsItemHovered() const;
    bool IsItemActive() const;
    bool IsItemClicked(int32 mouse_button = 0) const;
    bool IsWindowHovered() const;
    bool IsWindowFocused() const;

    // Mouse input
    void SetMousePos(float32 x, float32 y);
    void SetMouseButton(int32 button, bool down);
    void SetMouseWheel(float32 delta);

    // Keyboard input
    void SetKeyDown(int32 key, bool down);
    void AddCharacter(uint32 codepoint);

    // Font
    void SetFont(SharedPtr<Font> font) { current_font_ = font; }
    SharedPtr<Font> GetFont() const { return current_font_; }

    // Draw list
    UIDrawList& GetDrawList() { return draw_list_; }
    const UIDrawList& GetDrawList() const { return draw_list_; }

private:
    struct WindowState {
        String title;
        UIRect rect;
        UIRect content_rect;
        float32 scroll_y = 0;
        bool collapsed = false;
        bool open = true;
        bool appeared = false;
    };

    uint32 GetID(const String& str);
    UIRect GetItemRect(float32 width, float32 height);
    void ItemSize(float32 width, float32 height);
    bool IsMouseHoveringRect(const UIRect& rect) const;

    UIStyle style_;
    UIDrawList draw_list_;
    SharedPtr<Font> current_font_;

    // Input state
    Math::Vector2 mouse_pos_;
    Math::Vector2 mouse_delta_;
    bool mouse_down_[5] = {};
    bool mouse_clicked_[5] = {};
    float32 mouse_wheel_ = 0;
    bool keys_down_[512] = {};
    String input_chars_;

    // UI state
    Array<WindowState> windows_;
    WindowState* current_window_ = nullptr;
    float32 cursor_x_ = 0, cursor_y_ = 0;
    float32 line_height_ = 0;
    float32 same_line_offset_ = 0;
    float32 next_item_width_ = 0;
    uint32 hot_item_ = 0;
    uint32 active_item_ = 0;
    uint32 last_item_id_ = 0;
    Array<uint32> id_stack_;
    float32 screen_width_ = 0, screen_height_ = 0;
    float32 delta_time_ = 0;
};

/**
 * UI System - manages UI rendering
 */
class UISystem {
public:
    static UISystem& Instance();

    bool Initialize();
    void Shutdown();

    void BeginFrame(float32 width, float32 height, float32 delta_time);
    void EndFrame();
    void Render();

    UIContext& GetContext() { return context_; }

    // Font loading
    SharedPtr<Font> LoadFont(const String& path, float32 size);
    SharedPtr<Font> GetDefaultFont() const { return default_font_; }
    void SetDefaultFont(SharedPtr<Font> font) { default_font_ = font; }

    // Input forwarding
    void OnMouseMove(float32 x, float32 y);
    void OnMouseButton(int32 button, bool down);
    void OnMouseWheel(float32 delta);
    void OnKey(int32 key, bool down);
    void OnChar(uint32 codepoint);

    // Check if UI wants input
    bool WantCaptureMouse() const;
    bool WantCaptureKeyboard() const;

private:
    UISystem() = default;

    UIContext context_;
    SharedPtr<Font> default_font_;
    bool initialized_ = false;
};

} // namespace DRX::UI

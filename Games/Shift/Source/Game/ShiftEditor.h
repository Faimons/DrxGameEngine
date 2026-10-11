#pragma once

/**
 * SHIFT - Map editor (inside the game)
 *
 * Builds and edits Level descriptions: blocks, moving walls, starts, core, exits and decoration.
 * Maps are saved as JSON below Assets/Maps and appear in the map choice of the menu.
 *
 * Controls: hold right mouse + WASD/QE to fly (Shift = fast), left click selects, drag moves on the
 * grid, Del deletes, Ctrl+D duplicates, R turns 90 degrees, arrow keys / PageUp / PageDown nudge,
 * Ctrl+Z / Ctrl+Y undo / redo, Ctrl+S saves, F focuses the selection, G changes the grid.
 */

#include "../Core/ShiftConfig.h"
#include "../Core/ShiftLevel.h"

#include "Input/Public/InputSystem.h"
#include "Renderer/Public/ModelLoader.h"
#include "Renderer/Public/SceneRenderer.h"
#include "UI/Public/UISystem.h"

#include <map>
#include <initializer_list>
#include <tuple>

namespace Shift {

class MapEditor {
public:
    enum class Request : uint8 { None, Play, Exit };

    // `level_name`: file or built-in map to start with
    void Open(Renderer::SceneRenderer& renderer, const ShiftConfig& config, const String& asset_dir,
              const String& level_name);
    void Close(Renderer::SceneRenderer& renderer);
    bool IsOpen() const { return open_; }

    void Update(Input::InputSystem& input, float32 dt, float32 screen_width, float32 screen_height, bool mouse_look);
    Renderer::CameraParams GetCamera(float32 aspect) const;
    // True while the right mouse button flies the camera (the app captures the mouse)
    bool WantsMouseLook(const Input::InputSystem& input) const;
    void Render(Renderer::SceneRenderer& renderer);
    void DrawUI(UI::UIContext& ui, Input::InputSystem& input, float32 width, float32 height);

    Request TakeRequest();
    const Level& GetLevel() const { return level_; }
    const String& GetFileName() const { return file_name_; }

private:
    enum class Kind : uint8 { None, Box, Segment, Prop, Spawn, Core, Exit };
    struct Selection {
        Kind kind = Kind::None;
        int32 index = -1;
        bool operator==(const Selection& o) const { return kind == o.kind && index == o.index; }
    };
    struct Ray {
        Math::Vector3 origin;
        Math::Vector3 direction;
    };
    struct Hit {
        Selection selection;
        float32 distance = 0.0f;
    };
    using SizeKey = std::tuple<int32, int32, int32>;

    // Layout and widgets
    struct Layout {
        UI::UIContext* ui = nullptr;
        Input::InputSystem* input = nullptr;
        float32 x = 0.0f;
        float32 y = 0.0f;
        float32 width = 0.0f;
    };
    void Header(Layout& l, const String& text);
    void Label(Layout& l, const String& text, const Math::Vector4& color = {0.85f, 0.88f, 0.95f, 1.0f});
    bool Button(Layout& l, const String& label, float32 x, float32 width, bool highlight = false);
    bool FullButton(Layout& l, const String& label, bool highlight = false);
    int32 ButtonRow(Layout& l, std::initializer_list<const char*> labels);
    bool Number(Layout& l, const String& label, float32& value, float32 step, float32 min_value, float32 max_value);
    bool Choice(Layout& l, const String& label, int32& index, const Array<String>& items);
    bool Toggle(Layout& l, const String& label, bool& value);
    void TextField(Layout& l, const String& label, String& text);
    bool Hovered(float32 x, float32 y, float32 w, float32 h) const;

    void DrawFilePanel(Layout& l);
    void DrawBuildPanel(Layout& l);
    void DrawViewPanel(Layout& l);
    void DrawProperties(Layout& l);

    // Editing
    void PushUndo();
    void Undo();
    void Redo();
    void MarkChanged();
    Math::Vector3 PlacementPoint() const;
    void AddBox(const Math::Vector3& size, Surface surface, float32 bottom);
    void AddSegment(const Math::Vector3& size, const Math::Vector3& move, float32 rotate, SegmentGroup group);
    void AddProp(const String& model);
    void DeleteSelection();
    void DuplicateSelection();
    void RotateSelection();
    void MoveSelection(const Math::Vector3& delta);
    bool GetSelectionPosition(Math::Vector3& out) const;
    Math::Vector3 SelectionSize() const;
    void SetStatus(const String& text, bool error = false);
    void NewLevel();
    void LoadLevelByName(const String& name);
    void SaveCurrent();
    void RunCheck();
    void RefreshFileLists();

    // Picking
    Ray MouseRay(float32 mx, float32 my) const;
    bool Pick(const Ray& ray, Hit& hit) const;
    bool OverPanel(float32 mx, float32 my) const;
    bool IsDraggable(const Selection& selection) const;
    float32 SnapCorner(float32 center, float32 size) const;

    // Rendering
    MeshHandle BoxMesh(Renderer::SceneRenderer& renderer, const Math::Vector3& size);
    const Renderer::Model* PropModel(Renderer::SceneRenderer& renderer, const String& model);
    SegmentPose PreviewPose(const MovingSegmentDef& def) const;

    bool open_ = false;
    const ShiftConfig* config_ = nullptr;
    String asset_dir_;
    String map_dir_;
    Level level_;
    String file_name_;
    bool dirty_ = false;
    bool confirm_exit_ = false;
    Request request_ = Request::None;

    Math::Vector3 cam_pos_ = {0.0f, 18.0f, 22.0f};
    float32 cam_yaw_ = 0.0f;
    float32 cam_pitch_ = -45.0f;
    float32 fov_ = 70.0f;
    float32 screen_w_ = 1280.0f;
    float32 screen_h_ = 720.0f;

    Selection selection_;
    float32 grid_ = 0.5f;
    bool hide_ceiling_ = true;
    bool animate_ = false;
    float32 anim_time_ = 0.0f;
    GroupMask preview_state_ = 0;

    Array<Level> undo_;
    Array<Level> redo_;
    bool dragging_ = false;
    bool drag_moved_ = false;
    float32 drag_plane_y_ = 0.0f;
    Math::Vector3 drag_offset_;
    Math::Vector2 press_mouse_;

    // Widget state
    uint32 active_widget_ = 0;
    float32 widget_start_x_ = 0.0f;
    float32 widget_start_value_ = 0.0f;
    bool widget_pushed_undo_ = false;
    bool text_focus_ = false;
    uint32 focus_field_ = 0;
    Math::Vector2 mouse_;
    bool mouse_clicked_ = false;
    bool mouse_down_ = false;
    float32 scroll_ = 0.0f;
    uint32 widget_counter_ = 0;

    String status_;
    bool status_error_ = false;
    float32 status_time_ = 0.0f;
    Array<String> problems_;
    Array<String> map_names_;
    int32 map_index_ = 0;
    Array<String> prop_files_;
    int32 prop_index_ = 0;

    // GPU resources owned by the editor
    TextureHandle grid_texture_;
    MeshHandle sphere_mesh_;
    std::map<SizeKey, MeshHandle> box_meshes_;
    std::map<String, Renderer::Model> prop_models_;
};

} // namespace Shift

#pragma once

/**
 * SHIFT - Map editor (inside the game)
 *
 * Builds and edits Level descriptions: blocks, moving walls, starts, core, exits and decoration.
 * Maps are saved as JSON below Assets/Maps and appear in the map choice of the menu.
 *
 * Layout: toolbar at the top (file, undo/redo, check, test, windows), floating windows that can be
 * moved by their title bar, minimised and closed: Bauen, Eigenschaften, Zustaende, Ansicht, Objekte,
 * Karte, Pruefung.
 *
 * Viewport: hold right mouse + WASD/QE to fly (Shift fast), wheel moves forward, left click selects
 * (Shift adds), drag moves on the grid (Alt: up/down), drag on empty space selects a rectangle.
 * Keys: Del, Ctrl+D, Ctrl+C/V, Ctrl+A, R (turn 90), arrows/PageUp/PageDown (nudge), Ctrl+Z/Y,
 * Ctrl+S, F (focus), G (grid), T (top view), Esc (clear selection).
 */

#include "../Core/ShiftConfig.h"
#include "../Core/ShiftLevel.h"

#include "Input/Public/InputSystem.h"
#include "Renderer/Public/ModelLoader.h"
#include "Renderer/Public/SceneRenderer.h"
#include "UI/Public/UISystem.h"

#include <initializer_list>
#include <map>
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
    struct UndoEntry {
        Level level;
        String action;
    };
    // Copied objects (positions relative to the copy centre)
    struct Clipboard {
        Array<LevelBox> boxes;
        Array<MovingSegmentDef> segments;
        Array<LevelProp> props;
        bool Empty() const { return boxes.empty() && segments.empty() && props.empty(); }
    };
    using SizeKey = std::tuple<int32, int32, int32>;

    enum PanelId : int32 { kPanelBuild, kPanelProps, kPanelStates, kPanelView, kPanelObjects, kPanelFile, kPanelCheck, kPanelCount };
    struct Panel {
        String title;
        float32 x = 0.0f;
        float32 y = 0.0f;
        float32 width = 300.0f;
        bool open = true;
        bool minimized = false;
        float32 content_height = 120.0f;
        float32 scroll = 0.0f;
    };
    enum Layer : int32 { kLayerFloors, kLayerWalls, kLayerCover, kLayerSteps, kLayerCeiling, kLayerMoving, kLayerProps, kLayerMarkers, kLayerCount };

    // Widgets (immediate mode, clipped to the window body)
    struct Layout {
        UI::UIContext* ui = nullptr;
        Input::InputSystem* input = nullptr;
        float32 x = 0.0f;
        float32 y = 0.0f;
        float32 width = 0.0f;
        float32 top = 0.0f;
        float32 bottom = 0.0f;
        bool interactive = false;
    };
    bool RowVisible(const Layout& l, float32 height) const;
    bool Hovered(const Layout& l, float32 x, float32 y, float32 w, float32 h) const;
    void Header(Layout& l, const String& text);
    void Label(Layout& l, const String& text, const Math::Vector4& color = {0.82f, 0.86f, 0.94f, 1.0f});
    bool Button(Layout& l, const String& label, float32 x, float32 width, bool highlight = false);
    bool FullButton(Layout& l, const String& label, bool highlight = false);
    int32 ButtonRow(Layout& l, std::initializer_list<const char*> labels, int32 highlight = -1);
    int32 ButtonGrid(Layout& l, const Array<String>& labels, int32 columns, int32 highlight = -1);
    bool Number(Layout& l, const String& label, float32& value, float32 step, float32 min_value, float32 max_value);
    bool Choice(Layout& l, const String& label, int32& index, const Array<String>& items);
    bool Toggle(Layout& l, const String& label, bool& value);
    void TextField(Layout& l, const String& label, String& text);
    bool ColorSwatches(Layout& l, Math::Vector4& color);

    // Windows and toolbar
    void SetupPanels();
    float32 PanelHeight(const Panel& panel) const;
    void DrawToolbar(UI::UIContext& ui, Input::InputSystem& input);
    void DrawPanel(UI::UIContext& ui, Input::InputSystem& input, PanelId id);
    void DrawBuildPanel(Layout& l);
    void DrawPropertiesPanel(Layout& l);
    void DrawStatesPanel(Layout& l);
    void DrawViewPanel(Layout& l);
    void DrawObjectsPanel(Layout& l);
    void DrawFilePanel(Layout& l);
    void DrawCheckPanel(Layout& l);
    void OpenPanel(PanelId id);
    void BringToFront(PanelId id);
    int32 PanelAt(float32 x, float32 y) const;

    // Editing
    void PushUndo(const String& action);
    void Undo();
    void Redo();
    void MarkChanged();
    Math::Vector3 PlacementPoint() const;
    void AddBox(const Math::Vector3& size, Surface surface, float32 bottom, bool select = true);
    void AddStairs();
    void AddRoom();
    void AddSegment(const Math::Vector3& size, const Math::Vector3& move, float32 rotate, SegmentGroup group);
    void AddRotatingRoom();
    void AddProp(const String& model);
    void DeleteSelection();
    void DuplicateSelection();
    void CopySelection();
    void PasteClipboard();
    void RotateSelection();
    void MoveSelection(const Math::Vector3& delta);
    void SelectAll();
    bool GetPosition(const Selection& s, Math::Vector3& out) const;
    Math::Vector3 SizeOf(const Selection& s) const;
    Selection Primary() const;
    bool IsSelected(const Selection& s) const;
    void Select(const Selection& s, bool additive);
    String Describe(const Selection& s) const;
    void SetStatus(const String& text, bool error = false);
    void NewLevel();
    void LoadLevelByName(const String& name);
    void SaveCurrent();
    void RunCheck();
    void RefreshFileLists();
    void ToggleTopView();
    void FocusSelection();

    // Picking and visibility
    Ray MouseRay(float32 mx, float32 my) const;
    bool Project(const Math::Vector3& point, Math::Vector2& out) const;
    bool Pick(const Ray& ray, Hit& hit) const;
    bool OverUI(float32 mx, float32 my) const;
    bool IsDraggable(const Selection& selection) const;
    float32 SnapCorner(float32 center, float32 size) const;
    float32 SnapValue(float32 value) const;
    // Moves `position` (centre of `s`) so its faces touch / align with nearby blocks and walls
    Math::Vector3 SnapToNeighbours(const Selection& s, const Math::Vector3& position) const;
    // Doors: cut an opening into wall block `index` around `along` (coordinate on the wall axis)
    bool CutOpening(size_t index, float32 along, float32 width);
    void InsertDoor(size_t wall_index, int32 type);   // 0 sinking, 1 sliding, 2 open passage
    void FitSegmentIntoWall(size_t segment_index);
    Layer BoxLayer(const LevelBox& box) const;
    bool BoxVisible(const LevelBox& box) const;

    // Rendering
    MeshHandle BoxMesh(Renderer::SceneRenderer& renderer, const Math::Vector3& size);
    const Renderer::Model* PropModel(Renderer::SceneRenderer& renderer, const String& model);
    SegmentPose PreviewPose(const MovingSegmentDef& def) const;
    void DrawOutline(const Selection& s, const Math::Vector4& color);

    bool open_ = false;
    const ShiftConfig* config_ = nullptr;
    String asset_dir_;
    String map_dir_;
    Level level_;
    String file_name_;
    bool dirty_ = false;
    bool confirm_exit_ = false;
    Request request_ = Request::None;

    Math::Vector3 cam_pos_ = {0.0f, 22.0f, 26.0f};
    float32 cam_yaw_ = 0.0f;
    float32 cam_pitch_ = -45.0f;
    float32 cam_speed_ = 12.0f;
    float32 fov_ = 70.0f;
    bool top_view_ = false;
    Math::Vector3 saved_cam_pos_;
    float32 saved_cam_yaw_ = 0.0f;
    float32 saved_cam_pitch_ = 0.0f;
    float32 screen_w_ = 1600.0f;
    float32 screen_h_ = 900.0f;

    Array<Selection> selection_;
    Selection hover_;
    float32 grid_ = 0.5f;
    bool snap_ = true;
    bool snap_edges_ = true;
    float32 door_width_ = 3.0f;
    Math::Vector3 last_hit_point_;
    bool labels_ = true;
    bool group_labels_ = false;
    bool show_grid_ = true;
    bool layers_[kLayerCount] = {true, true, true, true, false, true, true, true};
    GroupMask preview_state_ = 0;
    int32 preview_pattern_ = 0;      // 0 = round start, else ShiftPattern
    bool playing_ = false;
    float32 play_time_ = 0.0f;
    int32 play_speed_ = 1;
    ShiftPlan play_plan_;
    float32 anim_time_ = 0.0f;
    int32 build_tab_ = 0;
    int32 new_group_ = 1;            // group for new moving walls
    float32 autosave_timer_ = 0.0f;

    Array<UndoEntry> undo_;
    Array<UndoEntry> redo_;
    Clipboard clipboard_;
    bool dragging_ = false;
    bool drag_moved_ = false;
    bool drag_vertical_ = false;
    float32 drag_plane_y_ = 0.0f;
    Math::Vector3 drag_offset_;
    Math::Vector3 drag_start_pos_;
    Math::Vector2 press_mouse_;
    bool marquee_ = false;
    Math::Vector2 marquee_start_;

    // Windows
    Panel panels_[kPanelCount];
    Array<int32> z_order_;
    int32 hovered_panel_ = -1;
    int32 dragged_panel_ = -1;
    Math::Vector2 panel_grab_;
    bool panels_ready_ = false;

    // Widget state
    uint32 active_widget_ = 0;
    float32 widget_start_x_ = 0.0f;
    float32 widget_start_value_ = 0.0f;
    bool widget_dragged_ = false;
    bool widget_pushed_undo_ = false;
    bool text_focus_ = false;
    uint32 focus_field_ = 0;
    String number_edit_;
    Math::Vector2 mouse_;
    bool mouse_clicked_ = false;
    bool mouse_down_ = false;
    bool mouse_released_ = false;
    float32 scroll_ = 0.0f;
    uint32 widget_counter_ = 0;
    bool ui_consumed_click_ = false;

    String status_;
    bool status_error_ = false;
    float32 status_time_ = 0.0f;
    Array<String> problems_;
    bool checked_ = false;
    Array<String> map_names_;
    int32 map_index_ = 0;
    Array<String> prop_files_;
    float32 prop_scale_ = 1.0f;

    // GPU resources owned by the editor
    TextureHandle grid_texture_;
    MeshHandle sphere_mesh_;
    std::map<SizeKey, MeshHandle> box_meshes_;
    std::map<String, Renderer::Model> prop_models_;
};

} // namespace Shift

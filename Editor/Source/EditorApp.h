/**
 * DRX Engine - Visual Editor
 * Ein kompletter visueller Editor zum Erstellen von Spielen
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Vector4.h"
#include "Scene/Public/SceneSerialization.h"
#include <functional>

namespace DRX::Editor {

// ============================================================================
// Editor Konfiguration
// ============================================================================

struct EditorConfig {
    String window_title = "DRX Engine Editor";
    uint32 window_width = 1920;
    uint32 window_height = 1080;
    bool vsync = true;
    bool dark_mode = true;
    String last_project_path;
    String last_scene_path;
};

// ============================================================================
// Editor Panel Basis
// ============================================================================

class EditorPanel {
public:
    virtual ~EditorPanel() = default;

    virtual void OnInit() {}
    virtual void OnShutdown() {}
    virtual void OnUpdate(float32 delta_time) {}
    virtual void OnRender() = 0;

    void SetVisible(bool visible) { visible_ = visible; }
    bool IsVisible() const { return visible_; }

    const String& GetName() const { return name_; }

protected:
    String name_ = "Panel";
    bool visible_ = true;
    bool focused_ = false;
};

// ============================================================================
// Szenen-Hierarchie Panel - Zeigt alle Objekte im Spiel
// ============================================================================

class HierarchyPanel : public EditorPanel {
public:
    HierarchyPanel();

    void OnRender() override;

    void SetScene(Scene::SceneData* scene) { scene_ = scene; }
    Scene::EntityData* GetSelectedEntity() { return selected_entity_; }

    // Callbacks
    std::function<void(Scene::EntityData*)> on_entity_selected;
    std::function<void()> on_entity_created;
    std::function<void(uint64)> on_entity_deleted;

private:
    void DrawEntityNode(Scene::EntityData& entity);
    void DrawContextMenu();

    Scene::SceneData* scene_ = nullptr;
    Scene::EntityData* selected_entity_ = nullptr;
    uint64 dragged_entity_id_ = 0;
};

// ============================================================================
// Inspector Panel - Bearbeite Eigenschaften des ausgewählten Objekts
// ============================================================================

class InspectorPanel : public EditorPanel {
public:
    InspectorPanel();

    void OnRender() override;

    void SetSelectedEntity(Scene::EntityData* entity) { entity_ = entity; }

private:
    void DrawTransformComponent();
    void DrawComponents();
    void DrawAddComponentButton();

    Scene::EntityData* entity_ = nullptr;
};

// ============================================================================
// Asset Browser - Durchsuche und importiere Dateien
// ============================================================================

struct AssetItem {
    String name;
    String path;
    String extension;
    bool is_directory = false;
    uint64 size = 0;

    enum class Type {
        Unknown,
        Folder,
        Texture,
        Model,
        Audio,
        Script,
        Scene,
        Prefab,
        Shader,
        Material
    } type = Type::Unknown;
};

class AssetBrowserPanel : public EditorPanel {
public:
    AssetBrowserPanel();

    void OnRender() override;

    void SetRootPath(const String& path);
    void Refresh();

    std::function<void(const AssetItem&)> on_asset_selected;
    std::function<void(const AssetItem&)> on_asset_double_clicked;

private:
    void ScanDirectory(const String& path);
    void DrawPathBar();
    void DrawAssetGrid();
    void DrawAssetItem(const AssetItem& item, int index);
    AssetItem::Type GetAssetType(const String& extension);
    const char* GetAssetIcon(AssetItem::Type type);

    String root_path_;
    String current_path_;
    Array<AssetItem> current_items_;
    Array<String> path_history_;
    int selected_index_ = -1;
    float icon_size_ = 80.0f;
};

// ============================================================================
// Viewport Panel - 3D Ansicht der Szene
// ============================================================================

class ViewportPanel : public EditorPanel {
public:
    ViewportPanel();

    void OnRender() override;
    void OnUpdate(float32 delta_time) override;

    void SetRenderTexture(void* texture_id) { render_texture_ = texture_id; }

    // Gizmo Modus
    enum class GizmoMode { Translate, Rotate, Scale };
    void SetGizmoMode(GizmoMode mode) { gizmo_mode_ = mode; }
    GizmoMode GetGizmoMode() const { return gizmo_mode_; }

    // Kamera
    Math::Vector3 GetCameraPosition() const { return camera_position_; }
    Math::Vector3 GetCameraRotation() const { return camera_rotation_; }

    std::function<void(Scene::EntityData*, const Math::Vector3&)> on_entity_moved;

private:
    void HandleCameraInput(float32 delta_time);
    void DrawToolbar();
    void DrawGizmos();
    void DrawGrid();

    void* render_texture_ = nullptr;
    GizmoMode gizmo_mode_ = GizmoMode::Translate;

    // Kamera
    Math::Vector3 camera_position_ = {0, 5, -10};
    Math::Vector3 camera_rotation_ = {20, 0, 0};  // Pitch, Yaw, Roll
    float32 camera_speed_ = 10.0f;
    float32 camera_sensitivity_ = 0.3f;

    // Viewport State
    bool is_focused_ = false;
    bool is_hovered_ = false;
    Math::Vector2 viewport_size_ = {800, 600};
    Math::Vector2 viewport_bounds_[2];
};

// ============================================================================
// Console Panel - Zeigt Logs und erlaubt Befehle
// ============================================================================

struct ConsoleMessage {
    enum class Level { Info, Warning, Error, Debug };
    Level level;
    String message;
    String timestamp;
    int count = 1;
};

class ConsolePanel : public EditorPanel {
public:
    ConsolePanel();

    void OnRender() override;

    void AddLog(ConsoleMessage::Level level, const String& message);
    void Clear();

    static ConsolePanel& Instance();

private:
    void DrawToolbar();
    void DrawMessages();
    void DrawInputField();
    void ExecuteCommand(const String& command);

    Array<ConsoleMessage> messages_;
    char input_buffer_[256] = {0};
    bool scroll_to_bottom_ = true;
    bool show_info_ = true;
    bool show_warnings_ = true;
    bool show_errors_ = true;
};

// ============================================================================
// Toolbar - Hauptwerkzeugleiste
// ============================================================================

class Toolbar {
public:
    void OnRender();

    bool IsPlaying() const { return is_playing_; }
    bool IsPaused() const { return is_paused_; }

    std::function<void()> on_play;
    std::function<void()> on_pause;
    std::function<void()> on_stop;
    std::function<void()> on_step;

private:
    bool is_playing_ = false;
    bool is_paused_ = false;
};

// ============================================================================
// Hauptmenü
// ============================================================================

class MainMenu {
public:
    void OnRender();

    // File Menü Callbacks
    std::function<void()> on_new_scene;
    std::function<void()> on_open_scene;
    std::function<void()> on_save_scene;
    std::function<void()> on_save_scene_as;
    std::function<void()> on_exit;

    // Edit Menü Callbacks
    std::function<void()> on_undo;
    std::function<void()> on_redo;
    std::function<void()> on_copy;
    std::function<void()> on_paste;
    std::function<void()> on_duplicate;
    std::function<void()> on_delete;

    // GameObject Menü Callbacks
    std::function<void()> on_create_empty;
    std::function<void()> on_create_cube;
    std::function<void()> on_create_sphere;
    std::function<void()> on_create_plane;
    std::function<void()> on_create_light;
    std::function<void()> on_create_camera;

    // Window Menü Callbacks
    std::function<void(bool)> on_toggle_hierarchy;
    std::function<void(bool)> on_toggle_inspector;
    std::function<void(bool)> on_toggle_asset_browser;
    std::function<void(bool)> on_toggle_console;

private:
    void DrawFileMenu();
    void DrawEditMenu();
    void DrawGameObjectMenu();
    void DrawWindowMenu();
    void DrawHelpMenu();

    bool show_hierarchy_ = true;
    bool show_inspector_ = true;
    bool show_asset_browser_ = true;
    bool show_console_ = true;
};

// ============================================================================
// Dialog Fenster
// ============================================================================

class DialogManager {
public:
    static DialogManager& Instance();

    void ShowOpenFileDialog(const String& title, const Array<String>& filters,
                            std::function<void(const String&)> callback);
    void ShowSaveFileDialog(const String& title, const Array<String>& filters,
                            std::function<void(const String&)> callback);
    void ShowMessageBox(const String& title, const String& message);
    void ShowConfirmDialog(const String& title, const String& message,
                           std::function<void(bool)> callback);

    void OnRender();

private:
    struct DialogState {
        bool open = false;
        String title;
        String message;
        Array<String> filters;
        std::function<void(const String&)> file_callback;
        std::function<void(bool)> confirm_callback;
        char path_buffer[512] = {0};
    };

    DialogState open_dialog_;
    DialogState save_dialog_;
    DialogState message_dialog_;
    DialogState confirm_dialog_;
};

// ============================================================================
// Editor Application - Hauptklasse
// ============================================================================

class EditorApplication {
public:
    static EditorApplication& Instance();

    bool Initialize(const EditorConfig& config = {});
    void Shutdown();
    void Run();

    // Szenen Management
    void NewScene();
    void OpenScene(const String& path);
    void SaveScene();
    void SaveSceneAs(const String& path);

    // Entity Management
    Scene::EntityData* CreateEntity(const String& name = "New Entity");
    Scene::EntityData* CreateCube(const String& name = "Cube");
    Scene::EntityData* CreateSphere(const String& name = "Sphere");
    Scene::EntityData* CreatePlane(const String& name = "Plane");
    Scene::EntityData* CreateLight(const String& name = "Light");
    Scene::EntityData* CreateCamera(const String& name = "Camera");
    void DeleteEntity(uint64 entity_id);
    void DuplicateEntity(uint64 entity_id);

    // Playmode
    void Play();
    void Pause();
    void Stop();
    void Step();
    bool IsPlaying() const { return is_playing_; }

    // Undo/Redo
    void Undo();
    void Redo();

    // Getters
    Scene::SceneData* GetCurrentScene() { return &current_scene_; }
    Scene::EntityData* GetSelectedEntity() { return selected_entity_; }
    EditorConfig& GetConfig() { return config_; }

private:
    EditorApplication() = default;

    void SetupUI();
    void SetupCallbacks();
    void Update(float32 delta_time);
    void Render();
    void RenderDockspace();
    void HandleShortcuts();

    EditorConfig config_;
    bool running_ = false;
    bool is_playing_ = false;
    bool is_paused_ = false;

    // Aktuelle Szene
    Scene::SceneData current_scene_;
    Scene::EntityData* selected_entity_ = nullptr;
    String current_scene_path_;
    bool scene_modified_ = false;

    // UI Panels
    UniquePtr<HierarchyPanel> hierarchy_panel_;
    UniquePtr<InspectorPanel> inspector_panel_;
    UniquePtr<AssetBrowserPanel> asset_browser_panel_;
    UniquePtr<ViewportPanel> viewport_panel_;
    UniquePtr<ConsolePanel> console_panel_;
    UniquePtr<MainMenu> main_menu_;
    UniquePtr<Toolbar> toolbar_;

    // Entity ID Counter
    uint64 next_entity_id_ = 1;
};

// ============================================================================
// Logging Makros für Editor
// ============================================================================

#define EDITOR_LOG(msg) DRX::Editor::ConsolePanel::Instance().AddLog(DRX::Editor::ConsoleMessage::Level::Info, msg)
#define EDITOR_WARN(msg) DRX::Editor::ConsolePanel::Instance().AddLog(DRX::Editor::ConsoleMessage::Level::Warning, msg)
#define EDITOR_ERROR(msg) DRX::Editor::ConsolePanel::Instance().AddLog(DRX::Editor::ConsoleMessage::Level::Error, msg)

} // namespace DRX::Editor

/**
 * DRX Engine - Visual Editor
 * Complete visual editor for game creation using ImGui
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

// Forward declarations
struct GLFWwindow;

namespace DRX::Editor {

// ============================================================================
// Type Aliases
// ============================================================================
using String = std::string;
template<typename T> using Array = std::vector<T>;
template<typename T> using UniquePtr = std::unique_ptr<T>;
template<typename K, typename V> using HashMap = std::unordered_map<K, V>;
using uint32 = unsigned int;
using uint64 = unsigned long long;
using int32 = int;
using float32 = float;

// ============================================================================
// Math Types (simplified for editor)
// ============================================================================
namespace Math {
    struct Vector2 { float x = 0, y = 0; };
    struct Vector3 {
        float x = 0, y = 0, z = 0;
        static Vector3 Zero() { return {0, 0, 0}; }
        static Vector3 One() { return {1, 1, 1}; }
        Vector3 operator+(const Vector3& v) const { return {x+v.x, y+v.y, z+v.z}; }
        Vector3 operator-(const Vector3& v) const { return {x-v.x, y-v.y, z-v.z}; }
        Vector3 operator*(float f) const { return {x*f, y*f, z*f}; }
    };
    struct Vector4 { float x = 0, y = 0, z = 0, w = 1; };
    struct Quaternion {
        float x = 0, y = 0, z = 0, w = 1;
        static Quaternion Identity() { return {0, 0, 0, 1}; }
        Vector3 ToEuler() const { return {0, 0, 0}; } // Simplified
        static Quaternion FromEuler(const Vector3& euler) { return Identity(); }
    };
    struct Transform {
        Vector3 position;
        Quaternion rotation;
        Vector3 scale = Vector3::One();
    };
}

// ============================================================================
// Scene Data Structures
// ============================================================================
namespace Scene {
    struct ComponentData {
        String type;
        bool enabled = true;
        HashMap<String, String> properties;
    };

    struct EntityData {
        uint64 id = 0;
        String name;
        bool enabled = true;
        uint64 parent_id = 0;
        Math::Transform transform;
        Array<ComponentData> components;
        Array<String> tags;
    };

    struct SceneData {
        String name = "Untitled Scene";
        String version = "1.0";
        Array<EntityData> entities;
    };
}

// ============================================================================
// Editor Configuration
// ============================================================================
struct EditorConfig {
    String window_title = "DRX Engine Editor";
    uint32 window_width = 1920;
    uint32 window_height = 1080;
    bool vsync = true;
    bool dark_mode = true;
};

// ============================================================================
// Asset Browser Types
// ============================================================================
struct AssetItem {
    String name;
    String path;
    String extension;
    bool is_directory = false;
    uint64 size = 0;

    enum class Type {
        Unknown, Folder, Texture, Model, Audio, Script, Scene, Prefab, Shader, Material
    } type = Type::Unknown;
};

// ============================================================================
// Console Message
// ============================================================================
struct ConsoleMessage {
    enum class Level { Info, Warning, Error, Debug };
    Level level;
    String message;
    String timestamp;
    int count = 1;
};

// ============================================================================
// Editor Application - Main Class
// ============================================================================
class EditorApplication {
public:
    static EditorApplication& Instance();

    bool Initialize(const EditorConfig& config = {});
    void Shutdown();
    void Run();

    // Scene Management
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

    // Play Mode
    void Play();
    void Pause();
    void Stop();
    bool IsPlaying() const { return is_playing_; }

    // Console
    void Log(const String& message);
    void LogWarning(const String& message);
    void LogError(const String& message);

    // Getters
    Scene::SceneData* GetCurrentScene() { return &current_scene_; }
    Scene::EntityData* GetSelectedEntity() { return selected_entity_; }
    EditorConfig& GetConfig() { return config_; }

private:
    EditorApplication() = default;

    bool InitWindow();
    bool InitImGui();
    void ShutdownImGui();
    void ShutdownWindow();

    void Update();
    void Render();
    void RenderUI();

    // UI Panels
    void RenderMainMenu();
    void RenderToolbar();
    void RenderHierarchy();
    void RenderInspector();
    void RenderAssetBrowser();
    void RenderViewport();
    void RenderConsole();

    // Helpers
    void RenderEntityNode(Scene::EntityData& entity);
    void RenderTransformComponent();
    void RenderComponents();
    void RenderAddComponentPopup();
    void AddLog(ConsoleMessage::Level level, const String& message);
    const char* GetComponentIcon(const String& type);
    AssetItem::Type GetAssetType(const String& extension);
    const char* GetAssetIcon(AssetItem::Type type);
    void ScanDirectory(const String& path);
    void HandleShortcuts();

    // GLFW
    GLFWwindow* window_ = nullptr;

    // State
    EditorConfig config_;
    bool running_ = false;
    bool is_playing_ = false;
    bool is_paused_ = false;

    // Scene
    Scene::SceneData current_scene_;
    Scene::EntityData* selected_entity_ = nullptr;
    String current_scene_path_;
    bool scene_modified_ = false;
    uint64 next_entity_id_ = 1;

    // UI State
    bool show_hierarchy_ = true;
    bool show_inspector_ = true;
    bool show_asset_browser_ = true;
    bool show_console_ = true;
    bool show_viewport_ = true;
    bool show_demo_window_ = false;

    // Viewport
    float camera_pos_[3] = {0, 5, -10};
    float camera_rot_[2] = {20, 0}; // pitch, yaw
    float camera_speed_ = 10.0f;
    bool viewport_focused_ = false;

    // Asset Browser
    String asset_root_path_ = "./Assets";
    String asset_current_path_ = "./Assets";
    Array<AssetItem> asset_items_;
    int selected_asset_ = -1;
    float icon_size_ = 80.0f;

    // Console
    Array<ConsoleMessage> console_messages_;
    char console_input_[256] = {0};
    bool console_scroll_to_bottom_ = true;
    bool show_info_ = true;
    bool show_warnings_ = true;
    bool show_errors_ = true;

    // Gizmo
    enum class GizmoMode { Translate, Rotate, Scale };
    GizmoMode gizmo_mode_ = GizmoMode::Translate;
};

// Convenience macros
#define EDITOR_LOG(msg) DRX::Editor::EditorApplication::Instance().Log(msg)
#define EDITOR_WARN(msg) DRX::Editor::EditorApplication::Instance().LogWarning(msg)
#define EDITOR_ERROR(msg) DRX::Editor::EditorApplication::Instance().LogError(msg)

} // namespace DRX::Editor

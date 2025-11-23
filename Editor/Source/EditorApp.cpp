/**
 * DRX Engine - Visual Editor Implementation
 */

#include "EditorApp.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <cstring>

namespace fs = std::filesystem;

namespace DRX::Editor {

// ============================================================================
// HierarchyPanel Implementation
// ============================================================================

HierarchyPanel::HierarchyPanel() {
    name_ = "Hierarchy";
}

void HierarchyPanel::OnRender() {
    if (!visible_) return;

    // Fenster Titel mit Icon
    if (BeginPanel("🌳 Szenen-Hierarchie")) {
        // Suchfeld
        static char search_buffer[128] = "";
        DrawSearchBar(search_buffer, sizeof(search_buffer), "Objekte suchen...");

        Separator();

        // "Neues Objekt" Button
        if (Button("+ Neues Objekt", {-1, 30})) {
            if (on_entity_created) on_entity_created();
        }

        Separator();

        // Entity Liste
        if (scene_) {
            for (auto& entity : scene_->entities) {
                // Filter by search
                if (strlen(search_buffer) > 0) {
                    if (entity.name.find(search_buffer) == String::npos) {
                        continue;
                    }
                }

                // Nur Root-Entities (ohne Parent)
                if (entity.parent_id == 0) {
                    DrawEntityNode(entity);
                }
            }
        } else {
            TextDisabled("Keine Szene geladen");
        }

        // Rechtsklick-Menü
        DrawContextMenu();
    }
    EndPanel();
}

void HierarchyPanel::DrawEntityNode(Scene::EntityData& entity) {
    // Flags für den Tree Node
    int flags = TreeNodeFlags_OpenOnArrow | TreeNodeFlags_SpanAvailWidth;

    if (selected_entity_ && selected_entity_->id == entity.id) {
        flags |= TreeNodeFlags_Selected;
    }

    // Prüfe ob Entity Kinder hat
    bool has_children = false;
    if (scene_) {
        for (const auto& e : scene_->entities) {
            if (e.parent_id == entity.id) {
                has_children = true;
                break;
            }
        }
    }

    if (!has_children) {
        flags |= TreeNodeFlags_Leaf;
    }

    // Icon basierend auf Entity-Typ
    String label = GetEntityIcon(entity) + " " + entity.name;

    bool is_open = TreeNodeEx(entity.id, flags, label.c_str());

    // Klick-Handling
    if (IsItemClicked()) {
        selected_entity_ = &entity;
        if (on_entity_selected) {
            on_entity_selected(&entity);
        }
    }

    // Drag & Drop
    if (BeginDragDropSource()) {
        dragged_entity_id_ = entity.id;
        SetDragDropPayload("ENTITY", &entity.id, sizeof(uint64));
        Text("📦 %s", entity.name.c_str());
        EndDragDropSource();
    }

    if (BeginDragDropTarget()) {
        if (const void* payload = AcceptDragDropPayload("ENTITY")) {
            uint64 dropped_id = *(const uint64*)payload;
            // Setze Parent
            if (scene_) {
                for (auto& e : scene_->entities) {
                    if (e.id == dropped_id) {
                        e.parent_id = entity.id;
                        break;
                    }
                }
            }
        }
        EndDragDropTarget();
    }

    // Kinder zeichnen
    if (is_open) {
        if (scene_) {
            for (auto& child : scene_->entities) {
                if (child.parent_id == entity.id) {
                    DrawEntityNode(child);
                }
            }
        }
        TreePop();
    }
}

void HierarchyPanel::DrawContextMenu() {
    if (BeginPopupContextWindow("HierarchyContextMenu")) {
        if (MenuItem("🆕 Leeres Objekt erstellen")) {
            if (on_entity_created) on_entity_created();
        }

        Separator();

        if (MenuItem("📦 Würfel")) {
            // Create cube
        }
        if (MenuItem("⚽ Kugel")) {
            // Create sphere
        }
        if (MenuItem("🟫 Ebene")) {
            // Create plane
        }

        Separator();

        if (MenuItem("💡 Licht")) {
            // Create light
        }
        if (MenuItem("📷 Kamera")) {
            // Create camera
        }

        Separator();

        if (selected_entity_) {
            if (MenuItem("📋 Kopieren")) {}
            if (MenuItem("📄 Duplizieren")) {}
            if (MenuItem("🗑️ Löschen")) {
                if (on_entity_deleted) {
                    on_entity_deleted(selected_entity_->id);
                }
            }
        }

        EndPopup();
    }
}

String HierarchyPanel::GetEntityIcon(const Scene::EntityData& entity) {
    // Prüfe Komponenten für passendes Icon
    for (const auto& comp : entity.components) {
        if (comp.type == "Camera") return "📷";
        if (comp.type == "Light") return "💡";
        if (comp.type == "MeshRenderer") return "🎨";
        if (comp.type == "AudioSource") return "🔊";
        if (comp.type == "RigidBody") return "⚙️";
        if (comp.type == "Script") return "📜";
    }
    return "📦";  // Default
}

// ============================================================================
// InspectorPanel Implementation
// ============================================================================

InspectorPanel::InspectorPanel() {
    name_ = "Inspector";
}

void InspectorPanel::OnRender() {
    if (!visible_) return;

    if (BeginPanel("🔍 Inspector")) {
        if (entity_) {
            // Entity Name
            char name_buffer[128];
            strncpy(name_buffer, entity_->name.c_str(), sizeof(name_buffer) - 1);
            name_buffer[sizeof(name_buffer) - 1] = '\0';

            Text("Name:");
            SameLine();
            if (InputText("##EntityName", name_buffer, sizeof(name_buffer))) {
                entity_->name = name_buffer;
            }

            // Enabled Checkbox
            Checkbox("Aktiv", &entity_->enabled);

            // Tags
            Text("Tags: ");
            SameLine();
            for (const auto& tag : entity_->tags) {
                SameLine();
                SmallButton(tag.c_str());
            }
            SameLine();
            if (SmallButton("+")) {
                // Add tag dialog
            }

            Separator();

            // Transform Component (immer vorhanden)
            DrawTransformComponent();

            Separator();

            // Andere Komponenten
            DrawComponents();

            Separator();

            // Add Component Button
            DrawAddComponentButton();
        } else {
            TextDisabled("Kein Objekt ausgewählt");
            Text("");
            Text("Wähle ein Objekt in der");
            Text("Hierarchie aus um seine");
            Text("Eigenschaften zu bearbeiten.");
        }
    }
    EndPanel();
}

void InspectorPanel::DrawTransformComponent() {
    if (!entity_) return;

    if (CollapsingHeader("📐 Transform", TreeNodeFlags_DefaultOpen)) {
        // Position
        Text("Position");
        float pos[3] = {entity_->transform.position.x,
                        entity_->transform.position.y,
                        entity_->transform.position.z};
        if (DragFloat3("##Position", pos, 0.1f)) {
            entity_->transform.position = {pos[0], pos[1], pos[2]};
        }

        // Rotation (als Euler-Winkel anzeigen)
        Text("Rotation");
        Math::Vector3 euler = entity_->transform.rotation.ToEuler();
        float rot[3] = {euler.x, euler.y, euler.z};
        if (DragFloat3("##Rotation", rot, 1.0f)) {
            entity_->transform.rotation = Math::Quaternion::FromEuler({rot[0], rot[1], rot[2]});
        }

        // Scale
        Text("Skalierung");
        float scale[3] = {entity_->transform.scale.x,
                          entity_->transform.scale.y,
                          entity_->transform.scale.z};
        if (DragFloat3("##Scale", scale, 0.1f)) {
            entity_->transform.scale = {scale[0], scale[1], scale[2]};
        }

        // Reset Button
        if (Button("Zurücksetzen")) {
            entity_->transform.position = Math::Vector3::Zero;
            entity_->transform.rotation = Math::Quaternion::Identity;
            entity_->transform.scale = Math::Vector3::One;
        }
    }
}

void InspectorPanel::DrawComponents() {
    if (!entity_) return;

    for (size_t i = 0; i < entity_->components.size(); ++i) {
        auto& component = entity_->components[i];

        String header = GetComponentIcon(component.type) + " " + component.type;

        bool remove_component = false;

        // Component Header mit Options
        if (CollapsingHeader(header.c_str(), TreeNodeFlags_DefaultOpen)) {
            // Enabled Checkbox
            Checkbox(("##Enabled" + std::to_string(i)).c_str(), &component.enabled);
            SameLine();
            Text("Aktiviert");

            // Component-spezifische UI
            DrawComponentUI(component);

            // Remove Button
            if (Button(("Entfernen##" + std::to_string(i)).c_str())) {
                remove_component = true;
            }
        }

        if (remove_component) {
            entity_->components.erase(entity_->components.begin() + i);
            break;
        }
    }
}

void InspectorPanel::DrawComponentUI(Scene::ComponentData& component) {
    // Je nach Komponenten-Typ verschiedene UI
    if (component.type == "MeshRenderer") {
        Text("Mesh:");
        // Mesh Auswahl
        if (Button("Mesh auswählen...")) {
            // Open asset picker
        }
        Text("Material:");
        if (Button("Material auswählen...")) {
            // Open asset picker
        }
    }
    else if (component.type == "Camera") {
        static float fov = 60.0f;
        static float near_plane = 0.1f;
        static float far_plane = 1000.0f;

        SliderFloat("Sichtfeld (FOV)", &fov, 30.0f, 120.0f);
        DragFloat("Nahe Ebene", &near_plane, 0.01f, 0.01f, 10.0f);
        DragFloat("Ferne Ebene", &far_plane, 1.0f, 10.0f, 10000.0f);
    }
    else if (component.type == "Light") {
        static int light_type = 0;
        static float color[3] = {1, 1, 1};
        static float intensity = 1.0f;

        const char* types[] = {"Directional", "Point", "Spot"};
        Combo("Typ", &light_type, types, 3);
        ColorEdit3("Farbe", color);
        SliderFloat("Intensität", &intensity, 0.0f, 10.0f);
    }
    else if (component.type == "RigidBody") {
        static float mass = 1.0f;
        static bool use_gravity = true;
        static bool is_kinematic = false;

        DragFloat("Masse", &mass, 0.1f, 0.0f, 1000.0f);
        Checkbox("Schwerkraft", &use_gravity);
        Checkbox("Kinematisch", &is_kinematic);
    }
    else if (component.type == "AudioSource") {
        Text("Audio Clip:");
        if (Button("Audio auswählen...")) {}

        static float volume = 1.0f;
        static bool loop = false;
        static bool play_on_start = false;

        SliderFloat("Lautstärke", &volume, 0.0f, 1.0f);
        Checkbox("Wiederholen", &loop);
        Checkbox("Auto-Play", &play_on_start);
    }
    else if (component.type == "Script") {
        Text("Script Datei:");
        if (Button("Script auswählen...")) {}
    }
}

void InspectorPanel::DrawAddComponentButton() {
    if (Button("+ Komponente hinzufügen", {-1, 35})) {
        OpenPopup("AddComponentPopup");
    }

    if (BeginPopup("AddComponentPopup")) {
        Text("Komponente auswählen:");
        Separator();

        if (MenuItem("🎨 Mesh Renderer")) {
            AddComponent("MeshRenderer");
        }
        if (MenuItem("📷 Kamera")) {
            AddComponent("Camera");
        }
        if (MenuItem("💡 Licht")) {
            AddComponent("Light");
        }
        if (MenuItem("⚙️ Rigidbody (Physik)")) {
            AddComponent("RigidBody");
        }
        if (MenuItem("🔊 Audio Source")) {
            AddComponent("AudioSource");
        }
        if (MenuItem("📜 Script")) {
            AddComponent("Script");
        }
        if (MenuItem("🎬 Animator")) {
            AddComponent("Animator");
        }
        if (MenuItem("💥 Partikel System")) {
            AddComponent("ParticleSystem");
        }

        EndPopup();
    }
}

void InspectorPanel::AddComponent(const String& type) {
    if (!entity_) return;

    Scene::ComponentData comp;
    comp.type = type;
    comp.enabled = true;
    entity_->components.push_back(comp);
}

String InspectorPanel::GetComponentIcon(const String& type) {
    if (type == "MeshRenderer") return "🎨";
    if (type == "Camera") return "📷";
    if (type == "Light") return "💡";
    if (type == "RigidBody") return "⚙️";
    if (type == "AudioSource") return "🔊";
    if (type == "Script") return "📜";
    if (type == "Animator") return "🎬";
    if (type == "ParticleSystem") return "💥";
    return "📦";
}

// ============================================================================
// AssetBrowserPanel Implementation
// ============================================================================

AssetBrowserPanel::AssetBrowserPanel() {
    name_ = "Assets";
}

void AssetBrowserPanel::OnRender() {
    if (!visible_) return;

    if (BeginPanel("📁 Asset Browser")) {
        DrawPathBar();
        Separator();

        // Toolbar
        if (Button("⬅️")) {
            // Go back
            if (current_path_ != root_path_ && !path_history_.empty()) {
                current_path_ = fs::path(current_path_).parent_path().string();
                Refresh();
            }
        }
        SameLine();
        if (Button("🔄")) {
            Refresh();
        }
        SameLine();
        if (Button("📁+")) {
            // Create folder
        }

        SliderFloat("Icon Größe", &icon_size_, 40.0f, 150.0f);

        Separator();

        DrawAssetGrid();
    }
    EndPanel();
}

void AssetBrowserPanel::SetRootPath(const String& path) {
    root_path_ = path;
    current_path_ = path;
    Refresh();
}

void AssetBrowserPanel::Refresh() {
    current_items_.clear();
    ScanDirectory(current_path_);
}

void AssetBrowserPanel::ScanDirectory(const String& path) {
    if (!fs::exists(path)) return;

    for (const auto& entry : fs::directory_iterator(path)) {
        AssetItem item;
        item.path = entry.path().string();
        item.name = entry.path().filename().string();
        item.is_directory = entry.is_directory();

        if (item.is_directory) {
            item.type = AssetItem::Type::Folder;
        } else {
            item.extension = entry.path().extension().string();
            item.size = entry.file_size();
            item.type = GetAssetType(item.extension);
        }

        current_items_.push_back(item);
    }

    // Sortieren: Ordner zuerst, dann alphabetisch
    std::sort(current_items_.begin(), current_items_.end(),
        [](const AssetItem& a, const AssetItem& b) {
            if (a.is_directory != b.is_directory) {
                return a.is_directory;
            }
            return a.name < b.name;
        });
}

void AssetBrowserPanel::DrawPathBar() {
    Text("📍 ");
    SameLine();

    // Pfad als klickbare Breadcrumbs
    fs::path p(current_path_);
    String accumulated;

    for (const auto& part : p) {
        if (accumulated.empty()) {
            accumulated = part.string();
        } else {
            accumulated += "/" + part.string();
        }

        if (SmallButton(part.string().c_str())) {
            current_path_ = accumulated;
            Refresh();
        }
        SameLine();
        Text("/");
        SameLine();
    }
    NewLine();
}

void AssetBrowserPanel::DrawAssetGrid() {
    float panel_width = GetContentRegionAvail().x;
    int columns = std::max(1, (int)(panel_width / (icon_size_ + 10)));

    BeginChild("AssetGrid");

    int column = 0;
    for (size_t i = 0; i < current_items_.size(); ++i) {
        DrawAssetItem(current_items_[i], static_cast<int>(i));

        column++;
        if (column < columns) {
            SameLine();
        } else {
            column = 0;
        }
    }

    EndChild();
}

void AssetBrowserPanel::DrawAssetItem(const AssetItem& item, int index) {
    bool is_selected = (index == selected_index_);

    BeginGroup();

    // Icon Button
    PushStyleColor(StyleColor_Button,
        is_selected ? Vec4(0.3f, 0.5f, 0.8f, 1.0f) : Vec4(0.2f, 0.2f, 0.2f, 1.0f));

    if (Button((GetAssetIcon(item.type) + String("##") + std::to_string(index)).c_str(),
               {icon_size_, icon_size_})) {
        selected_index_ = index;
        if (on_asset_selected) {
            on_asset_selected(item);
        }
    }

    PopStyleColor();

    // Doppelklick
    if (IsItemHovered() && IsMouseDoubleClicked(0)) {
        if (item.is_directory) {
            current_path_ = item.path;
            Refresh();
        } else {
            if (on_asset_double_clicked) {
                on_asset_double_clicked(item);
            }
        }
    }

    // Drag & Drop Source
    if (BeginDragDropSource()) {
        SetDragDropPayload("ASSET", item.path.c_str(), item.path.size() + 1);
        Text("%s %s", GetAssetIcon(item.type), item.name.c_str());
        EndDragDropSource();
    }

    // Name (gekürzt wenn zu lang)
    String display_name = item.name;
    if (display_name.length() > 12) {
        display_name = display_name.substr(0, 10) + "...";
    }
    Text("%s", display_name.c_str());

    EndGroup();
}

AssetItem::Type AssetBrowserPanel::GetAssetType(const String& ext) {
    String lower_ext = ext;
    std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(), ::tolower);

    if (lower_ext == ".png" || lower_ext == ".jpg" || lower_ext == ".jpeg" ||
        lower_ext == ".tga" || lower_ext == ".bmp") {
        return AssetItem::Type::Texture;
    }
    if (lower_ext == ".obj" || lower_ext == ".fbx" || lower_ext == ".gltf" ||
        lower_ext == ".glb" || lower_ext == ".dae") {
        return AssetItem::Type::Model;
    }
    if (lower_ext == ".wav" || lower_ext == ".mp3" || lower_ext == ".ogg") {
        return AssetItem::Type::Audio;
    }
    if (lower_ext == ".lua" || lower_ext == ".py" || lower_ext == ".cpp") {
        return AssetItem::Type::Script;
    }
    if (lower_ext == ".drxscene") {
        return AssetItem::Type::Scene;
    }
    if (lower_ext == ".drxprefab") {
        return AssetItem::Type::Prefab;
    }
    if (lower_ext == ".vert" || lower_ext == ".frag" || lower_ext == ".glsl") {
        return AssetItem::Type::Shader;
    }
    if (lower_ext == ".drxmat") {
        return AssetItem::Type::Material;
    }

    return AssetItem::Type::Unknown;
}

const char* AssetBrowserPanel::GetAssetIcon(AssetItem::Type type) {
    switch (type) {
        case AssetItem::Type::Folder: return "📁";
        case AssetItem::Type::Texture: return "🖼️";
        case AssetItem::Type::Model: return "🎭";
        case AssetItem::Type::Audio: return "🔊";
        case AssetItem::Type::Script: return "📜";
        case AssetItem::Type::Scene: return "🎬";
        case AssetItem::Type::Prefab: return "📦";
        case AssetItem::Type::Shader: return "✨";
        case AssetItem::Type::Material: return "🎨";
        default: return "📄";
    }
}

// ============================================================================
// ViewportPanel Implementation
// ============================================================================

ViewportPanel::ViewportPanel() {
    name_ = "Viewport";
}

void ViewportPanel::OnUpdate(float32 delta_time) {
    if (is_focused_) {
        HandleCameraInput(delta_time);
    }
}

void ViewportPanel::OnRender() {
    if (!visible_) return;

    PushStyleVar(StyleVar_WindowPadding, Vec2(0, 0));

    if (BeginPanel("🎮 Viewport")) {
        DrawToolbar();

        // Viewport größe
        Vec2 size = GetContentRegionAvail();
        viewport_size_ = {size.x, size.y};

        // Render das Bild
        if (render_texture_) {
            Image(render_texture_, size);
        } else {
            // Platzhalter
            DrawRect(GetCursorScreenPos(),
                     {GetCursorScreenPos().x + size.x, GetCursorScreenPos().y + size.y},
                     Vec4(0.1f, 0.1f, 0.15f, 1.0f));

            // Zentrierter Text
            Vec2 text_size = CalcTextSize("3D Ansicht wird geladen...");
            SetCursorPos({(size.x - text_size.x) / 2, (size.y - text_size.y) / 2});
            Text("3D Ansicht wird geladen...");
        }

        // Prüfe ob Viewport fokussiert ist
        is_focused_ = IsWindowFocused();
        is_hovered_ = IsWindowHovered();

        // Gizmos zeichnen
        if (is_hovered_) {
            DrawGizmos();
        }
    }
    EndPanel();

    PopStyleVar();
}

void ViewportPanel::DrawToolbar() {
    // Gizmo Mode Buttons
    PushStyleVar(StyleVar_FrameRounding, 3.0f);

    if (RadioButton("↔️", gizmo_mode_ == GizmoMode::Translate)) {
        gizmo_mode_ = GizmoMode::Translate;
    }
    if (IsItemHovered()) SetTooltip("Verschieben (W)");

    SameLine();
    if (RadioButton("🔄", gizmo_mode_ == GizmoMode::Rotate)) {
        gizmo_mode_ = GizmoMode::Rotate;
    }
    if (IsItemHovered()) SetTooltip("Rotieren (E)");

    SameLine();
    if (RadioButton("⬛", gizmo_mode_ == GizmoMode::Scale)) {
        gizmo_mode_ = GizmoMode::Scale;
    }
    if (IsItemHovered()) SetTooltip("Skalieren (R)");

    SameLine();
    Separator();
    SameLine();

    // Kamera Info
    Text("📷 Pos: %.1f, %.1f, %.1f",
         camera_position_.x, camera_position_.y, camera_position_.z);

    PopStyleVar();

    Separator();
}

void ViewportPanel::HandleCameraInput(float32 delta_time) {
    // Rechte Maustaste für Kamera-Rotation
    if (IsMouseDown(1) && is_hovered_) {
        Vec2 delta = GetMouseDragDelta(1);
        camera_rotation_.y += delta.x * camera_sensitivity_;
        camera_rotation_.x += delta.y * camera_sensitivity_;
        camera_rotation_.x = std::clamp(camera_rotation_.x, -89.0f, 89.0f);
        ResetMouseDragDelta(1);
    }

    // WASD für Bewegung
    if (is_focused_) {
        Math::Vector3 move_dir = Math::Vector3::Zero;

        float rad = camera_rotation_.y * 3.14159265359f / 180.0f;
        Math::Vector3 forward = {sin(rad), 0, cos(rad)};
        Math::Vector3 right = {cos(rad), 0, -sin(rad)};

        if (IsKeyDown(Key_W)) move_dir = move_dir + forward;
        if (IsKeyDown(Key_S)) move_dir = move_dir - forward;
        if (IsKeyDown(Key_A)) move_dir = move_dir - right;
        if (IsKeyDown(Key_D)) move_dir = move_dir + right;
        if (IsKeyDown(Key_E)) move_dir.y += 1.0f;
        if (IsKeyDown(Key_Q)) move_dir.y -= 1.0f;

        float speed = camera_speed_;
        if (IsKeyDown(Key_LeftShift)) speed *= 3.0f;

        camera_position_ = camera_position_ + move_dir * speed * delta_time;
    }

    // Mausrad für Zoom
    float scroll = GetMouseWheel();
    if (scroll != 0 && is_hovered_) {
        float rad = camera_rotation_.y * 3.14159265359f / 180.0f;
        Math::Vector3 forward = {sin(rad), 0, cos(rad)};
        camera_position_ = camera_position_ + forward * scroll * 2.0f;
    }
}

void ViewportPanel::DrawGizmos() {
    // Gizmo-Zeichnung würde hier implementiert
    // Für echte Implementierung würde man ImGuizmo verwenden
}

void ViewportPanel::DrawGrid() {
    // Grid-Zeichnung
}

// ============================================================================
// ConsolePanel Implementation
// ============================================================================

ConsolePanel::ConsolePanel() {
    name_ = "Console";
}

ConsolePanel& ConsolePanel::Instance() {
    static ConsolePanel instance;
    return instance;
}

void ConsolePanel::OnRender() {
    if (!visible_) return;

    if (BeginPanel("📋 Console")) {
        DrawToolbar();
        Separator();
        DrawMessages();
        Separator();
        DrawInputField();
    }
    EndPanel();
}

void ConsolePanel::AddLog(ConsoleMessage::Level level, const String& message) {
    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", localtime(&time));

    // Prüfe ob letzte Nachricht gleich ist (zusammenfassen)
    if (!messages_.empty() &&
        messages_.back().level == level &&
        messages_.back().message == message) {
        messages_.back().count++;
    } else {
        messages_.push_back({level, message, timestamp, 1});
    }

    scroll_to_bottom_ = true;
}

void ConsolePanel::Clear() {
    messages_.clear();
}

void ConsolePanel::DrawToolbar() {
    if (Button("🗑️ Leeren")) {
        Clear();
    }
    SameLine();

    Checkbox("ℹ️", &show_info_);
    if (IsItemHovered()) SetTooltip("Info anzeigen");
    SameLine();

    Checkbox("⚠️", &show_warnings_);
    if (IsItemHovered()) SetTooltip("Warnungen anzeigen");
    SameLine();

    Checkbox("❌", &show_errors_);
    if (IsItemHovered()) SetTooltip("Fehler anzeigen");

    SameLine();
    Text("  |  %zu Nachrichten", messages_.size());
}

void ConsolePanel::DrawMessages() {
    BeginChild("ConsoleMessages", {0, -30}, true);

    for (const auto& msg : messages_) {
        // Filter
        if (msg.level == ConsoleMessage::Level::Info && !show_info_) continue;
        if (msg.level == ConsoleMessage::Level::Warning && !show_warnings_) continue;
        if (msg.level == ConsoleMessage::Level::Error && !show_errors_) continue;

        // Farbe je nach Level
        Vec4 color;
        const char* icon;
        switch (msg.level) {
            case ConsoleMessage::Level::Info:
                color = Vec4(0.8f, 0.8f, 0.8f, 1.0f);
                icon = "ℹ️";
                break;
            case ConsoleMessage::Level::Warning:
                color = Vec4(1.0f, 0.8f, 0.2f, 1.0f);
                icon = "⚠️";
                break;
            case ConsoleMessage::Level::Error:
                color = Vec4(1.0f, 0.3f, 0.3f, 1.0f);
                icon = "❌";
                break;
            case ConsoleMessage::Level::Debug:
                color = Vec4(0.5f, 0.5f, 1.0f, 1.0f);
                icon = "🔧";
                break;
        }

        TextColored(color, "[%s] %s %s", msg.timestamp.c_str(), icon, msg.message.c_str());

        if (msg.count > 1) {
            SameLine();
            Text("(x%d)", msg.count);
        }
    }

    if (scroll_to_bottom_) {
        SetScrollHereY(1.0f);
        scroll_to_bottom_ = false;
    }

    EndChild();
}

void ConsolePanel::DrawInputField() {
    PushItemWidth(-60);
    if (InputText("##ConsoleInput", input_buffer_, sizeof(input_buffer_),
                  InputTextFlags_EnterReturnsTrue)) {
        ExecuteCommand(input_buffer_);
        input_buffer_[0] = '\0';
    }
    PopItemWidth();

    SameLine();
    if (Button("Senden")) {
        ExecuteCommand(input_buffer_);
        input_buffer_[0] = '\0';
    }
}

void ConsolePanel::ExecuteCommand(const String& command) {
    if (command.empty()) return;

    AddLog(ConsoleMessage::Level::Info, "> " + command);

    // Einfache Befehle
    if (command == "help") {
        AddLog(ConsoleMessage::Level::Info, "Verfügbare Befehle:");
        AddLog(ConsoleMessage::Level::Info, "  help - Diese Hilfe");
        AddLog(ConsoleMessage::Level::Info, "  clear - Console leeren");
        AddLog(ConsoleMessage::Level::Info, "  version - Version anzeigen");
    }
    else if (command == "clear") {
        Clear();
    }
    else if (command == "version") {
        AddLog(ConsoleMessage::Level::Info, "DRX Engine v1.0.0");
    }
    else {
        AddLog(ConsoleMessage::Level::Warning, "Unbekannter Befehl: " + command);
    }
}

// ============================================================================
// MainMenu Implementation
// ============================================================================

void MainMenu::OnRender() {
    if (BeginMainMenuBar()) {
        DrawFileMenu();
        DrawEditMenu();
        DrawGameObjectMenu();
        DrawWindowMenu();
        DrawHelpMenu();
        EndMainMenuBar();
    }
}

void MainMenu::DrawFileMenu() {
    if (BeginMenu("📁 Datei")) {
        if (MenuItem("🆕 Neue Szene", "Ctrl+N")) {
            if (on_new_scene) on_new_scene();
        }
        if (MenuItem("📂 Szene öffnen...", "Ctrl+O")) {
            if (on_open_scene) on_open_scene();
        }

        Separator();

        if (MenuItem("💾 Speichern", "Ctrl+S")) {
            if (on_save_scene) on_save_scene();
        }
        if (MenuItem("💾 Speichern als...", "Ctrl+Shift+S")) {
            if (on_save_scene_as) on_save_scene_as();
        }

        Separator();

        if (MenuItem("🚪 Beenden", "Alt+F4")) {
            if (on_exit) on_exit();
        }

        EndMenu();
    }
}

void MainMenu::DrawEditMenu() {
    if (BeginMenu("✏️ Bearbeiten")) {
        if (MenuItem("↩️ Rückgängig", "Ctrl+Z")) {
            if (on_undo) on_undo();
        }
        if (MenuItem("↪️ Wiederholen", "Ctrl+Y")) {
            if (on_redo) on_redo();
        }

        Separator();

        if (MenuItem("📋 Kopieren", "Ctrl+C")) {
            if (on_copy) on_copy();
        }
        if (MenuItem("📄 Einfügen", "Ctrl+V")) {
            if (on_paste) on_paste();
        }
        if (MenuItem("📑 Duplizieren", "Ctrl+D")) {
            if (on_duplicate) on_duplicate();
        }

        Separator();

        if (MenuItem("🗑️ Löschen", "Delete")) {
            if (on_delete) on_delete();
        }

        EndMenu();
    }
}

void MainMenu::DrawGameObjectMenu() {
    if (BeginMenu("🎮 Objekte")) {
        if (MenuItem("📦 Leeres Objekt")) {
            if (on_create_empty) on_create_empty();
        }

        Separator();

        if (BeginMenu("🔷 3D Objekte")) {
            if (MenuItem("📦 Würfel")) {
                if (on_create_cube) on_create_cube();
            }
            if (MenuItem("⚽ Kugel")) {
                if (on_create_sphere) on_create_sphere();
            }
            if (MenuItem("🟫 Ebene")) {
                if (on_create_plane) on_create_plane();
            }
            EndMenu();
        }

        Separator();

        if (MenuItem("💡 Licht")) {
            if (on_create_light) on_create_light();
        }
        if (MenuItem("📷 Kamera")) {
            if (on_create_camera) on_create_camera();
        }

        EndMenu();
    }
}

void MainMenu::DrawWindowMenu() {
    if (BeginMenu("🪟 Fenster")) {
        if (MenuItem("🌳 Hierarchie", nullptr, &show_hierarchy_)) {
            if (on_toggle_hierarchy) on_toggle_hierarchy(show_hierarchy_);
        }
        if (MenuItem("🔍 Inspector", nullptr, &show_inspector_)) {
            if (on_toggle_inspector) on_toggle_inspector(show_inspector_);
        }
        if (MenuItem("📁 Asset Browser", nullptr, &show_asset_browser_)) {
            if (on_toggle_asset_browser) on_toggle_asset_browser(show_asset_browser_);
        }
        if (MenuItem("📋 Console", nullptr, &show_console_)) {
            if (on_toggle_console) on_toggle_console(show_console_);
        }

        EndMenu();
    }
}

void MainMenu::DrawHelpMenu() {
    if (BeginMenu("❓ Hilfe")) {
        if (MenuItem("📖 Dokumentation")) {
            // Open docs
        }
        if (MenuItem("🎓 Tutorial")) {
            // Open tutorial
        }

        Separator();

        if (MenuItem("ℹ️ Über DRX Engine")) {
            // Show about dialog
        }

        EndMenu();
    }
}

// ============================================================================
// Toolbar Implementation
// ============================================================================

void Toolbar::OnRender() {
    PushStyleVar(StyleVar_WindowPadding, Vec2(8, 4));

    BeginChild("Toolbar", {0, 40}, true);

    // Play/Pause/Stop Buttons
    if (!is_playing_) {
        if (Button("▶️ Play", {80, 28})) {
            is_playing_ = true;
            is_paused_ = false;
            if (on_play) on_play();
        }
    } else {
        if (is_paused_) {
            if (Button("▶️ Weiter", {80, 28})) {
                is_paused_ = false;
                if (on_play) on_play();
            }
        } else {
            if (Button("⏸️ Pause", {80, 28})) {
                is_paused_ = true;
                if (on_pause) on_pause();
            }
        }
    }

    SameLine();

    bool can_stop = is_playing_;
    if (!can_stop) BeginDisabled();
    if (Button("⏹️ Stop", {80, 28})) {
        is_playing_ = false;
        is_paused_ = false;
        if (on_stop) on_stop();
    }
    if (!can_stop) EndDisabled();

    SameLine();

    if (!is_playing_) BeginDisabled();
    if (Button("⏭️ Step", {80, 28})) {
        if (on_step) on_step();
    }
    if (!is_playing_) EndDisabled();

    // Status
    SameLine();
    Separator();
    SameLine();

    if (is_playing_) {
        if (is_paused_) {
            TextColored(Vec4(1.0f, 0.8f, 0.2f, 1.0f), "⏸️ PAUSIERT");
        } else {
            TextColored(Vec4(0.3f, 1.0f, 0.3f, 1.0f), "▶️ LÄUFT");
        }
    } else {
        TextColored(Vec4(0.6f, 0.6f, 0.6f, 1.0f), "⏹️ EDITOR MODUS");
    }

    EndChild();

    PopStyleVar();
}

// ============================================================================
// EditorApplication Implementation
// ============================================================================

EditorApplication& EditorApplication::Instance() {
    static EditorApplication instance;
    return instance;
}

bool EditorApplication::Initialize(const EditorConfig& config) {
    config_ = config;

    EDITOR_LOG("DRX Engine Editor wird gestartet...");

    // UI Panels erstellen
    hierarchy_panel_ = std::make_unique<HierarchyPanel>();
    inspector_panel_ = std::make_unique<InspectorPanel>();
    asset_browser_panel_ = std::make_unique<AssetBrowserPanel>();
    viewport_panel_ = std::make_unique<ViewportPanel>();
    console_panel_ = std::make_unique<ConsolePanel>();
    main_menu_ = std::make_unique<MainMenu>();
    toolbar_ = std::make_unique<Toolbar>();

    SetupCallbacks();

    // Neue Szene erstellen
    NewScene();

    // Asset Browser Pfad setzen
    asset_browser_panel_->SetRootPath("./Assets");

    EDITOR_LOG("Editor erfolgreich initialisiert!");

    running_ = true;
    return true;
}

void EditorApplication::Shutdown() {
    EDITOR_LOG("Editor wird beendet...");
    running_ = false;
}

void EditorApplication::Run() {
    float32 delta_time = 1.0f / 60.0f;

    while (running_) {
        Update(delta_time);
        Render();
    }
}

void EditorApplication::SetupCallbacks() {
    // Hierarchy Callbacks
    hierarchy_panel_->on_entity_selected = [this](Scene::EntityData* entity) {
        selected_entity_ = entity;
        inspector_panel_->SetSelectedEntity(entity);
    };

    hierarchy_panel_->on_entity_created = [this]() {
        CreateEntity();
    };

    hierarchy_panel_->on_entity_deleted = [this](uint64 id) {
        DeleteEntity(id);
    };

    // Menu Callbacks
    main_menu_->on_new_scene = [this]() { NewScene(); };
    main_menu_->on_save_scene = [this]() { SaveScene(); };
    main_menu_->on_exit = [this]() { running_ = false; };

    main_menu_->on_create_empty = [this]() { CreateEntity(); };
    main_menu_->on_create_cube = [this]() { CreateCube(); };
    main_menu_->on_create_sphere = [this]() { CreateSphere(); };
    main_menu_->on_create_plane = [this]() { CreatePlane(); };
    main_menu_->on_create_light = [this]() { CreateLight(); };
    main_menu_->on_create_camera = [this]() { CreateCamera(); };

    main_menu_->on_toggle_hierarchy = [this](bool v) { hierarchy_panel_->SetVisible(v); };
    main_menu_->on_toggle_inspector = [this](bool v) { inspector_panel_->SetVisible(v); };
    main_menu_->on_toggle_asset_browser = [this](bool v) { asset_browser_panel_->SetVisible(v); };
    main_menu_->on_toggle_console = [this](bool v) { console_panel_->SetVisible(v); };

    // Toolbar Callbacks
    toolbar_->on_play = [this]() { Play(); };
    toolbar_->on_pause = [this]() { Pause(); };
    toolbar_->on_stop = [this]() { Stop(); };
    toolbar_->on_step = [this]() { Step(); };
}

void EditorApplication::Update(float32 delta_time) {
    HandleShortcuts();

    viewport_panel_->OnUpdate(delta_time);

    hierarchy_panel_->SetScene(&current_scene_);
}

void EditorApplication::Render() {
    RenderDockspace();

    main_menu_->OnRender();
    toolbar_->OnRender();

    hierarchy_panel_->OnRender();
    inspector_panel_->OnRender();
    asset_browser_panel_->OnRender();
    viewport_panel_->OnRender();
    console_panel_->OnRender();

    DialogManager::Instance().OnRender();
}

void EditorApplication::RenderDockspace() {
    // Dockspace für Panel-Layout
    // ImGui Docking würde hier verwendet
}

void EditorApplication::HandleShortcuts() {
    // Keyboard shortcuts
    // Ctrl+N, Ctrl+O, Ctrl+S, etc.
}

// Entity Creation
Scene::EntityData* EditorApplication::CreateEntity(const String& name) {
    Scene::EntityData entity;
    entity.id = next_entity_id_++;
    entity.name = name;
    entity.enabled = true;
    entity.parent_id = 0;
    entity.transform.position = Math::Vector3::Zero;
    entity.transform.rotation = Math::Quaternion::Identity;
    entity.transform.scale = Math::Vector3::One;

    current_scene_.entities.push_back(entity);
    scene_modified_ = true;

    EDITOR_LOG("Objekt erstellt: " + name);

    return &current_scene_.entities.back();
}

Scene::EntityData* EditorApplication::CreateCube(const String& name) {
    auto entity = CreateEntity(name);
    Scene::ComponentData mesh_comp;
    mesh_comp.type = "MeshRenderer";
    mesh_comp.enabled = true;
    // mesh_comp.data["mesh"] = "Primitives/Cube";
    entity->components.push_back(mesh_comp);
    return entity;
}

Scene::EntityData* EditorApplication::CreateSphere(const String& name) {
    auto entity = CreateEntity(name);
    Scene::ComponentData mesh_comp;
    mesh_comp.type = "MeshRenderer";
    mesh_comp.enabled = true;
    entity->components.push_back(mesh_comp);
    return entity;
}

Scene::EntityData* EditorApplication::CreatePlane(const String& name) {
    auto entity = CreateEntity(name);
    Scene::ComponentData mesh_comp;
    mesh_comp.type = "MeshRenderer";
    mesh_comp.enabled = true;
    entity->components.push_back(mesh_comp);
    return entity;
}

Scene::EntityData* EditorApplication::CreateLight(const String& name) {
    auto entity = CreateEntity(name);
    Scene::ComponentData light_comp;
    light_comp.type = "Light";
    light_comp.enabled = true;
    entity->components.push_back(light_comp);
    return entity;
}

Scene::EntityData* EditorApplication::CreateCamera(const String& name) {
    auto entity = CreateEntity(name);
    Scene::ComponentData camera_comp;
    camera_comp.type = "Camera";
    camera_comp.enabled = true;
    entity->components.push_back(camera_comp);
    return entity;
}

void EditorApplication::DeleteEntity(uint64 entity_id) {
    auto it = std::find_if(current_scene_.entities.begin(), current_scene_.entities.end(),
        [entity_id](const Scene::EntityData& e) { return e.id == entity_id; });

    if (it != current_scene_.entities.end()) {
        EDITOR_LOG("Objekt gelöscht: " + it->name);
        current_scene_.entities.erase(it);

        if (selected_entity_ && selected_entity_->id == entity_id) {
            selected_entity_ = nullptr;
            inspector_panel_->SetSelectedEntity(nullptr);
        }

        scene_modified_ = true;
    }
}

void EditorApplication::DuplicateEntity(uint64 entity_id) {
    for (const auto& entity : current_scene_.entities) {
        if (entity.id == entity_id) {
            Scene::EntityData copy = entity;
            copy.id = next_entity_id_++;
            copy.name = entity.name + " (Copy)";
            current_scene_.entities.push_back(copy);
            scene_modified_ = true;
            EDITOR_LOG("Objekt dupliziert: " + copy.name);
            break;
        }
    }
}

// Scene Management
void EditorApplication::NewScene() {
    current_scene_ = Scene::SceneData{};
    current_scene_.name = "Unbenannte Szene";
    current_scene_.version = "1.0";
    current_scene_path_.clear();
    scene_modified_ = false;

    selected_entity_ = nullptr;
    inspector_panel_->SetSelectedEntity(nullptr);

    EDITOR_LOG("Neue Szene erstellt");
}

void EditorApplication::OpenScene(const String& path) {
    if (Scene::SceneSerializer::Instance().LoadScene(current_scene_, path)) {
        current_scene_path_ = path;
        scene_modified_ = false;
        EDITOR_LOG("Szene geladen: " + path);
    } else {
        EDITOR_ERROR("Fehler beim Laden der Szene: " + path);
    }
}

void EditorApplication::SaveScene() {
    if (current_scene_path_.empty()) {
        // Show save dialog
        return;
    }
    SaveSceneAs(current_scene_path_);
}

void EditorApplication::SaveSceneAs(const String& path) {
    if (Scene::SceneSerializer::Instance().SaveScene(current_scene_, path)) {
        current_scene_path_ = path;
        scene_modified_ = false;
        EDITOR_LOG("Szene gespeichert: " + path);
    } else {
        EDITOR_ERROR("Fehler beim Speichern der Szene: " + path);
    }
}

// Playmode
void EditorApplication::Play() {
    is_playing_ = true;
    is_paused_ = false;
    EDITOR_LOG("Spiel gestartet");
}

void EditorApplication::Pause() {
    is_paused_ = true;
    EDITOR_LOG("Spiel pausiert");
}

void EditorApplication::Stop() {
    is_playing_ = false;
    is_paused_ = false;
    EDITOR_LOG("Spiel gestoppt");
}

void EditorApplication::Step() {
    EDITOR_LOG("Ein Frame weiter");
}

void EditorApplication::Undo() {
    EDITOR_LOG("Rückgängig");
}

void EditorApplication::Redo() {
    EDITOR_LOG("Wiederholen");
}

// ============================================================================
// DialogManager Implementation
// ============================================================================

DialogManager& DialogManager::Instance() {
    static DialogManager instance;
    return instance;
}

void DialogManager::ShowMessageBox(const String& title, const String& message) {
    message_dialog_.open = true;
    message_dialog_.title = title;
    message_dialog_.message = message;
}

void DialogManager::ShowConfirmDialog(const String& title, const String& message,
                                       std::function<void(bool)> callback) {
    confirm_dialog_.open = true;
    confirm_dialog_.title = title;
    confirm_dialog_.message = message;
    confirm_dialog_.confirm_callback = callback;
}

void DialogManager::OnRender() {
    // Message Box
    if (message_dialog_.open) {
        if (BeginPopupModal(message_dialog_.title.c_str())) {
            Text("%s", message_dialog_.message.c_str());
            if (Button("OK")) {
                message_dialog_.open = false;
                CloseCurrentPopup();
            }
            EndPopup();
        }
    }

    // Confirm Dialog
    if (confirm_dialog_.open) {
        if (BeginPopupModal(confirm_dialog_.title.c_str())) {
            Text("%s", confirm_dialog_.message.c_str());
            if (Button("Ja")) {
                if (confirm_dialog_.confirm_callback) {
                    confirm_dialog_.confirm_callback(true);
                }
                confirm_dialog_.open = false;
                CloseCurrentPopup();
            }
            SameLine();
            if (Button("Nein")) {
                if (confirm_dialog_.confirm_callback) {
                    confirm_dialog_.confirm_callback(false);
                }
                confirm_dialog_.open = false;
                CloseCurrentPopup();
            }
            EndPopup();
        }
    }
}

} // namespace DRX::Editor

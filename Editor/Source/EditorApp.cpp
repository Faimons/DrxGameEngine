/**
 * DRX Engine - Visual Editor Implementation
 * Complete working editor with ImGui, GLFW, and OpenGL3
 */

#include "EditorApp.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstring>

#if __has_include(<filesystem>)
    #include <filesystem>
    namespace fs = std::filesystem;
#else
    #include <experimental/filesystem>
    namespace fs = std::experimental::filesystem;
#endif

namespace DRX::Editor {

// ============================================================================
// EditorApplication Implementation
// ============================================================================

EditorApplication& EditorApplication::Instance() {
    static EditorApplication instance;
    return instance;
}

bool EditorApplication::Initialize(const EditorConfig& config) {
    config_ = config;

    if (!InitWindow()) {
        std::cerr << "Failed to initialize window\n";
        return false;
    }

    if (!InitImGui()) {
        std::cerr << "Failed to initialize ImGui\n";
        return false;
    }

    // Create default scene
    NewScene();

    // Create Assets directory if it doesn't exist
    if (!fs::exists(asset_root_path_)) {
        fs::create_directories(asset_root_path_);
    }
    ScanDirectory(asset_current_path_);

    Log("DRX Engine Editor initialized");
    Log("Welcome to DRX Engine!");

    running_ = true;
    return true;
}

bool EditorApplication::InitWindow() {
    if (!glfwInit()) {
        return false;
    }

    // OpenGL 3.3 Core
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    window_ = glfwCreateWindow(
        config_.window_width,
        config_.window_height,
        config_.window_title.c_str(),
        nullptr, nullptr
    );

    if (!window_) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(config_.vsync ? 1 : 0);

    return true;
}

bool EditorApplication::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // Dark theme
    if (config_.dark_mode) {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 5.0f;
        style.FrameRounding = 3.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 3.0f;

        // Custom colors
        auto& colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.13f, 1.0f);
        colors[ImGuiCol_Header] = ImVec4(0.2f, 0.2f, 0.25f, 1.0f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.3f, 0.3f, 0.35f, 1.0f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.15f, 0.15f, 0.2f, 1.0f);
        colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.25f, 1.0f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.3f, 0.35f, 1.0f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.15f, 0.2f, 1.0f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.2f, 0.2f, 0.25f, 1.0f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.3f, 1.0f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.15f, 0.15f, 0.2f, 1.0f);
        colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.2f, 1.0f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.38f, 0.38f, 0.45f, 1.0f);
        colors[ImGuiCol_TabActive] = ImVec4(0.28f, 0.28f, 0.35f, 1.0f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.1f, 0.1f, 0.13f, 1.0f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.2f, 1.0f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.18f, 1.0f);
    }

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    return true;
}

void EditorApplication::Shutdown() {
    Log("Shutting down editor...");
    running_ = false;
    ShutdownImGui();
    ShutdownWindow();
}

void EditorApplication::ShutdownImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void EditorApplication::ShutdownWindow() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

void EditorApplication::Run() {
    while (running_ && !glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        Update();
        Render();
    }
}

void EditorApplication::Update() {
    HandleShortcuts();
}

void EditorApplication::Render() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    RenderUI();

    ImGui::Render();

    int display_w, display_h;
    glfwGetFramebufferSize(window_, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.1f, 0.1f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Update viewports for multi-window support
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup);
    }

    glfwSwapBuffers(window_);
}

void EditorApplication::RenderUI() {
    // Enable dockspace over entire window
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpace", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");

    // Build the default layout once when no saved layout exists
    if (!dock_layout_checked_) {
        dock_layout_checked_ = true;
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace_id);
        if (node == nullptr || !node->IsSplitNode()) {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

            const ImVec2 size = viewport->WorkSize;
            ImGuiID center = dockspace_id;
            ImGuiID toolbar = ImGui::DockBuilderSplitNode(center, ImGuiDir_Up, 0.06f, nullptr, &center);
            ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
            ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
            ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);

            // Explicit sizes keep the side panels usable regardless of split order
            ImGui::DockBuilderSetNodeSize(toolbar, ImVec2(size.x, 48.0f));
            ImGui::DockBuilderSetNodeSize(left, ImVec2(size.x * 0.18f, size.y));
            ImGui::DockBuilderSetNodeSize(right, ImVec2(size.x * 0.22f, size.y));
            ImGui::DockBuilderSetNodeSize(bottom, ImVec2(size.x * 0.6f, size.y * 0.28f));

            if (ImGuiDockNode* toolbar_node = ImGui::DockBuilderGetNode(toolbar)) {
                toolbar_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoResize;
            }

            ImGui::DockBuilderDockWindow("##Toolbar", toolbar);
            ImGui::DockBuilderDockWindow("Hierarchy", left);
            ImGui::DockBuilderDockWindow("Inspector", right);
            ImGui::DockBuilderDockWindow("Assets", bottom);
            ImGui::DockBuilderDockWindow("Console", bottom);
            ImGui::DockBuilderDockWindow("Viewport", center);
            ImGui::DockBuilderFinish(dockspace_id);
        }
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    // Render menu bar and panels
    RenderMainMenu();
    RenderToolbar();

    if (show_hierarchy_) RenderHierarchy();
    if (show_inspector_) RenderInspector();
    if (show_asset_browser_) RenderAssetBrowser();
    if (show_viewport_) RenderViewport();
    if (show_console_) RenderConsole();

    if (show_demo_window_) ImGui::ShowDemoWindow(&show_demo_window_);

    ImGui::End();
}

void EditorApplication::RenderMainMenu() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) NewScene();
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                // TODO: File dialog
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) SaveScene();
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
                // TODO: File dialog
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) running_ = false;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {}
            if (ImGui::MenuItem("Paste", "Ctrl+V")) {}
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                if (selected_entity_) DuplicateEntity(selected_entity_->id);
            }
            if (ImGui::MenuItem("Delete", "Delete")) {
                if (selected_entity_) DeleteEntity(selected_entity_->id);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("GameObject")) {
            if (ImGui::MenuItem("Create Empty")) CreateEntity();
            ImGui::Separator();
            if (ImGui::BeginMenu("3D Objects")) {
                if (ImGui::MenuItem("Cube")) CreateCube();
                if (ImGui::MenuItem("Sphere")) CreateSphere();
                if (ImGui::MenuItem("Plane")) CreatePlane();
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Light")) CreateLight();
            if (ImGui::MenuItem("Camera")) CreateCamera();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("Hierarchy", nullptr, &show_hierarchy_);
            ImGui::MenuItem("Inspector", nullptr, &show_inspector_);
            ImGui::MenuItem("Asset Browser", nullptr, &show_asset_browser_);
            ImGui::MenuItem("Console", nullptr, &show_console_);
            ImGui::MenuItem("Viewport", nullptr, &show_viewport_);
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &show_demo_window_);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Documentation")) {}
            if (ImGui::MenuItem("About DRX Engine")) {
                Log("DRX Engine v1.0.0 - Autonomous Game Engine");
            }
            ImGui::EndMenu();
        }

        // Status on the right
        float status_width = 200.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - status_width);
        if (is_playing_) {
            if (is_paused_) {
                ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "PAUSED");
            } else {
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "PLAYING");
            }
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "EDITOR");
        }

        ImGui::EndMainMenuBar();
    }
}

void EditorApplication::RenderToolbar() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("##Toolbar", nullptr, flags)) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 8));

        // Play/Pause/Stop buttons
        if (!is_playing_) {
            if (ImGui::Button("Play", ImVec2(80, 30))) Play();
        } else {
            if (is_paused_) {
                if (ImGui::Button("Resume", ImVec2(80, 30))) {
                    is_paused_ = false;
                    Log("Game resumed");
                }
            } else {
                if (ImGui::Button("Pause", ImVec2(80, 30))) Pause();
            }
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(!is_playing_);
        if (ImGui::Button("Stop", ImVec2(80, 30))) Stop();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Gizmo mode buttons
        bool translate = (gizmo_mode_ == GizmoMode::Translate);
        bool rotate = (gizmo_mode_ == GizmoMode::Rotate);
        bool scale = (gizmo_mode_ == GizmoMode::Scale);

        if (ImGui::RadioButton("Move (W)", translate)) gizmo_mode_ = GizmoMode::Translate;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate (E)", rotate)) gizmo_mode_ = GizmoMode::Rotate;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale (R)", scale)) gizmo_mode_ = GizmoMode::Scale;

        ImGui::PopStyleVar();
    }
    ImGui::End();
}

void EditorApplication::RenderHierarchy() {
    if (ImGui::Begin("Hierarchy", &show_hierarchy_)) {
        // Search
        static char search[128] = "";
        ImGui::InputTextWithHint("##Search", "Search...", search, sizeof(search));

        ImGui::Separator();

        // Add entity button
        if (ImGui::Button("+ Add Entity", ImVec2(-1, 30))) {
            CreateEntity();
        }

        ImGui::Separator();

        // Entity list
        for (auto& entity : current_scene_.entities) {
            // Filter by search
            if (strlen(search) > 0 && entity.name.find(search) == String::npos) {
                continue;
            }

            // Only show root entities
            if (entity.parent_id == 0) {
                RenderEntityNode(entity);
            }
        }

        // Right-click context menu
        if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Create Empty")) CreateEntity();
            ImGui::Separator();
            if (ImGui::MenuItem("Create Cube")) CreateCube();
            if (ImGui::MenuItem("Create Sphere")) CreateSphere();
            if (ImGui::MenuItem("Create Plane")) CreatePlane();
            ImGui::Separator();
            if (ImGui::MenuItem("Create Light")) CreateLight();
            if (ImGui::MenuItem("Create Camera")) CreateCamera();
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

void EditorApplication::RenderEntityNode(Scene::EntityData& entity) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth;

    if (selected_entity_ && selected_entity_->id == entity.id) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // Check for children
    bool has_children = false;
    for (const auto& e : current_scene_.entities) {
        if (e.parent_id == entity.id) {
            has_children = true;
            break;
        }
    }

    if (!has_children) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    // Icon based on components
    const char* icon = "O";
    for (const auto& comp : entity.components) {
        if (comp.type == "Camera") { icon = "[C]"; break; }
        if (comp.type == "Light") { icon = "[L]"; break; }
        if (comp.type == "MeshRenderer") { icon = "[M]"; break; }
    }

    String label = String(icon) + " " + entity.name;
    bool is_open = ImGui::TreeNodeEx((void*)(intptr_t)entity.id, flags, "%s", label.c_str());

    // Selection
    if (ImGui::IsItemClicked()) {
        selected_entity_ = &entity;
    }

    // Drag & drop
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("ENTITY", &entity.id, sizeof(uint64));
        ImGui::Text("Moving: %s", entity.name.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY")) {
            uint64 dropped_id = *(uint64*)payload->Data;
            for (auto& e : current_scene_.entities) {
                if (e.id == dropped_id) {
                    e.parent_id = entity.id;
                    break;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Context menu
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Duplicate")) DuplicateEntity(entity.id);
        if (ImGui::MenuItem("Delete")) DeleteEntity(entity.id);
        ImGui::EndPopup();
    }

    // Draw children
    if (is_open) {
        for (auto& child : current_scene_.entities) {
            if (child.parent_id == entity.id) {
                RenderEntityNode(child);
            }
        }
        ImGui::TreePop();
    }
}

void EditorApplication::RenderInspector() {
    if (ImGui::Begin("Inspector", &show_inspector_)) {
        if (selected_entity_) {
            // Entity name
            char name_buf[128];
            strncpy(name_buf, selected_entity_->name.c_str(), sizeof(name_buf) - 1);
            name_buf[sizeof(name_buf) - 1] = '\0';

            ImGui::Text("Name:");
            ImGui::SameLine();
            if (ImGui::InputText("##Name", name_buf, sizeof(name_buf))) {
                selected_entity_->name = name_buf;
                scene_modified_ = true;
            }

            ImGui::Checkbox("Active", &selected_entity_->enabled);

            ImGui::Separator();

            // Transform
            RenderTransformComponent();

            ImGui::Separator();

            // Components
            RenderComponents();

            ImGui::Separator();

            // Add component button
            if (ImGui::Button("+ Add Component", ImVec2(-1, 30))) {
                ImGui::OpenPopup("AddComponentPopup");
            }
            RenderAddComponentPopup();

        } else {
            ImGui::TextDisabled("No entity selected");
            ImGui::Spacing();
            ImGui::Text("Select an entity in the");
            ImGui::Text("Hierarchy to view its properties.");
        }
    }
    ImGui::End();
}

void EditorApplication::RenderTransformComponent() {
    if (!selected_entity_) return;

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& t = selected_entity_->transform;

        // Position
        float pos[3] = {t.position.x, t.position.y, t.position.z};
        ImGui::Text("Position");
        if (ImGui::DragFloat3("##Pos", pos, 0.1f)) {
            t.position = {pos[0], pos[1], pos[2]};
            scene_modified_ = true;
        }

        // Rotation
        // Shown in degrees as (pitch, yaw, roll)
        constexpr float kRadToDeg = 57.29577951308232f;
        constexpr float kDegToRad = 0.017453292519943295f;
        Math::Vector3 euler = t.rotation.ToEuler();
        float rot[3] = {euler.x * kRadToDeg, euler.y * kRadToDeg, euler.z * kRadToDeg};
        ImGui::Text("Rotation");
        if (ImGui::DragFloat3("##Rot", rot, 1.0f)) {
            t.rotation = Math::Quaternion::FromEuler(rot[0] * kDegToRad, rot[1] * kDegToRad, rot[2] * kDegToRad);
            scene_modified_ = true;
        }

        // Scale
        float scale[3] = {t.scale.x, t.scale.y, t.scale.z};
        ImGui::Text("Scale");
        if (ImGui::DragFloat3("##Scale", scale, 0.1f)) {
            t.scale = {scale[0], scale[1], scale[2]};
            scene_modified_ = true;
        }

        if (ImGui::Button("Reset Transform")) {
            t.position = Math::Vector3::Zero;
            t.rotation = Math::Quaternion::Identity();
            t.scale = Math::Vector3::One;
            scene_modified_ = true;
        }
    }
}

void EditorApplication::RenderComponents() {
    if (!selected_entity_) return;

    for (size_t i = 0; i < selected_entity_->components.size(); ++i) {
        auto& comp = selected_entity_->components[i];

        ImGui::PushID((int)i);

        String header = String(GetComponentIcon(comp.type)) + " " + comp.type;
        bool is_open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        // Right-click to remove
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Component")) {
                selected_entity_->components.erase(selected_entity_->components.begin() + i);
                scene_modified_ = true;
                ImGui::EndPopup();
                ImGui::PopID();
                return;
            }
            ImGui::EndPopup();
        }

        if (is_open) {
            ImGui::Checkbox("Enabled", &comp.enabled);

            // Component-specific UI
            if (comp.type == "Camera") {
                static float fov = 60.0f;
                static float near_plane = 0.1f;
                static float far_plane = 1000.0f;
                ImGui::SliderFloat("FOV", &fov, 30.0f, 120.0f);
                ImGui::DragFloat("Near", &near_plane, 0.01f, 0.01f, 10.0f);
                ImGui::DragFloat("Far", &far_plane, 1.0f, 10.0f, 10000.0f);
            }
            else if (comp.type == "Light") {
                static int light_type = 0;
                static float color[3] = {1, 1, 1};
                static float intensity = 1.0f;
                const char* types[] = {"Directional", "Point", "Spot"};
                ImGui::Combo("Type", &light_type, types, 3);
                ImGui::ColorEdit3("Color", color);
                ImGui::SliderFloat("Intensity", &intensity, 0.0f, 10.0f);
            }
            else if (comp.type == "MeshRenderer") {
                ImGui::Text("Mesh: Cube");
                if (ImGui::Button("Select Mesh...")) {}
                ImGui::Text("Material: Default");
                if (ImGui::Button("Select Material...")) {}
            }
            else if (comp.type == "RigidBody") {
                static float mass = 1.0f;
                static bool use_gravity = true;
                ImGui::DragFloat("Mass", &mass, 0.1f, 0.0f, 1000.0f);
                ImGui::Checkbox("Use Gravity", &use_gravity);
            }
            else if (comp.type == "AudioSource") {
                static float volume = 1.0f;
                static bool loop = false;
                ImGui::Text("Audio Clip: None");
                if (ImGui::Button("Select Audio...")) {}
                ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f);
                ImGui::Checkbox("Loop", &loop);
            }
        }

        ImGui::PopID();
    }
}

void EditorApplication::RenderAddComponentPopup() {
    if (ImGui::BeginPopup("AddComponentPopup")) {
        ImGui::Text("Add Component:");
        ImGui::Separator();

        if (ImGui::MenuItem("[M] Mesh Renderer")) {
            Scene::ComponentData comp;
            comp.type = "MeshRenderer";
            selected_entity_->components.push_back(comp);
            scene_modified_ = true;
        }
        if (ImGui::MenuItem("[C] Camera")) {
            Scene::ComponentData comp;
            comp.type = "Camera";
            selected_entity_->components.push_back(comp);
            scene_modified_ = true;
        }
        if (ImGui::MenuItem("[L] Light")) {
            Scene::ComponentData comp;
            comp.type = "Light";
            selected_entity_->components.push_back(comp);
            scene_modified_ = true;
        }
        if (ImGui::MenuItem("[P] RigidBody")) {
            Scene::ComponentData comp;
            comp.type = "RigidBody";
            selected_entity_->components.push_back(comp);
            scene_modified_ = true;
        }
        if (ImGui::MenuItem("[A] Audio Source")) {
            Scene::ComponentData comp;
            comp.type = "AudioSource";
            selected_entity_->components.push_back(comp);
            scene_modified_ = true;
        }
        if (ImGui::MenuItem("[S] Script")) {
            Scene::ComponentData comp;
            comp.type = "Script";
            selected_entity_->components.push_back(comp);
            scene_modified_ = true;
        }

        ImGui::EndPopup();
    }
}

void EditorApplication::RenderAssetBrowser() {
    if (ImGui::Begin("Assets", &show_asset_browser_)) {
        // Path bar
        ImGui::Text("Path: %s", asset_current_path_.c_str());

        ImGui::SameLine();
        if (ImGui::Button("Up")) {
            if (asset_current_path_ != asset_root_path_) {
                asset_current_path_ = fs::path(asset_current_path_).parent_path().string();
                ScanDirectory(asset_current_path_);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            ScanDirectory(asset_current_path_);
        }

        ImGui::Separator();

        // Icon size slider
        ImGui::SliderFloat("Icon Size", &icon_size_, 40.0f, 150.0f);

        ImGui::Separator();

        // Asset grid
        float panel_width = ImGui::GetContentRegionAvail().x;
        int columns = std::max(1, (int)(panel_width / (icon_size_ + 20)));

        if (ImGui::BeginChild("AssetGrid")) {
            int col = 0;
            for (size_t i = 0; i < asset_items_.size(); ++i) {
                const auto& item = asset_items_[i];

                ImGui::PushID((int)i);

                ImGui::BeginGroup();

                // Selection highlight
                bool is_selected = ((int)i == selected_asset_);
                if (is_selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
                }

                // Icon button
                if (ImGui::Button(GetAssetIcon(item.type), ImVec2(icon_size_, icon_size_))) {
                    selected_asset_ = (int)i;
                }

                if (is_selected) {
                    ImGui::PopStyleColor();
                }

                // Double-click to open folder
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    if (item.is_directory) {
                        asset_current_path_ = item.path;
                        ScanDirectory(asset_current_path_);
                    }
                }

                // Drag source
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("ASSET", item.path.c_str(), item.path.size() + 1);
                    ImGui::Text("%s %s", GetAssetIcon(item.type), item.name.c_str());
                    ImGui::EndDragDropSource();
                }

                // Name (truncated)
                String display = item.name;
                if (display.length() > 12) {
                    display = display.substr(0, 10) + "...";
                }
                ImGui::TextWrapped("%s", display.c_str());

                ImGui::EndGroup();

                ImGui::PopID();

                col++;
                if (col < columns) {
                    ImGui::SameLine();
                } else {
                    col = 0;
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void EditorApplication::RenderViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::Begin("Viewport", &show_viewport_)) {
        // Toolbar
        ImGui::PopStyleVar();

        if (ImGui::RadioButton("Translate (W)", gizmo_mode_ == GizmoMode::Translate))
            gizmo_mode_ = GizmoMode::Translate;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate (E)", gizmo_mode_ == GizmoMode::Rotate))
            gizmo_mode_ = GizmoMode::Rotate;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale (R)", gizmo_mode_ == GizmoMode::Scale))
            gizmo_mode_ = GizmoMode::Scale;

        ImGui::SameLine();
        ImGui::Text("  Camera: %.1f, %.1f, %.1f", camera_pos_[0], camera_pos_[1], camera_pos_[2]);

        ImGui::Separator();

        // Viewport area
        ImVec2 size = ImGui::GetContentRegionAvail();

        // Draw a placeholder viewport (dark area with grid info)
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        // Background
        draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                                  IM_COL32(30, 30, 40, 255));

        // Grid lines
        float grid_step = 50.0f;
        ImU32 grid_color = IM_COL32(50, 50, 60, 255);
        for (float x = pos.x; x < pos.x + size.x; x += grid_step) {
            draw_list->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + size.y), grid_color);
        }
        for (float y = pos.y; y < pos.y + size.y; y += grid_step) {
            draw_list->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + size.x, y), grid_color);
        }

        // Center info
        String info = "3D Viewport\nWASD to move camera\nRight-click to look around";
        ImVec2 text_size = ImGui::CalcTextSize(info.c_str());
        draw_list->AddText(
            ImVec2(pos.x + (size.x - text_size.x) / 2, pos.y + (size.y - text_size.y) / 2),
            IM_COL32(100, 100, 100, 255), info.c_str()
        );

        // Draw entities as boxes
        for (const auto& entity : current_scene_.entities) {
            float scale = 20.0f;
            float cx = pos.x + size.x / 2 + entity.transform.position.x * scale;
            float cy = pos.y + size.y / 2 - entity.transform.position.z * scale;

            ImU32 color = IM_COL32(100, 150, 200, 255);
            if (selected_entity_ && selected_entity_->id == entity.id) {
                color = IM_COL32(255, 200, 100, 255);
            }

            draw_list->AddRectFilled(
                ImVec2(cx - 10, cy - 10),
                ImVec2(cx + 10, cy + 10),
                color
            );
            draw_list->AddText(ImVec2(cx - 10, cy + 12), IM_COL32(200, 200, 200, 255),
                               entity.name.substr(0, 8).c_str());
        }

        // Handle viewport interaction
        viewport_focused_ = ImGui::IsWindowFocused();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    } else {
        ImGui::PopStyleVar();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorApplication::RenderConsole() {
    if (ImGui::Begin("Console", &show_console_)) {
        // Toolbar
        if (ImGui::Button("Clear")) {
            console_messages_.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Info", &show_info_);
        ImGui::SameLine();
        ImGui::Checkbox("Warnings", &show_warnings_);
        ImGui::SameLine();
        ImGui::Checkbox("Errors", &show_errors_);
        ImGui::SameLine();
        ImGui::Text("| %zu messages", console_messages_.size());

        ImGui::Separator();

        // Messages
        if (ImGui::BeginChild("Messages", ImVec2(0, -30), true)) {
            for (const auto& msg : console_messages_) {
                if (msg.level == ConsoleMessage::Level::Info && !show_info_) continue;
                if (msg.level == ConsoleMessage::Level::Warning && !show_warnings_) continue;
                if (msg.level == ConsoleMessage::Level::Error && !show_errors_) continue;

                ImVec4 color;
                const char* prefix;
                switch (msg.level) {
                    case ConsoleMessage::Level::Info:
                        color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                        prefix = "[INFO]";
                        break;
                    case ConsoleMessage::Level::Warning:
                        color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                        prefix = "[WARN]";
                        break;
                    case ConsoleMessage::Level::Error:
                        color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                        prefix = "[ERROR]";
                        break;
                    default:
                        color = ImVec4(0.5f, 0.5f, 1.0f, 1.0f);
                        prefix = "[DEBUG]";
                        break;
                }

                ImGui::TextColored(color, "[%s] %s %s", msg.timestamp.c_str(), prefix, msg.message.c_str());

                if (msg.count > 1) {
                    ImGui::SameLine();
                    ImGui::Text("(x%d)", msg.count);
                }
            }

            if (console_scroll_to_bottom_) {
                ImGui::SetScrollHereY(1.0f);
                console_scroll_to_bottom_ = false;
            }
        }
        ImGui::EndChild();

        ImGui::Separator();

        // Input
        ImGui::PushItemWidth(-60);
        if (ImGui::InputText("##Input", console_input_, sizeof(console_input_),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (strlen(console_input_) > 0) {
                Log(String("> ") + console_input_);

                // Simple commands
                String cmd = console_input_;
                if (cmd == "help") {
                    Log("Commands: help, clear, version, entities");
                } else if (cmd == "clear") {
                    console_messages_.clear();
                } else if (cmd == "version") {
                    Log("DRX Engine v1.0.0");
                } else if (cmd == "entities") {
                    Log("Entities in scene: " + std::to_string(current_scene_.entities.size()));
                } else {
                    LogWarning("Unknown command: " + cmd);
                }

                console_input_[0] = '\0';
            }
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Send")) {
            // Trigger input handling
        }
    }
    ImGui::End();
}

void EditorApplication::HandleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();

    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_N)) NewScene();
        if (ImGui::IsKeyPressed(ImGuiKey_S)) SaveScene();
        if (ImGui::IsKeyPressed(ImGuiKey_D) && selected_entity_) {
            DuplicateEntity(selected_entity_->id);
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && selected_entity_) {
        DeleteEntity(selected_entity_->id);
    }

    // Gizmo shortcuts
    if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) gizmo_mode_ = GizmoMode::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) gizmo_mode_ = GizmoMode::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) gizmo_mode_ = GizmoMode::Scale;
    }
}

// ============================================================================
// Entity Management
// ============================================================================

Scene::EntityData* EditorApplication::CreateEntity(const String& name) {
    Scene::EntityData entity;
    entity.id = next_entity_id_++;
    entity.name = name;
    entity.enabled = true;
    entity.transform.position = Math::Vector3::Zero;
    entity.transform.rotation = Math::Quaternion::Identity();
    entity.transform.scale = Math::Vector3::One;

    current_scene_.entities.push_back(entity);
    scene_modified_ = true;

    Log("Created entity: " + name);

    return &current_scene_.entities.back();
}

Scene::EntityData* EditorApplication::CreateCube(const String& name) {
    auto* entity = CreateEntity(name);
    Scene::ComponentData comp;
    comp.type = "MeshRenderer";
    entity->components.push_back(comp);
    return entity;
}

Scene::EntityData* EditorApplication::CreateSphere(const String& name) {
    auto* entity = CreateEntity(name);
    Scene::ComponentData comp;
    comp.type = "MeshRenderer";
    entity->components.push_back(comp);
    return entity;
}

Scene::EntityData* EditorApplication::CreatePlane(const String& name) {
    auto* entity = CreateEntity(name);
    Scene::ComponentData comp;
    comp.type = "MeshRenderer";
    entity->components.push_back(comp);
    return entity;
}

Scene::EntityData* EditorApplication::CreateLight(const String& name) {
    auto* entity = CreateEntity(name);
    Scene::ComponentData comp;
    comp.type = "Light";
    entity->components.push_back(comp);
    return entity;
}

Scene::EntityData* EditorApplication::CreateCamera(const String& name) {
    auto* entity = CreateEntity(name);
    Scene::ComponentData comp;
    comp.type = "Camera";
    entity->components.push_back(comp);
    return entity;
}

void EditorApplication::DeleteEntity(uint64 entity_id) {
    auto it = std::find_if(current_scene_.entities.begin(), current_scene_.entities.end(),
        [entity_id](const Scene::EntityData& e) { return e.id == entity_id; });

    if (it != current_scene_.entities.end()) {
        Log("Deleted entity: " + it->name);
        current_scene_.entities.erase(it);

        if (selected_entity_ && selected_entity_->id == entity_id) {
            selected_entity_ = nullptr;
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
            Log("Duplicated entity: " + copy.name);
            return;
        }
    }
}

// ============================================================================
// Scene Management
// ============================================================================

void EditorApplication::NewScene() {
    current_scene_ = Scene::SceneData{};
    current_scene_.name = "Untitled Scene";
    current_scene_path_.clear();
    selected_entity_ = nullptr;
    scene_modified_ = false;
    next_entity_id_ = 1;

    Log("New scene created");
}

void EditorApplication::OpenScene(const String& path) {
    Log("Opening scene: " + path);
    // TODO: Implement scene loading
}

void EditorApplication::SaveScene() {
    if (current_scene_path_.empty()) {
        Log("No path set - use Save As");
        return;
    }
    SaveSceneAs(current_scene_path_);
}

void EditorApplication::SaveSceneAs(const String& path) {
    Log("Saving scene to: " + path);
    current_scene_path_ = path;
    scene_modified_ = false;
    // TODO: Implement scene saving
}

// ============================================================================
// Play Mode
// ============================================================================

void EditorApplication::Play() {
    is_playing_ = true;
    is_paused_ = false;
    Log("Play mode started");
}

void EditorApplication::Pause() {
    is_paused_ = true;
    Log("Play mode paused");
}

void EditorApplication::Stop() {
    is_playing_ = false;
    is_paused_ = false;
    Log("Play mode stopped");
}

// ============================================================================
// Logging
// ============================================================================

void EditorApplication::Log(const String& message) {
    AddLog(ConsoleMessage::Level::Info, message);
}

void EditorApplication::LogWarning(const String& message) {
    AddLog(ConsoleMessage::Level::Warning, message);
}

void EditorApplication::LogError(const String& message) {
    AddLog(ConsoleMessage::Level::Error, message);
}

void EditorApplication::AddLog(ConsoleMessage::Level level, const String& message) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", localtime(&time));

    // Collapse repeated messages
    if (!console_messages_.empty() &&
        console_messages_.back().level == level &&
        console_messages_.back().message == message) {
        console_messages_.back().count++;
    } else {
        console_messages_.push_back({level, message, timestamp, 1});
    }

    console_scroll_to_bottom_ = true;

    // Also print to stdout
    std::cout << "[" << timestamp << "] " << message << std::endl;
}

// ============================================================================
// Helpers
// ============================================================================

const char* EditorApplication::GetComponentIcon(const String& type) {
    if (type == "MeshRenderer") return "[M]";
    if (type == "Camera") return "[C]";
    if (type == "Light") return "[L]";
    if (type == "RigidBody") return "[P]";
    if (type == "AudioSource") return "[A]";
    if (type == "Script") return "[S]";
    return "[?]";
}

void EditorApplication::ScanDirectory(const String& path) {
    asset_items_.clear();
    selected_asset_ = -1;

    if (!fs::exists(path)) {
        return;
    }

    for (const auto& entry : fs::directory_iterator(path)) {
        AssetItem item;
        item.path = entry.path().string();
        item.name = entry.path().filename().string();
        item.is_directory = entry.is_directory();

        if (item.is_directory) {
            item.type = AssetItem::Type::Folder;
        } else {
            item.extension = entry.path().extension().string();
            item.type = GetAssetType(item.extension);
            try {
                item.size = fs::file_size(entry.path());
            } catch (...) {
                item.size = 0;
            }
        }

        asset_items_.push_back(item);
    }

    // Sort: folders first, then alphabetically
    std::sort(asset_items_.begin(), asset_items_.end(),
        [](const AssetItem& a, const AssetItem& b) {
            if (a.is_directory != b.is_directory) return a.is_directory;
            return a.name < b.name;
        });
}

AssetItem::Type EditorApplication::GetAssetType(const String& ext) {
    String lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == ".png" || lower == ".jpg" || lower == ".jpeg" ||
        lower == ".tga" || lower == ".bmp") return AssetItem::Type::Texture;
    if (lower == ".obj" || lower == ".fbx" || lower == ".gltf" ||
        lower == ".glb") return AssetItem::Type::Model;
    if (lower == ".wav" || lower == ".mp3" || lower == ".ogg")
        return AssetItem::Type::Audio;
    if (lower == ".lua" || lower == ".py" || lower == ".cpp")
        return AssetItem::Type::Script;
    if (lower == ".drxscene") return AssetItem::Type::Scene;
    if (lower == ".drxprefab") return AssetItem::Type::Prefab;
    if (lower == ".vert" || lower == ".frag" || lower == ".glsl")
        return AssetItem::Type::Shader;
    if (lower == ".drxmat") return AssetItem::Type::Material;

    return AssetItem::Type::Unknown;
}

const char* EditorApplication::GetAssetIcon(AssetItem::Type type) {
    switch (type) {
        case AssetItem::Type::Folder: return "[DIR]";
        case AssetItem::Type::Texture: return "[TEX]";
        case AssetItem::Type::Model: return "[MDL]";
        case AssetItem::Type::Audio: return "[SND]";
        case AssetItem::Type::Script: return "[SCR]";
        case AssetItem::Type::Scene: return "[SCN]";
        case AssetItem::Type::Prefab: return "[PRE]";
        case AssetItem::Type::Shader: return "[SHD]";
        case AssetItem::Type::Material: return "[MAT]";
        default: return "[???]";
    }
}

} // namespace DRX::Editor

/**
 * SHIFT - Map editor
 */

#include "ShiftEditor.h"

#include "../Core/ShiftBot.h"
#include "ShiftColors.h"

#include "Core/Public/Log.h"
#include "Debug/Public/DebugSystem.h"
#include "Renderer/Public/MeshBuilder.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <filesystem>
#include <limits>

namespace Shift {

using Math::Matrix4;
using Math::Quaternion;
using Math::Vector2;
using Math::Vector3;
using Math::Vector4;

namespace {

constexpr float32 kPi = 3.14159265358979323846f;
constexpr float32 kDegToRad = kPi / 180.0f;
constexpr float32 kLeftWidth = 290.0f;
constexpr float32 kRightWidth = 300.0f;
constexpr float32 kRowHeight = 24.0f;
constexpr float32 kFont = 15.0f;
constexpr float32 kBottomBar = 46.0f;
constexpr size_t kMaxUndo = 64;
constexpr float32 kWallHeight = 4.0f;
constexpr float32 kExitHeight = 0.3f;

const Vector4 kPanelColor = {0.07f, 0.08f, 0.1f, 0.92f};
const Vector4 kButtonColor = {0.18f, 0.2f, 0.25f, 1.0f};
const Vector4 kButtonHover = {0.27f, 0.31f, 0.4f, 1.0f};
const Vector4 kButtonHighlight = {0.2f, 0.42f, 0.75f, 1.0f};
const Vector4 kFieldColor = {0.12f, 0.13f, 0.16f, 1.0f};
const Vector4 kText = {0.95f, 0.96f, 1.0f, 1.0f};
const Vector4 kHeader = {1.0f, 0.82f, 0.35f, 1.0f};
const Vector4 kSelect = {1.0f, 0.85f, 0.2f, 1.0f};

const Array<String> kSurfaceNames = {"Boden", "Wand", "Decke", "Deckung", "Stufe", "Startzone Blau", "Startzone Rot", "Beweglich"};
const Array<String> kGridNames = {"0,25 m", "0,5 m", "1 m", "2 m"};
const float32 kGridSizes[] = {0.25f, 0.5f, 1.0f, 2.0f};

float32 Snap(float32 value, float32 grid) { return std::round(value / grid) * grid; }

bool RayBox(const Vector3& origin, const Vector3& direction, const Vector3& min, const Vector3& max, float32& t) {
    float32 t0 = 0.0f;
    float32 t1 = std::numeric_limits<float32>::max();
    for (int32 axis = 0; axis < 3; ++axis) {
        const float32 o = axis == 0 ? origin.x : axis == 1 ? origin.y : origin.z;
        const float32 d = axis == 0 ? direction.x : axis == 1 ? direction.y : direction.z;
        const float32 lo = axis == 0 ? min.x : axis == 1 ? min.y : min.z;
        const float32 hi = axis == 0 ? max.x : axis == 1 ? max.y : max.z;
        if (std::abs(d) < 1.0e-8f) {
            if (o < lo || o > hi) return false;
            continue;
        }
        float32 a = (lo - o) / d;
        float32 b = (hi - o) / d;
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    t = t0;
    return true;
}

bool RayOrientedBox(const Vector3& origin, const Vector3& direction, const Vector3& center, const Quaternion& rotation,
                    const Vector3& size, float32& t) {
    const Quaternion inverse = rotation.Conjugate();
    const Vector3 local_origin = inverse.Rotate(origin - center);
    const Vector3 local_direction = inverse.Rotate(direction);
    return RayBox(local_origin, local_direction, size * -0.5f, size * 0.5f, t);
}

bool RaySphere(const Vector3& origin, const Vector3& direction, const Vector3& center, float32 radius, float32& t) {
    const Vector3 m = origin - center;
    const float32 b = Vector3::Dot(m, direction);
    const float32 c = Vector3::Dot(m, m) - radius * radius;
    if (c > 0.0f && b > 0.0f) return false;
    const float32 disc = b * b - c;
    if (disc < 0.0f) return false;
    t = std::max(0.0f, -b - std::sqrt(disc));
    return true;
}

// File names: lower case letters, digits, '-' and '_'
String SanitizeFileName(const String& text) {
    String out;
    for (const char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out += c;
        else if (c >= 'A' && c <= 'Z') out += static_cast<char>(c - 'A' + 'a');
        else if (c == ' ') out += '_';
    }
    return out.empty() ? String("neue_karte") : out;
}

void PopUtf8(String& text) {
    while (!text.empty()) {
        const unsigned char c = static_cast<unsigned char>(text.back());
        text.pop_back();
        if ((c & 0xC0) != 0x80) break;
    }
}

uint32 HashLabel(const String& label, uint32 counter) {
    uint32 h = 2166136261u ^ counter;
    for (const char c : label) h = (h ^ static_cast<uint8>(c)) * 16777619u;
    return h | 1u;
}

} // namespace

// ============================================================================
// Lifetime
// ============================================================================

void MapEditor::Open(Renderer::SceneRenderer& renderer, const ShiftConfig& config, const String& asset_dir,
                     const String& level_name) {
    config_ = &config;
    asset_dir_ = asset_dir;
    map_dir_ = (std::filesystem::path(asset_dir) / "Maps").string();
    if (!grid_texture_.IsValid()) {
        const auto grid = Renderer::MeshBuilder::GridTexture(256, 4, {0.82f, 0.82f, 0.84f, 1.0f}, {0.62f, 0.62f, 0.66f, 1.0f},
                                                             {0.5f, 0.5f, 0.54f, 1.0f});
        grid_texture_ = renderer.CreateTexture(256, 256, grid.data(), true, true, "EditorGrid");
        sphere_mesh_ = renderer.CreateMesh(Renderer::MeshBuilder::Sphere(1.0f), "EditorSphere");
    }
    RefreshFileLists();
    LoadLevelByName(level_name);
    undo_.clear();
    redo_.clear();
    open_ = true;
    request_ = Request::None;
}

void MapEditor::Close(Renderer::SceneRenderer& renderer) {
    for (auto& [key, mesh] : box_meshes_) renderer.DestroyMesh(mesh);
    box_meshes_.clear();
    for (auto& [path, model] : prop_models_) Renderer::ReleaseModel(renderer, model);
    prop_models_.clear();
    if (sphere_mesh_.IsValid()) renderer.DestroyMesh(sphere_mesh_);
    sphere_mesh_ = {};
    if (grid_texture_.IsValid()) renderer.DestroyTexture(grid_texture_);
    grid_texture_ = {};
    open_ = false;
}

MapEditor::Request MapEditor::TakeRequest() {
    const Request request = request_;
    request_ = Request::None;
    return request;
}

void MapEditor::RefreshFileLists() {
    map_names_ = ListLevelFiles(map_dir_);
    for (const String& builtin : BuiltinLevelNames()) {
        if (std::find(map_names_.begin(), map_names_.end(), builtin) == map_names_.end()) map_names_.push_back(builtin);
    }
    prop_files_.clear();
    std::error_code ec;
    const std::filesystem::path props = std::filesystem::path(asset_dir_) / "Models" / "Props";
    if (std::filesystem::is_directory(props, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(props, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            String ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".glb" && ext != ".gltf" && ext != ".fbx" && ext != ".obj") continue;
            String relative = std::filesystem::relative(entry.path(), asset_dir_, ec).generic_string();
            if (!ec) prop_files_.push_back(relative);
        }
    }
    std::sort(prop_files_.begin(), prop_files_.end());
    prop_index_ = std::clamp(prop_index_, 0, std::max(0, static_cast<int32>(prop_files_.size()) - 1));
}

void MapEditor::NewLevel() {
    PushUndo();
    Level level;
    level.name = "Neue Karte";
    constexpr float32 half = 15.0f;
    level.AddBlock(-half - 0.5f, half + 0.5f, -half - 0.5f, half + 0.5f, Surface::Floor, -0.5f, 0.0f);
    level.AddBlock(-half - 0.5f, half + 0.5f, -half - 0.5f, half + 0.5f, Surface::Ceiling, kWallHeight, kWallHeight + 0.5f);
    level.AddBlock(-half - 0.5f, half + 0.5f, -half - 0.5f, -half, Surface::Wall);
    level.AddBlock(-half - 0.5f, half + 0.5f, half, half + 0.5f, Surface::Wall);
    level.AddBlock(-half - 0.5f, -half, -half, half, Surface::Wall);
    level.AddBlock(half, half + 0.5f, -half, half, Surface::Wall);
    level.spawns[0] = {{-12.0f, 0.0f, 0.0f}, -90.0f};
    level.spawns[1] = {{12.0f, 0.0f, 0.0f}, 90.0f};
    level.has_core = true;
    level.core_home = {0.0f, 0.0f, 0.0f};
    level.exits[0] = {{-12.0f, 0.5f, 10.0f}, {3.0f, 1.0f, 3.0f}};
    level.exits[1] = {{12.0f, 0.5f, -10.0f}, {3.0f, 1.0f, 3.0f}};
    level_ = level;
    file_name_ = "neue_karte";
    selection_ = {};
    problems_.clear();
    preview_state_ = ShiftPlan::InitialState();
    dirty_ = true;
    SetStatus("Neue Karte angelegt (30 x 30 m)");
}

void MapEditor::LoadLevelByName(const String& name) {
    level_ = BuildLevel(name, map_dir_);
    file_name_ = SanitizeFileName(name);
    selection_ = {};
    problems_.clear();
    preview_state_ = ShiftPlan::InitialState();
    dirty_ = false;
    for (size_t i = 0; i < map_names_.size(); ++i) {
        if (map_names_[i] == name) map_index_ = static_cast<int32>(i);
    }
    SetStatus(fmt::format("Karte '{}' geladen", name));
}

void MapEditor::SaveCurrent() {
    file_name_ = SanitizeFileName(file_name_);
    const String path = (std::filesystem::path(map_dir_) / (file_name_ + ".json")).string();
    String error;
    if (!SaveLevel(level_, path, &error)) {
        SetStatus("Speichern fehlgeschlagen: " + error, true);
        return;
    }
    dirty_ = false;
    RefreshFileLists();
    for (size_t i = 0; i < map_names_.size(); ++i) {
        if (map_names_[i] == file_name_) map_index_ = static_cast<int32>(i);
    }
    SetStatus(fmt::format("Gespeichert: Maps/{}.json", file_name_));
}

void MapEditor::RunCheck() {
    problems_ = CheckLevelRoutes(*config_, level_);
    if (problems_.empty()) SetStatus("Pruefung bestanden: alle Wege in allen Wandzustaenden frei");
    else SetStatus(fmt::format("Pruefung: {} Problem(e), siehe rechts", problems_.size()), true);
}

void MapEditor::SetStatus(const String& text, bool error) {
    status_ = text;
    status_error_ = error;
    status_time_ = 8.0f;
    if (error) DRX_LOG_WARNING("Editor: {}", text);
}

// ============================================================================
// Undo
// ============================================================================

void MapEditor::PushUndo() {
    undo_.push_back(level_);
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    redo_.clear();
}

void MapEditor::Undo() {
    if (undo_.empty()) return;
    redo_.push_back(level_);
    level_ = undo_.back();
    undo_.pop_back();
    selection_ = {};
    dirty_ = true;
    SetStatus("Rueckgaengig");
}

void MapEditor::Redo() {
    if (redo_.empty()) return;
    undo_.push_back(level_);
    level_ = redo_.back();
    redo_.pop_back();
    selection_ = {};
    dirty_ = true;
    SetStatus("Wiederholt");
}

void MapEditor::MarkChanged() {
    dirty_ = true;
    confirm_exit_ = false;
    problems_.clear();
}

// ============================================================================
// Editing
// ============================================================================

Math::Vector3 MapEditor::PlacementPoint() const {
    const Ray ray = MouseRay(screen_w_ * 0.5f + (kLeftWidth - kRightWidth) * 0.5f, screen_h_ * 0.5f);
    Vector3 point = cam_pos_ + ray.direction * 8.0f;
    if (ray.direction.y < -0.05f) {
        const float32 t = -cam_pos_.y / ray.direction.y;
        if (t > 0.0f && t < 200.0f) point = cam_pos_ + ray.direction * t;
    }
    return Vector3(Snap(point.x, grid_), 0.0f, Snap(point.z, grid_));
}

float32 MapEditor::SnapCorner(float32 center, float32 size) const {
    return Snap(center - size * 0.5f, grid_) + size * 0.5f;
}

void MapEditor::AddBox(const Vector3& size, Surface surface, float32 bottom) {
    PushUndo();
    const Vector3 at = PlacementPoint();
    LevelBox box;
    box.size = size;
    box.center = Vector3(SnapCorner(at.x, size.x), bottom + size.y * 0.5f, SnapCorner(at.z, size.z));
    box.surface = surface;
    box.casts_shadow = surface != Surface::Ceiling;
    level_.boxes.push_back(box);
    selection_ = {Kind::Box, static_cast<int32>(level_.boxes.size()) - 1};
    MarkChanged();
}

void MapEditor::AddSegment(const Vector3& size, const Vector3& move, float32 rotate, SegmentGroup group) {
    PushUndo();
    const Vector3 at = PlacementPoint();
    MovingSegmentDef def;
    def.name = fmt::format("Wand {}", level_.segments.size() + 1);
    def.size = size;
    def.center = Vector3(SnapCorner(at.x, size.x), size.y * 0.5f, SnapCorner(at.z, size.z));
    def.move = move;
    def.rotate = rotate;
    def.pivot = def.center;
    def.group = group;
    level_.segments.push_back(def);
    selection_ = {Kind::Segment, static_cast<int32>(level_.segments.size()) - 1};
    MarkChanged();
}

void MapEditor::AddProp(const String& model) {
    PushUndo();
    LevelProp prop;
    prop.model = model;
    prop.position = PlacementPoint();
    level_.props.push_back(prop);
    selection_ = {Kind::Prop, static_cast<int32>(level_.props.size()) - 1};
    MarkChanged();
}

void MapEditor::DeleteSelection() {
    const size_t i = static_cast<size_t>(std::max(0, selection_.index));
    switch (selection_.kind) {
        case Kind::Box:
            PushUndo();
            level_.boxes.erase(level_.boxes.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        case Kind::Segment:
            PushUndo();
            level_.segments.erase(level_.segments.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        case Kind::Prop:
            PushUndo();
            level_.props.erase(level_.props.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        case Kind::Core:
            PushUndo();
            level_.has_core = false;
            break;
        default:
            SetStatus("Starts und Ausgaenge koennen nur verschoben werden", true);
            return;
    }
    selection_ = {};
    MarkChanged();
}

void MapEditor::DuplicateSelection() {
    const Vector3 offset(std::max(grid_, 1.0f) * 2.0f, 0.0f, 0.0f);
    const size_t i = static_cast<size_t>(std::max(0, selection_.index));
    switch (selection_.kind) {
        case Kind::Box: {
            PushUndo();
            LevelBox copy = level_.boxes[i];
            copy.center = copy.center + offset;
            level_.boxes.push_back(copy);
            selection_.index = static_cast<int32>(level_.boxes.size()) - 1;
            break;
        }
        case Kind::Segment: {
            PushUndo();
            MovingSegmentDef copy = level_.segments[i];
            copy.center = copy.center + offset;
            copy.pivot = copy.pivot + offset;
            copy.name += " Kopie";
            level_.segments.push_back(copy);
            selection_.index = static_cast<int32>(level_.segments.size()) - 1;
            break;
        }
        case Kind::Prop: {
            PushUndo();
            LevelProp copy = level_.props[i];
            copy.position = copy.position + offset;
            level_.props.push_back(copy);
            selection_.index = static_cast<int32>(level_.props.size()) - 1;
            break;
        }
        default:
            return;
    }
    MarkChanged();
}

void MapEditor::RotateSelection() {
    const size_t i = static_cast<size_t>(std::max(0, selection_.index));
    const auto turn = [](const Vector3& v) { return Vector3(-v.z, v.y, v.x); };   // 90 degrees around +Y
    switch (selection_.kind) {
        case Kind::Box: {
            PushUndo();
            LevelBox& box = level_.boxes[i];
            std::swap(box.size.x, box.size.z);
            break;
        }
        case Kind::Segment: {
            PushUndo();
            MovingSegmentDef& def = level_.segments[i];
            std::swap(def.size.x, def.size.z);
            def.move = turn(def.move);
            def.pivot = def.center + turn(def.pivot - def.center);
            break;
        }
        case Kind::Exit: {
            PushUndo();
            ZoneBox& exit = level_.exits[i];
            std::swap(exit.size.x, exit.size.z);
            break;
        }
        case Kind::Prop:
            PushUndo();
            level_.props[i].yaw = std::fmod(level_.props[i].yaw + 90.0f, 360.0f);
            break;
        case Kind::Spawn:
            PushUndo();
            level_.spawns[i].yaw = std::fmod(level_.spawns[i].yaw + 90.0f, 360.0f);
            break;
        default:
            return;
    }
    MarkChanged();
}

void MapEditor::MoveSelection(const Vector3& delta) {
    const size_t i = static_cast<size_t>(std::max(0, selection_.index));
    switch (selection_.kind) {
        case Kind::Box: level_.boxes[i].center = level_.boxes[i].center + delta; break;
        case Kind::Segment:
            level_.segments[i].center = level_.segments[i].center + delta;
            level_.segments[i].pivot = level_.segments[i].pivot + delta;
            break;
        case Kind::Prop: level_.props[i].position = level_.props[i].position + delta; break;
        case Kind::Spawn: level_.spawns[i].position = level_.spawns[i].position + delta; break;
        case Kind::Core: level_.core_home = level_.core_home + delta; break;
        case Kind::Exit: level_.exits[i].center = level_.exits[i].center + delta; break;
        case Kind::None: return;
    }
    MarkChanged();
}

bool MapEditor::GetSelectionPosition(Vector3& out) const {
    const size_t i = static_cast<size_t>(std::max(0, selection_.index));
    switch (selection_.kind) {
        case Kind::Box: out = level_.boxes[i].center; return true;
        case Kind::Segment: out = level_.segments[i].center; return true;
        case Kind::Prop: out = level_.props[i].position; return true;
        case Kind::Spawn: out = level_.spawns[i].position; return true;
        case Kind::Core: out = level_.core_home; return true;
        case Kind::Exit: out = level_.exits[i].center; return true;
        case Kind::None: break;
    }
    return false;
}

Math::Vector3 MapEditor::SelectionSize() const {
    const size_t i = static_cast<size_t>(std::max(0, selection_.index));
    switch (selection_.kind) {
        case Kind::Box: return level_.boxes[i].size;
        case Kind::Segment: return level_.segments[i].size;
        case Kind::Exit: return level_.exits[i].size;
        default: return Vector3::Zero;
    }
}

bool MapEditor::IsDraggable(const Selection& selection) const {
    // Floors and ceilings span the whole map: moved only by numbers, never by accident
    if (selection.kind == Kind::Box) {
        const Surface surface = level_.boxes[static_cast<size_t>(selection.index)].surface;
        return surface != Surface::Floor && surface != Surface::Ceiling;
    }
    return selection.kind != Kind::None;
}

// ============================================================================
// Camera, picking and input
// ============================================================================

Renderer::CameraParams MapEditor::GetCamera(float32 aspect) const {
    return Renderer::CameraParams::LookAt(cam_pos_, cam_pos_ + LookDirection(cam_yaw_, cam_pitch_), fov_ * kDegToRad, aspect,
                                          0.05f, 500.0f);
}

bool MapEditor::WantsMouseLook(const Input::InputSystem& input) const {
    return open_ && input.IsMouseButtonDown(Input::MouseButton::Right) && !text_focus_;
}

MapEditor::Ray MapEditor::MouseRay(float32 mx, float32 my) const {
    const Vector3 forward = LookDirection(cam_yaw_, cam_pitch_);
    const Vector3 right = Vector3::Cross(forward, Vector3::Up).Normalized();
    const Vector3 up = Vector3::Cross(right, forward);
    const float32 tan_half = std::tan(fov_ * kDegToRad * 0.5f);
    const float32 aspect = screen_h_ > 0.0f ? screen_w_ / screen_h_ : 1.0f;
    const float32 nx = (mx / screen_w_) * 2.0f - 1.0f;
    const float32 ny = 1.0f - (my / screen_h_) * 2.0f;
    return {cam_pos_, (forward + right * (nx * tan_half * aspect) + up * (ny * tan_half)).Normalized()};
}

bool MapEditor::OverPanel(float32 mx, float32 my) const {
    return mx < kLeftWidth || mx > screen_w_ - kRightWidth || my > screen_h_ - kBottomBar;
}

SegmentPose MapEditor::PreviewPose(const MovingSegmentDef& def) const {
    float32 amount = (preview_state_ & GroupBit(def.group)) ? 1.0f : 0.0f;
    if (animate_) {
        const float32 wave = 0.5f - 0.5f * std::cos(anim_time_ * 0.8f);
        amount = std::abs(amount - wave);
    }
    return def.PoseAt(amount);
}

bool MapEditor::Pick(const Ray& ray, Hit& hit) const {
    float32 best = std::numeric_limits<float32>::max();
    Selection found;
    const auto consider = [&](float32 t, Kind kind, size_t index) {
        if (t < best) {
            best = t;
            found = {kind, static_cast<int32>(index)};
        }
    };
    float32 t = 0.0f;
    for (size_t i = 0; i < level_.boxes.size(); ++i) {
        const LevelBox& box = level_.boxes[i];
        if (hide_ceiling_ && box.surface == Surface::Ceiling) continue;
        if (RayBox(ray.origin, ray.direction, box.center - box.size * 0.5f, box.center + box.size * 0.5f, t)) consider(t, Kind::Box, i);
    }
    for (size_t i = 0; i < level_.segments.size(); ++i) {
        const SegmentPose pose = PreviewPose(level_.segments[i]);
        if (RayOrientedBox(ray.origin, ray.direction, pose.center, pose.Rotation(), level_.segments[i].size, t)) {
            consider(t, Kind::Segment, i);
        }
    }
    for (size_t i = 0; i < level_.props.size(); ++i) {
        const LevelProp& prop = level_.props[i];
        const auto it = prop_models_.find(prop.model);
        Vector3 center = prop.position + Vector3(0.0f, 0.5f, 0.0f);
        float32 radius = 0.5f;
        if (it != prop_models_.end() && it->second.IsValid()) {
            const Renderer::AABB& b = it->second.bounds;
            const Quaternion yaw = Quaternion::FromAxisAngle(Vector3::Up, prop.yaw * kDegToRad);
            center = prop.position + yaw.Rotate(b.GetCenter() * prop.scale);
            radius = std::max(0.25f, (b.max - b.min).Length() * 0.5f * prop.scale);
        }
        if (RaySphere(ray.origin, ray.direction, center, radius, t)) consider(t, Kind::Prop, i);
    }
    const float32 h = config_ ? config_->player.height : 1.8f;
    for (size_t i = 0; i < 2; ++i) {
        const Vector3 feet = level_.spawns[i].position;
        if (RayBox(ray.origin, ray.direction, feet - Vector3(0.35f, 0.0f, 0.35f), feet + Vector3(0.35f, h, 0.35f), t)) {
            consider(std::max(0.0f, t - 0.5f), Kind::Spawn, i);
        }
        const ZoneBox& exit = level_.exits[i];
        const Vector3 half(exit.size.x * 0.5f, kExitHeight * 0.5f, exit.size.z * 0.5f);
        const Vector3 base(exit.center.x, kExitHeight * 0.5f, exit.center.z);
        if (RayBox(ray.origin, ray.direction, base - half, base + half, t)) consider(std::max(0.0f, t - 0.5f), Kind::Exit, i);
    }
    if (level_.has_core && RaySphere(ray.origin, ray.direction, level_.core_home + Vector3(0, 0.4f, 0), 0.5f, t)) {
        consider(std::max(0.0f, t - 0.5f), Kind::Core, 0);
    }
    if (found.kind == Kind::None) return false;
    hit.selection = found;
    hit.distance = best;
    return true;
}

void MapEditor::Update(Input::InputSystem& input, float32 dt, float32 screen_width, float32 screen_height, bool mouse_look) {
    using Input::KeyCode;
    screen_w_ = std::max(1.0f, screen_width);
    screen_h_ = std::max(1.0f, screen_height);
    anim_time_ += dt;
    status_time_ = std::max(0.0f, status_time_ - dt);

    // Fly camera
    if (mouse_look) {
        const Vector2 delta = input.GetMouseDelta();
        cam_yaw_ = std::fmod(cam_yaw_ + delta.x * 0.12f, 360.0f);
        cam_pitch_ = std::clamp(cam_pitch_ - delta.y * 0.12f, -89.0f, 89.0f);
        const float32 speed = (input.IsKeyDown(KeyCode::LeftShift) ? 30.0f : 10.0f) * dt;
        const Vector3 forward = LookDirection(cam_yaw_, cam_pitch_);
        const Vector3 right = FlatRight(cam_yaw_);
        if (input.IsKeyDown(KeyCode::W)) cam_pos_ = cam_pos_ + forward * speed;
        if (input.IsKeyDown(KeyCode::S)) cam_pos_ = cam_pos_ - forward * speed;
        if (input.IsKeyDown(KeyCode::D)) cam_pos_ = cam_pos_ + right * speed;
        if (input.IsKeyDown(KeyCode::A)) cam_pos_ = cam_pos_ - right * speed;
        if (input.IsKeyDown(KeyCode::E)) cam_pos_.y += speed;
        if (input.IsKeyDown(KeyCode::Q)) cam_pos_.y -= speed;
        return;
    }
    if (text_focus_) return;

    // Keyboard editing
    const bool ctrl = input.IsKeyDown(KeyCode::LeftControl) || input.IsKeyDown(KeyCode::RightControl);
    if (ctrl && input.IsKeyPressed(KeyCode::Z)) Undo();
    if (ctrl && input.IsKeyPressed(KeyCode::Y)) Redo();
    if (ctrl && input.IsKeyPressed(KeyCode::S)) SaveCurrent();
    if (ctrl && input.IsKeyPressed(KeyCode::D)) DuplicateSelection();
    if (!ctrl && input.IsKeyPressed(KeyCode::Delete)) DeleteSelection();
    if (!ctrl && input.IsKeyPressed(KeyCode::R)) RotateSelection();
    if (!ctrl && input.IsKeyPressed(KeyCode::G)) {
        size_t next = 0;
        for (size_t i = 0; i < 4; ++i) {
            if (std::abs(kGridSizes[i] - grid_) < 1.0e-4f) next = (i + 1) % 4;
        }
        grid_ = kGridSizes[next];
        SetStatus(fmt::format("Raster {}", kGridNames[next]));
    }
    if (!ctrl && input.IsKeyPressed(KeyCode::F)) {
        Vector3 target;
        if (GetSelectionPosition(target)) {
            const float32 distance = std::max(6.0f, SelectionSize().Length() * 1.2f);
            cam_pos_ = target - LookDirection(cam_yaw_, cam_pitch_) * distance;
        }
    }
    Vector3 nudge = Vector3::Zero;
    if (input.IsKeyPressed(KeyCode::Left)) nudge.x -= grid_;
    if (input.IsKeyPressed(KeyCode::Right)) nudge.x += grid_;
    if (input.IsKeyPressed(KeyCode::Up)) nudge.z -= grid_;
    if (input.IsKeyPressed(KeyCode::Down)) nudge.z += grid_;
    if (input.IsKeyPressed(KeyCode::PageUp)) nudge.y += grid_;
    if (input.IsKeyPressed(KeyCode::PageDown)) nudge.y -= grid_;
    if (nudge.LengthSquared() > 0.0f && selection_.kind != Kind::None) {
        PushUndo();
        MoveSelection(nudge);
    }

    // Mouse: select and drag
    const Vector2 mouse = input.GetMousePosition();
    if (input.IsMouseButtonPressed(Input::MouseButton::Left) && !OverPanel(mouse.x, mouse.y)) {
        Hit hit;
        const Ray ray = MouseRay(mouse.x, mouse.y);
        if (Pick(ray, hit)) {
            selection_ = hit.selection;
            Vector3 position;
            dragging_ = IsDraggable(selection_) && GetSelectionPosition(position);
            if (dragging_) {
                const Vector3 point = ray.origin + ray.direction * hit.distance;
                drag_plane_y_ = point.y;
                drag_offset_ = position - point;
                drag_moved_ = false;
                press_mouse_ = mouse;
            }
        } else {
            selection_ = {};
        }
    }
    if (dragging_ && input.IsMouseButtonDown(Input::MouseButton::Left)) {
        if (!drag_moved_ && (mouse - press_mouse_).Length() < 4.0f) return;
        const Ray ray = MouseRay(mouse.x, mouse.y);
        if (std::abs(ray.direction.y) < 1.0e-4f) return;
        const float32 t = (drag_plane_y_ - ray.origin.y) / ray.direction.y;
        if (t <= 0.0f || t > 500.0f) return;
        const Vector3 wanted = ray.origin + ray.direction * t + drag_offset_;
        Vector3 current;
        if (!GetSelectionPosition(current)) return;
        const Vector3 size = SelectionSize();
        const Vector3 snapped(size.x > 0.0f ? SnapCorner(wanted.x, size.x) : Snap(wanted.x, grid_), current.y,
                              size.z > 0.0f ? SnapCorner(wanted.z, size.z) : Snap(wanted.z, grid_));
        const Vector3 delta = snapped - current;
        if (delta.LengthSquared() > 1.0e-8f) {
            if (!drag_moved_) PushUndo();
            drag_moved_ = true;
            MoveSelection(delta);
        }
    }
    if (!input.IsMouseButtonDown(Input::MouseButton::Left)) dragging_ = false;
}

// ============================================================================
// Rendering
// ============================================================================

MeshHandle MapEditor::BoxMesh(Renderer::SceneRenderer& renderer, const Vector3& size) {
    const SizeKey key{static_cast<int32>(std::lround(size.x * 1000.0f)), static_cast<int32>(std::lround(size.y * 1000.0f)),
                      static_cast<int32>(std::lround(size.z * 1000.0f))};
    const auto it = box_meshes_.find(key);
    if (it != box_meshes_.end()) return it->second;
    const MeshHandle mesh = renderer.CreateMesh(Renderer::MeshBuilder::Box(size), "EditorBox");
    box_meshes_[key] = mesh;
    return mesh;
}

const Renderer::Model* MapEditor::PropModel(Renderer::SceneRenderer& renderer, const String& model) {
    auto it = prop_models_.find(model);
    if (it == prop_models_.end()) {
        Renderer::Model loaded;
        Renderer::ModelData data;
        const String path = (std::filesystem::path(asset_dir_) / model).string();
        if (!Renderer::LoadModelData(path, data) || !Renderer::UploadModel(renderer, data, loaded)) {
            SetStatus(fmt::format("Modell '{}' nicht ladbar", model), true);
        }
        it = prop_models_.emplace(model, std::move(loaded)).first;   // failures are cached as empty models
    }
    return it->second.IsValid() ? &it->second : nullptr;
}

void MapEditor::Render(Renderer::SceneRenderer& renderer) {
    Debug::DebugRenderer& debug = Debug::DebugRenderer::Instance();
    const auto selected = [&](Kind kind, size_t index) {
        return selection_.kind == kind && selection_.index == static_cast<int32>(index);
    };

    for (size_t i = 0; i < level_.boxes.size(); ++i) {
        const LevelBox& box = level_.boxes[i];
        if (hide_ceiling_ && box.surface == Surface::Ceiling) continue;
        Renderer::Material material;
        material.albedo = grid_texture_;
        material.color = SurfaceColor(box.surface);
        material.cast_shadows = box.casts_shadow;
        if (!box.collision) material.color = Vector4(material.color.x * 0.7f, material.color.y * 0.7f, material.color.z * 0.7f, 1.0f);
        if (selected(Kind::Box, i)) {
            material.emissive = {0.9f, 0.7f, 0.1f};
            material.emissive_strength = 0.35f;
            debug.DrawWireBox(box.center, box.size + Vector3(0.02f), {}, kSelect);
        }
        renderer.DrawMesh(BoxMesh(renderer, box.size), Matrix4::Translation(box.center), material);
    }

    for (size_t i = 0; i < level_.segments.size(); ++i) {
        const MovingSegmentDef& def = level_.segments[i];
        const SegmentPose pose = PreviewPose(def);
        Renderer::Material material;
        material.albedo = grid_texture_;
        material.color = SurfaceColor(Surface::Moving);
        if (selected(Kind::Segment, i)) {
            material.emissive = {0.9f, 0.7f, 0.1f};
            material.emissive_strength = 0.35f;
            // Both end states and the pivot
            const SegmentPose a = def.PoseAt(0.0f);
            const SegmentPose b = def.PoseAt(1.0f);
            debug.DrawWireBox(a.center, def.size + Vector3(0.02f), a.Rotation(), {0.3f, 1.0f, 0.4f, 1.0f});
            debug.DrawWireBox(b.center, def.size + Vector3(0.02f), b.Rotation(), {1.0f, 0.35f, 0.3f, 1.0f});
            if (std::abs(def.rotate) > 0.01f) debug.DrawWireSphere(def.pivot, 0.25f, kSelect);
            else debug.DrawArrow(a.center, b.center, kSelect);
        }
        renderer.DrawMesh(BoxMesh(renderer, def.size), Matrix4::Translation(pose.center) * pose.Rotation().ToMatrix(), material);
    }

    for (size_t i = 0; i < level_.props.size(); ++i) {
        const LevelProp& prop = level_.props[i];
        const Matrix4 transform = Matrix4::Translation(prop.position) *
                                  Quaternion::FromAxisAngle(Vector3::Up, prop.yaw * kDegToRad).ToMatrix() *
                                  Matrix4::Scale(prop.scale);
        if (const Renderer::Model* model = PropModel(renderer, prop.model)) {
            Renderer::DrawModel(renderer, *model, transform);
            if (selected(Kind::Prop, i)) {
                const Renderer::AABB& b = model->bounds;
                debug.DrawWireBox(prop.position + Vector3(0.0f, b.GetCenter().y * prop.scale, 0.0f), (b.max - b.min) * prop.scale,
                                  Quaternion::FromAxisAngle(Vector3::Up, prop.yaw * kDegToRad), kSelect);
            }
        } else {
            Renderer::Material missing;
            missing.color = {1.0f, 0.0f, 1.0f, 1.0f};
            renderer.DrawMesh(BoxMesh(renderer, Vector3(0.5f)), Matrix4::Translation(prop.position + Vector3(0, 0.25f, 0)), missing);
        }
    }

    // Starts, core and exits
    const float32 h = config_ ? config_->player.height : 1.8f;
    for (size_t i = 0; i < 2; ++i) {
        const Team team = i == 0 ? Team::A : Team::B;
        const SpawnPoint& spawn = level_.spawns[i];
        Renderer::Material body;
        body.color = TeamColor(team);
        body.emissive = Vector3(body.color.x, body.color.y, body.color.z);
        body.emissive_strength = selected(Kind::Spawn, i) ? 1.2f : 0.4f;
        renderer.DrawMesh(BoxMesh(renderer, Vector3(0.6f, h, 0.6f)), Matrix4::Translation(spawn.position + Vector3(0, h * 0.5f, 0)), body);
        debug.DrawArrow(spawn.position + Vector3(0, 1.0f, 0), spawn.position + Vector3(0, 1.0f, 0) + FlatForward(spawn.yaw) * 1.5f,
                        TeamColor(team));
        debug.DrawText(spawn.position + Vector3(0, h + 0.4f, 0), i == 0 ? "START BLAU" : "START ROT", TeamColor(team));

        const ZoneBox& exit = level_.exits[i];
        Renderer::Material plate;
        plate.color = TeamColor(team);
        plate.emissive = Vector3(plate.color.x, plate.color.y, plate.color.z);
        plate.emissive_strength = selected(Kind::Exit, i) ? 1.5f : 0.7f;
        plate.cast_shadows = false;
        renderer.DrawMesh(BoxMesh(renderer, Vector3(exit.size.x, 0.04f, exit.size.z)),
                          Matrix4::Translation(Vector3(exit.center.x, 0.02f, exit.center.z)), plate);
        debug.DrawText(Vector3(exit.center.x, 1.5f, exit.center.z), i == 0 ? "AUSGANG BLAU" : "AUSGANG ROT", TeamColor(team));
        if (selected(Kind::Exit, i)) {
            debug.DrawWireBox(Vector3(exit.center.x, kExitHeight * 0.5f, exit.center.z), Vector3(exit.size.x, kExitHeight, exit.size.z), {},
                              kSelect);
        }
    }
    if (level_.has_core) {
        Renderer::Material glow;
        glow.color = {1.0f, 0.9f, 0.4f, 1.0f};
        glow.emissive = {1.0f, 0.85f, 0.3f};
        glow.emissive_strength = selected(Kind::Core, 0) ? 4.0f : 2.0f;
        glow.cast_shadows = false;
        renderer.DrawMesh(sphere_mesh_, Matrix4::Translation(level_.core_home + Vector3(0, 0.4f, 0)) * Matrix4::Scale(0.3f), glow);
        debug.DrawText(level_.core_home + Vector3(0, 1.3f, 0), "KERN", {1.0f, 0.9f, 0.4f, 1.0f});
    }

    // Placement marker
    const Vector3 at = PlacementPoint();
    debug.DrawWireBox(at + Vector3(0, 0.01f, 0), Vector3(grid_, 0.02f, grid_), {}, {1.0f, 1.0f, 1.0f, 0.6f});
}

// ============================================================================
// UI
// ============================================================================

bool MapEditor::Hovered(float32 x, float32 y, float32 w, float32 h) const {
    return mouse_.x >= x && mouse_.x < x + w && mouse_.y >= y && mouse_.y < y + h;
}

void MapEditor::Header(Layout& l, const String& text) {
    l.y += 6.0f;
    l.ui->DrawText(text, l.x, l.y + 3.0f, kHeader, kFont + 1.0f);
    l.y += kRowHeight;
}

void MapEditor::Label(Layout& l, const String& text, const Vector4& color) {
    l.ui->DrawText(text, l.x, l.y + 4.0f, color, kFont - 1.0f);
    l.y += kRowHeight - 4.0f;
}

bool MapEditor::Button(Layout& l, const String& label, float32 x, float32 width, bool highlight) {
    const float32 h = kRowHeight - 3.0f;
    const bool hovered = Hovered(x, l.y, width, h);
    l.ui->DrawRect({x, l.y, width, h}, highlight ? kButtonHighlight : hovered ? kButtonHover : kButtonColor);
    const float32 tw = l.ui->GetTextWidth(label, kFont - 1.0f);
    l.ui->DrawText(label, x + (width - tw) * 0.5f, l.y + 3.0f, kText, kFont - 1.0f);
    return hovered && mouse_clicked_;
}

bool MapEditor::FullButton(Layout& l, const String& label, bool highlight) {
    const bool pressed = Button(l, label, l.x, l.width, highlight);
    l.y += kRowHeight;
    return pressed;
}

int32 MapEditor::ButtonRow(Layout& l, std::initializer_list<const char*> labels) {
    const float32 gap = 4.0f;
    const float32 count = static_cast<float32>(labels.size());
    const float32 w = (l.width - gap * (count - 1.0f)) / count;
    int32 pressed = -1;
    int32 i = 0;
    for (const char* label : labels) {
        if (Button(l, label, l.x + static_cast<float32>(i) * (w + gap), w)) pressed = i;
        ++i;
    }
    l.y += kRowHeight;
    return pressed;
}

bool MapEditor::Number(Layout& l, const String& label, float32& value, float32 step, float32 min_value, float32 max_value) {
    const uint32 id = HashLabel(label, ++widget_counter_);
    const float32 label_w = l.width * 0.42f;
    const float32 bw = 22.0f;
    const float32 field_x = l.x + label_w + bw + 2.0f;
    const float32 field_w = l.width - label_w - 2.0f * (bw + 2.0f);
    const float32 h = kRowHeight - 3.0f;
    l.ui->DrawText(label, l.x, l.y + 3.0f, kText, kFont - 1.0f);

    const float32 before = value;
    if (Button(l, "-", l.x + label_w, bw)) value -= step;
    if (Button(l, "+", field_x + field_w + 2.0f, bw)) value += step;
    const bool hovered = Hovered(field_x, l.y, field_w, h);
    // Drag left / right on the value; mouse wheel steps
    if (hovered && mouse_clicked_) {
        active_widget_ = id;
        widget_start_x_ = mouse_.x;
        widget_start_value_ = value;
        widget_pushed_undo_ = false;
    }
    if (active_widget_ == id) {
        if (!mouse_down_) {
            active_widget_ = 0;
        } else {
            const float32 steps = std::round((mouse_.x - widget_start_x_) / 10.0f);
            value = widget_start_value_ + steps * step;
        }
    }
    if (hovered && scroll_ != 0.0f) value += (scroll_ > 0.0f ? step : -step);
    value = std::clamp(Snap(value, step * 0.5f), min_value, max_value);
    l.ui->DrawRect({field_x, l.y, field_w, h}, active_widget_ == id ? kButtonHighlight : hovered ? kButtonHover : kFieldColor);
    const String text = fmt::format("{:.2f}", value);
    const float32 tw = l.ui->GetTextWidth(text, kFont - 1.0f);
    l.ui->DrawText(text, field_x + (field_w - tw) * 0.5f, l.y + 3.0f, kText, kFont - 1.0f);
    l.y += kRowHeight;

    const bool changed = std::abs(value - before) > 1.0e-6f;
    if (changed && !widget_pushed_undo_) {
        // One undo step per drag or click
        const float32 now = value;
        value = before;
        PushUndo();
        value = now;
        widget_pushed_undo_ = active_widget_ == id;
    }
    return changed;
}

bool MapEditor::Choice(Layout& l, const String& label, int32& index, const Array<String>& items) {
    if (items.empty()) {
        Label(l, label + ": (keine)");
        return false;
    }
    const float32 label_w = l.width * 0.42f;
    const float32 bw = 22.0f;
    const float32 field_x = l.x + label_w + bw + 2.0f;
    const float32 field_w = l.width - label_w - 2.0f * (bw + 2.0f);
    l.ui->DrawText(label, l.x, l.y + 3.0f, kText, kFont - 1.0f);
    const int32 count = static_cast<int32>(items.size());
    const int32 before = index;
    if (Button(l, "<", l.x + label_w, bw)) index = (index + count - 1) % count;
    if (Button(l, ">", field_x + field_w + 2.0f, bw)) index = (index + 1) % count;
    if (Button(l, "", field_x, field_w)) index = (index + 1) % count;
    index = std::clamp(index, 0, count - 1);
    String text = items[static_cast<size_t>(index)];
    while (text.size() > 4 && l.ui->GetTextWidth(text, kFont - 1.0f) > field_w - 8.0f) text = "..." + text.substr(4);
    const float32 tw = l.ui->GetTextWidth(text, kFont - 1.0f);
    l.ui->DrawText(text, field_x + (field_w - tw) * 0.5f, l.y + 3.0f, kText, kFont - 1.0f);
    l.y += kRowHeight;
    return index != before;
}

bool MapEditor::Toggle(Layout& l, const String& label, bool& value) {
    const float32 box = kRowHeight - 8.0f;
    const bool hovered = Hovered(l.x, l.y, l.width, kRowHeight - 3.0f);
    l.ui->DrawRect({l.x, l.y + 2.0f, box, box}, hovered ? kButtonHover : kFieldColor);
    if (value) l.ui->DrawRect({l.x + 4.0f, l.y + 6.0f, box - 8.0f, box - 8.0f}, kButtonHighlight + Vector4(0.2f, 0.2f, 0.2f, 0.0f));
    l.ui->DrawText(label, l.x + box + 8.0f, l.y + 3.0f, kText, kFont - 1.0f);
    const bool pressed = hovered && mouse_clicked_;
    if (pressed) value = !value;
    l.y += kRowHeight;
    return pressed;
}

void MapEditor::TextField(Layout& l, const String& label, String& text) {
    const float32 label_w = l.width * 0.3f;
    const float32 h = kRowHeight - 3.0f;
    const float32 fx = l.x + label_w;
    const float32 fw = l.width - label_w;
    l.ui->DrawText(label, l.x, l.y + 3.0f, kText, kFont - 1.0f);
    const bool hovered = Hovered(fx, l.y, fw, h);
    const uint32 id = HashLabel(label, 0);
    bool focused = text_focus_ && focus_field_ == id;
    if (mouse_clicked_) {
        if (hovered && !focused) {
            if (!text_focus_) l.input->BeginTextInput();
            l.input->ClearTextInput();
            text_focus_ = true;
            focus_field_ = id;
        } else if (!hovered && focused) {
            l.input->EndTextInput();
            text_focus_ = false;
        }
        focused = text_focus_ && focus_field_ == id;
    }
    if (focused) {
        if (!l.input->GetTextInput().empty()) {
            text += l.input->GetTextInput();
            l.input->ClearTextInput();
            dirty_ = true;
        }
        if (l.input->IsKeyPressed(Input::KeyCode::Backspace)) PopUtf8(text);
        if (l.input->IsKeyPressed(Input::KeyCode::Enter) || l.input->IsKeyPressed(Input::KeyCode::Escape)) {
            l.input->EndTextInput();
            text_focus_ = false;
            focused = false;
        }
    }
    l.ui->DrawRect({fx, l.y, fw, h}, focused ? kButtonHighlight : hovered ? kButtonHover : kFieldColor);
    const bool blink = focused && std::fmod(anim_time_, 1.0f) < 0.5f;
    l.ui->DrawText(text + (blink ? "|" : ""), fx + 6.0f, l.y + 3.0f, kText, kFont - 1.0f);
    l.y += kRowHeight;
}

void MapEditor::DrawUI(UI::UIContext& ui, Input::InputSystem& input, float32 width, float32 height) {
    screen_w_ = std::max(1.0f, width);
    screen_h_ = std::max(1.0f, height);
    mouse_ = input.GetMousePosition();
    mouse_down_ = input.IsMouseButtonDown(Input::MouseButton::Left);
    mouse_clicked_ = input.IsMouseButtonPressed(Input::MouseButton::Left) && !input.IsMouseButtonDown(Input::MouseButton::Right);
    scroll_ = input.GetMouseScroll().y;
    widget_counter_ = 0;
    if (!mouse_down_) widget_pushed_undo_ = false;

    ui.DrawRect({0.0f, 0.0f, kLeftWidth, height - kBottomBar}, kPanelColor);
    ui.DrawRect({width - kRightWidth, 0.0f, kRightWidth, height - kBottomBar}, kPanelColor);
    ui.DrawRect({0.0f, height - kBottomBar, width, kBottomBar}, kPanelColor);

    Layout left{&ui, &input, 12.0f, 8.0f, kLeftWidth - 24.0f};
    DrawFilePanel(left);
    DrawBuildPanel(left);
    DrawViewPanel(left);

    Layout right{&ui, &input, width - kRightWidth + 12.0f, 8.0f, kRightWidth - 24.0f};
    DrawProperties(right);

    // Status and help
    const Vector4 status_color = status_error_ ? Vector4(1.0f, 0.45f, 0.4f, 1.0f) : Vector4(0.5f, 1.0f, 0.6f, 1.0f);
    if (status_time_ > 0.0f) ui.DrawText(status_, 12.0f, height - kBottomBar + 4.0f, status_color, kFont);
    ui.DrawText("Rechte Maus + WASD/QE fliegen (Shift schnell)  |  Links: waehlen/ziehen  |  Entf loeschen  |  Strg+D kopieren  |  "
                "R drehen  |  Pfeile/Bild verschieben  |  Strg+Z/Y  |  Strg+S speichern  |  F zeigen  |  G Raster",
                12.0f, height - kBottomBar + 24.0f, {0.7f, 0.75f, 0.85f, 1.0f}, kFont - 2.0f);
    const String title = fmt::format("{}{}", file_name_, dirty_ ? " *" : "");
    ui.DrawText(title, kLeftWidth + 12.0f, 10.0f, {1, 1, 1, 0.9f}, kFont + 3.0f);
}

void MapEditor::DrawFilePanel(Layout& l) {
    Header(l, "KARTE");
    TextField(l, "Datei", file_name_);
    TextField(l, "Titel", level_.name);
    int32 pressed = ButtonRow(l, {"Speichern", "Pruefen", "Testen"});
    if (pressed == 0) SaveCurrent();
    if (pressed == 1) RunCheck();
    if (pressed == 2) {
        problems_ = CheckLevelRoutes(*config_, level_);
        if (!problems_.empty()) SetStatus("Hinweis: die Pruefung hat Probleme gefunden (rechts)", true);
        request_ = Request::Play;
    }
    Choice(l, "Oeffnen", map_index_, map_names_);
    pressed = ButtonRow(l, {"Laden", "Neu", "Menue"});
    if (pressed == 0 && !map_names_.empty()) {
        PushUndo();
        LoadLevelByName(map_names_[static_cast<size_t>(map_index_)]);
    }
    if (pressed == 1) NewLevel();
    if (pressed == 2) {
        if (dirty_ && !confirm_exit_) {
            confirm_exit_ = true;
            SetStatus("Nicht gespeichert! Nochmal 'Menue' klicken zum Verlassen", true);
        } else {
            request_ = Request::Exit;
        }
    }
}

void MapEditor::DrawBuildPanel(Layout& l) {
    Header(l, "BAUEN");
    int32 pressed = ButtonRow(l, {"Wand", "Halbe Wand", "Boden"});
    if (pressed == 0) AddBox({4.0f, kWallHeight, 0.5f}, Surface::Wall, 0.0f);
    if (pressed == 1) AddBox({2.0f, kWallHeight, 0.5f}, Surface::Wall, 0.0f);
    if (pressed == 2) AddBox({8.0f, 0.5f, 8.0f}, Surface::Floor, -0.5f);
    pressed = ButtonRow(l, {"Deckung", "Hoch", "Stufe"});
    if (pressed == 0) AddBox({1.5f, 1.0f, 0.6f}, Surface::Cover, 0.0f);
    if (pressed == 1) AddBox({1.5f, 1.8f, 0.6f}, Surface::Cover, 0.0f);
    if (pressed == 2) AddBox({2.0f, 0.25f, 1.0f}, Surface::Step, 0.0f);
    pressed = ButtonRow(l, {"Plattform", "Decke"});
    if (pressed == 0) AddBox({4.0f, 1.0f, 4.0f}, Surface::Floor, 0.0f);
    if (pressed == 1) AddBox({8.0f, 0.5f, 8.0f}, Surface::Ceiling, kWallHeight);
    Label(l, "Bewegliche Waende:");
    pressed = ButtonRow(l, {"Schiebetor", "Senktuer", "Drehwand"});
    if (pressed == 0) AddSegment({4.0f, kWallHeight, 0.5f}, {4.0f, 0.0f, 0.0f}, 0.0f, SegmentGroup::NorthGates);
    if (pressed == 1) AddSegment({3.0f, kWallHeight, 0.5f}, {0.0f, -(kWallHeight + 0.1f), 0.0f}, 0.0f, SegmentGroup::SouthGates);
    if (pressed == 2) AddSegment({6.0f, kWallHeight, 0.4f}, Vector3::Zero, 90.0f, SegmentGroup::MachineRoom);
    Label(l, "Deko (Assets/Models/Props):");
    Choice(l, "Modell", prop_index_, prop_files_);
    pressed = ButtonRow(l, {"Deko setzen", "Liste neu"});
    if (pressed == 0) {
        if (prop_files_.empty()) SetStatus("Keine Modelle in Assets/Models/Props", true);
        else AddProp(prop_files_[static_cast<size_t>(prop_index_)]);
    }
    if (pressed == 1) RefreshFileLists();
}

void MapEditor::DrawViewPanel(Layout& l) {
    Header(l, "ANSICHT");
    int32 grid_index = 1;
    for (int32 i = 0; i < 4; ++i) {
        if (std::abs(kGridSizes[i] - grid_) < 1.0e-4f) grid_index = i;
    }
    if (Choice(l, "Raster", grid_index, kGridNames)) grid_ = kGridSizes[grid_index];
    Toggle(l, "Decke ausblenden", hide_ceiling_);
    Toggle(l, "Waende bewegen (Vorschau)", animate_);
    // End state per group that exists in the map
    GroupMask groups = 0;
    for (const MovingSegmentDef& def : level_.segments) groups |= GroupBit(def.group);
    if (groups) Label(l, "Gruppen im Endzustand zeigen:");
    for (uint32 g = 0; g < static_cast<uint32>(SegmentGroup::Count); ++g) {
        const SegmentGroup group = static_cast<SegmentGroup>(g);
        if (!(groups & GroupBit(group))) continue;
        bool on = (preview_state_ & GroupBit(group)) != 0;
        if (Toggle(l, SegmentGroupName(group), on)) preview_state_ ^= GroupBit(group);
    }
}

void MapEditor::DrawProperties(Layout& l) {
    Header(l, "EIGENSCHAFTEN");
    const size_t i = static_cast<size_t>(std::max(0, selection_.index));
    bool changed = false;
    const auto vector_rows = [&](const char* name, Vector3& v, float32 step, float32 min_value, float32 max_value) {
        changed |= Number(l, fmt::format("{} X", name), v.x, step, min_value, max_value);
        changed |= Number(l, fmt::format("{} Y", name), v.y, step, min_value, max_value);
        changed |= Number(l, fmt::format("{} Z", name), v.z, step, min_value, max_value);
    };

    switch (selection_.kind) {
        case Kind::None: {
            Label(l, "Nichts gewaehlt - Objekt anklicken");
            Label(l, "oder links unter BAUEN anlegen.");
            bool has_core = level_.has_core;
            if (Toggle(l, "Karte hat einen Kern", has_core)) {
                PushUndo();
                level_.has_core = has_core;
                MarkChanged();
            }
            bool toggle_mode = level_.shift_mode == ShiftMode::ToggleAll;
            if (Toggle(l, "Alle Waende wechseln jedes Mal", toggle_mode)) {
                PushUndo();
                level_.shift_mode = toggle_mode ? ShiftMode::ToggleAll : ShiftMode::Patterns;
                MarkChanged();
            }
            Label(l, fmt::format("{} Bloecke, {} bewegliche Waende, {} Deko", level_.boxes.size(), level_.segments.size(),
                                 level_.props.size()));
            break;
        }
        case Kind::Box: {
            LevelBox& box = level_.boxes[i];
            Label(l, fmt::format("Block {}", i + 1), kHeader);
            vector_rows("Pos", box.center, grid_, -500.0f, 500.0f);
            vector_rows("Groesse", box.size, grid_, 0.05f, 500.0f);
            int32 surface = static_cast<int32>(box.surface);
            if (Choice(l, "Art", surface, kSurfaceNames)) {
                PushUndo();
                box.surface = static_cast<Surface>(surface);
                box.casts_shadow = box.surface != Surface::Ceiling;
                changed = true;
            }
            bool collision = box.collision;
            if (Toggle(l, "Kollision", collision)) {
                PushUndo();
                box.collision = collision;
                changed = true;
            }
            break;
        }
        case Kind::Segment: {
            MovingSegmentDef& def = level_.segments[i];
            Label(l, def.name, kHeader);
            int32 group = static_cast<int32>(def.group);
            Array<String> groups;
            for (uint32 g = 0; g < static_cast<uint32>(SegmentGroup::Count); ++g) groups.push_back(SegmentGroupName(static_cast<SegmentGroup>(g)));
            if (Choice(l, "Gruppe", group, groups)) {
                PushUndo();
                def.group = static_cast<SegmentGroup>(group);
                changed = true;
            }
            const Vector3 old_center = def.center;
            vector_rows("Pos", def.center, grid_, -500.0f, 500.0f);
            def.pivot = def.pivot + (def.center - old_center);
            vector_rows("Groesse", def.size, grid_, 0.05f, 500.0f);
            vector_rows("Weg", def.move, grid_, -100.0f, 100.0f);
            changed |= Number(l, "Drehung", def.rotate, 15.0f, -180.0f, 180.0f);
            if (std::abs(def.rotate) > 0.01f) vector_rows("Drehpunkt", def.pivot, grid_, -500.0f, 500.0f);
            const float32 seconds = SegmentMoveDuration(config_->shifts, def);
            Label(l, fmt::format("Bewegung dauert {:.1f} s", seconds), {0.7f, 0.8f, 1.0f, 1.0f});
            break;
        }
        case Kind::Prop: {
            LevelProp& prop = level_.props[i];
            Label(l, prop.model, kHeader);
            vector_rows("Pos", prop.position, grid_, -500.0f, 500.0f);
            changed |= Number(l, "Drehung", prop.yaw, 15.0f, -360.0f, 360.0f);
            changed |= Number(l, "Skalierung", prop.scale, 0.05f, 0.01f, 100.0f);
            break;
        }
        case Kind::Spawn: {
            SpawnPoint& spawn = level_.spawns[i];
            Label(l, i == 0 ? "Start Team Blau" : "Start Team Rot", kHeader);
            vector_rows("Pos", spawn.position, grid_, -500.0f, 500.0f);
            changed |= Number(l, "Blickrichtung", spawn.yaw, 15.0f, -360.0f, 360.0f);
            break;
        }
        case Kind::Core: {
            Label(l, "Kern", kHeader);
            vector_rows("Pos", level_.core_home, grid_, -500.0f, 500.0f);
            break;
        }
        case Kind::Exit: {
            ZoneBox& exit = level_.exits[i];
            Label(l, i == 0 ? "Ausgang Team Blau" : "Ausgang Team Rot", kHeader);
            vector_rows("Pos", exit.center, grid_, -500.0f, 500.0f);
            changed |= Number(l, "Breite X", exit.size.x, grid_, 0.5f, 50.0f);
            changed |= Number(l, "Breite Z", exit.size.z, grid_, 0.5f, 50.0f);
            break;
        }
    }
    if (changed) MarkChanged();

    if (selection_.kind != Kind::None) {
        l.y += 6.0f;
        const int32 pressed = ButtonRow(l, {"Kopie", "Drehen 90", "Loeschen"});
        if (pressed == 0) DuplicateSelection();
        if (pressed == 1) RotateSelection();
        if (pressed == 2) DeleteSelection();
    }

    if (!problems_.empty()) {
        Header(l, "PRUEFUNG");
        for (size_t p = 0; p < problems_.size() && p < 12; ++p) {
            String text = problems_[p];
            // Wrap long lines
            while (!text.empty()) {
                size_t cut = text.size();
                while (cut > 10 && l.ui->GetTextWidth(text.substr(0, cut), kFont - 2.0f) > l.width) {
                    const size_t space = text.rfind(' ', cut - 1);
                    cut = space == String::npos || space < 10 ? cut - 1 : space;
                }
                l.ui->DrawText(text.substr(0, cut), l.x, l.y + 2.0f, {1.0f, 0.55f, 0.45f, 1.0f}, kFont - 2.0f);
                l.y += kRowHeight - 6.0f;
                text = cut < text.size() ? text.substr(cut + (text[cut] == ' ' ? 1 : 0)) : String();
            }
        }
    }
}

} // namespace Shift

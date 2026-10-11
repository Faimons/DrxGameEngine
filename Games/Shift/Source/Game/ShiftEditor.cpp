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
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iterator>
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
constexpr float32 kToolbar = 38.0f;
constexpr float32 kStatusBar = 28.0f;
constexpr float32 kTitle = 26.0f;
constexpr float32 kRowHeight = 24.0f;
constexpr float32 kFont = 15.0f;
constexpr float32 kPad = 10.0f;
constexpr size_t kMaxUndo = 100;
constexpr float32 kWallHeight = 4.0f;
constexpr float32 kExitHeight = 0.3f;
constexpr float32 kAutosaveSeconds = 120.0f;

const Vector4 kPanelColor = {0.07f, 0.08f, 0.1f, 0.94f};
const Vector4 kTitleColor = {0.12f, 0.14f, 0.19f, 1.0f};
const Vector4 kTitleActive = {0.16f, 0.24f, 0.4f, 1.0f};
const Vector4 kToolbarColor = {0.05f, 0.06f, 0.08f, 0.97f};
const Vector4 kButtonColor = {0.18f, 0.2f, 0.25f, 1.0f};
const Vector4 kButtonHover = {0.27f, 0.31f, 0.4f, 1.0f};
const Vector4 kButtonHighlight = {0.2f, 0.42f, 0.75f, 1.0f};
const Vector4 kFieldColor = {0.12f, 0.13f, 0.16f, 1.0f};
const Vector4 kText = {0.95f, 0.96f, 1.0f, 1.0f};
const Vector4 kDim = {0.6f, 0.65f, 0.75f, 1.0f};
const Vector4 kHeader = {1.0f, 0.82f, 0.35f, 1.0f};
const Vector4 kSelect = {1.0f, 0.85f, 0.2f, 1.0f};
const Vector4 kHover = {1.0f, 1.0f, 1.0f, 0.7f};

const Array<String> kSurfaceNames = {"Boden", "Wand", "Decke", "Deckung", "Stufe", "Startplatte Blau", "Startplatte Rot", "Beweglich"};
const Array<String> kGridNames = {"0,25 m", "0,5 m", "1 m", "2 m"};
const float32 kGridSizes[] = {0.25f, 0.5f, 1.0f, 2.0f};
const Array<String> kSpeedNames = {"1x", "3x", "10x"};
const int32 kSpeeds[] = {1, 3, 10};
const char* const kLayerNames[] = {"Boeden", "Waende", "Deckung", "Stufen", "Decke", "Bewegliche Waende", "Deko", "Spielobjekte"};

struct PaletteColor {
    const char* name;
    Vector4 color;
};
const PaletteColor kPalette[] = {
    {"Standard", {0.0f, 0.0f, 0.0f, 0.0f}},   {"Weiss", {0.92f, 0.92f, 0.94f, 1.0f}},   {"Hellgrau", {0.72f, 0.73f, 0.76f, 1.0f}},
    {"Grau", {0.45f, 0.46f, 0.5f, 1.0f}},     {"Dunkel", {0.17f, 0.18f, 0.21f, 1.0f}},  {"Beton", {0.6f, 0.58f, 0.54f, 1.0f}},
    {"Holz", {0.55f, 0.38f, 0.22f, 1.0f}},    {"Rot", {0.8f, 0.16f, 0.13f, 1.0f}},      {"Orange", {0.95f, 0.5f, 0.12f, 1.0f}},
    {"Gelb", {0.95f, 0.84f, 0.2f, 1.0f}},     {"Gruen", {0.25f, 0.68f, 0.3f, 1.0f}},    {"Tuerkis", {0.15f, 0.68f, 0.72f, 1.0f}},
    {"Blau", {0.2f, 0.4f, 0.9f, 1.0f}},       {"Lila", {0.55f, 0.3f, 0.8f, 1.0f}},
};

float32 Snap(float32 value, float32 grid) { return std::round(value / grid) * grid; }

bool RayBox(const Vector3& origin, const Vector3& direction, const Vector3& min, const Vector3& max, float32& t) {
    float32 t0 = 0.0f;
    float32 t1 = std::numeric_limits<float32>::max();
    const float32 o[3] = {origin.x, origin.y, origin.z};
    const float32 d[3] = {direction.x, direction.y, direction.z};
    const float32 lo[3] = {min.x, min.y, min.z};
    const float32 hi[3] = {max.x, max.y, max.z};
    for (int32 axis = 0; axis < 3; ++axis) {
        if (std::abs(d[axis]) < 1.0e-8f) {
            if (o[axis] < lo[axis] || o[axis] > hi[axis]) return false;
            continue;
        }
        float32 a = (lo[axis] - o[axis]) / d[axis];
        float32 b = (hi[axis] - o[axis]) / d[axis];
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
    return RayBox(inverse.Rotate(origin - center), inverse.Rotate(direction), size * -0.5f, size * 0.5f, t);
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
    while (!out.empty() && out[0] == '_') out.erase(out.begin());
    return out.empty() ? String("neue_karte") : out;
}

void PopUtf8(String& text) {
    while (!text.empty()) {
        const unsigned char c = static_cast<unsigned char>(text.back());
        text.pop_back();
        if ((c & 0xC0) != 0x80) break;
    }
}

uint32 HashLabel(const String& label, uint32 salt) {
    uint32 h = 2166136261u ^ (salt * 16777619u);
    for (const char c : label) h = (h ^ static_cast<uint8>(c)) * 16777619u;
    return h | 1u;
}

bool SameColor(const Vector4& a, const Vector4& b) {
    if (a.w <= 0.0f || b.w <= 0.0f) return a.w <= 0.0f && b.w <= 0.0f;
    return std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z) < 0.02f;
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
            std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
            if (ext != ".glb" && ext != ".gltf" && ext != ".fbx" && ext != ".obj") continue;
            const String relative = std::filesystem::relative(entry.path(), asset_dir_, ec).generic_string();
            if (!ec) prop_files_.push_back(relative);
        }
    }
    std::sort(prop_files_.begin(), prop_files_.end());
}

void MapEditor::NewLevel() {
    PushUndo("Neue Karte");
    Level level;
    level.name = "Neue Karte";
    constexpr float32 half = 15.0f;
    level.AddBlock(-half - 0.5f, half + 0.5f, -half - 0.5f, half + 0.5f, Surface::Floor, -0.5f, 0.0f);
    level.AddBlock(-half - 0.5f, half + 0.5f, -half - 0.5f, half + 0.5f, Surface::Ceiling, kWallHeight, kWallHeight + 0.5f);
    level.AddBlock(-half - 0.5f, half + 0.5f, -half - 0.5f, -half, Surface::Wall);
    level.AddBlock(-half - 0.5f, half + 0.5f, half, half + 0.5f, Surface::Wall);
    level.AddBlock(-half - 0.5f, -half, -half, half, Surface::Wall);
    level.AddBlock(half, half + 0.5f, -half, half, Surface::Wall);
    level.spawns[0] = {{-12.0f, 0.0f, 0.0f}, 90.0f};
    level.spawns[1] = {{12.0f, 0.0f, 0.0f}, 270.0f};
    level.has_core = true;
    level.core_home = {0.0f, 0.0f, 0.0f};
    level.exits[0] = {{-12.0f, 1.0f, 11.0f}, {3.0f, 2.0f, 3.0f}};
    level.exits[1] = {{12.0f, 1.0f, -11.0f}, {3.0f, 2.0f, 3.0f}};
    level_ = level;
    file_name_ = "neue_karte";
    selection_.clear();
    problems_.clear();
    checked_ = false;
    preview_state_ = ShiftPlan::InitialState();
    preview_pattern_ = 0;
    dirty_ = true;
    SetStatus("Neue Karte angelegt (30 x 30 m)");
}

void MapEditor::LoadLevelByName(const String& name) {
    level_ = BuildLevel(name, map_dir_);
    file_name_ = SanitizeFileName(name);
    selection_.clear();
    problems_.clear();
    checked_ = false;
    preview_state_ = ShiftPlan::InitialState();
    preview_pattern_ = 0;
    playing_ = false;
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
    confirm_exit_ = false;
    RefreshFileLists();
    for (size_t i = 0; i < map_names_.size(); ++i) {
        if (map_names_[i] == file_name_) map_index_ = static_cast<int32>(i);
    }
    SetStatus(fmt::format("Gespeichert: Maps/{}.json", file_name_));
}

void MapEditor::RunCheck() {
    problems_ = CheckLevelRoutes(*config_, level_);
    checked_ = true;
    OpenPanel(kPanelCheck);
    if (problems_.empty()) SetStatus("Pruefung bestanden: alle Wege in allen Wandzustaenden frei");
    else SetStatus(fmt::format("Pruefung: {} Problem(e) gefunden", problems_.size()), true);
}

void MapEditor::SetStatus(const String& text, bool error) {
    status_ = text;
    status_error_ = error;
    status_time_ = 10.0f;
    if (error) DRX_LOG_WARNING("Editor: {}", text);
}

// ============================================================================
// Undo
// ============================================================================

void MapEditor::PushUndo(const String& action) {
    undo_.push_back({level_, action});
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    redo_.clear();
}

void MapEditor::Undo() {
    if (undo_.empty()) {
        SetStatus("Nichts zum Rueckgaengigmachen");
        return;
    }
    UndoEntry entry = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back({level_, entry.action});
    level_ = std::move(entry.level);
    selection_.clear();
    dirty_ = true;
    checked_ = false;
    SetStatus("Rueckgaengig: " + entry.action);
}

void MapEditor::Redo() {
    if (redo_.empty()) {
        SetStatus("Nichts zum Wiederholen");
        return;
    }
    UndoEntry entry = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back({level_, entry.action});
    level_ = std::move(entry.level);
    selection_.clear();
    dirty_ = true;
    checked_ = false;
    SetStatus("Wiederholt: " + entry.action);
}

void MapEditor::MarkChanged() {
    dirty_ = true;
    confirm_exit_ = false;
    checked_ = false;
}

// ============================================================================
// Selection
// ============================================================================

MapEditor::Selection MapEditor::Primary() const { return selection_.empty() ? Selection{} : selection_.back(); }

bool MapEditor::IsSelected(const Selection& s) const {
    return std::find(selection_.begin(), selection_.end(), s) != selection_.end();
}

void MapEditor::Select(const Selection& s, bool additive) {
    if (!additive) selection_.clear();
    const auto it = std::find(selection_.begin(), selection_.end(), s);
    if (it != selection_.end()) {
        if (additive) selection_.erase(it);
        return;
    }
    if (s.kind != Kind::None) selection_.push_back(s);
}

void MapEditor::SelectAll() {
    selection_.clear();
    for (size_t i = 0; i < level_.boxes.size(); ++i) {
        if (BoxVisible(level_.boxes[i]) && IsDraggable({Kind::Box, static_cast<int32>(i)})) selection_.push_back({Kind::Box, static_cast<int32>(i)});
    }
    if (layers_[kLayerMoving]) {
        for (size_t i = 0; i < level_.segments.size(); ++i) selection_.push_back({Kind::Segment, static_cast<int32>(i)});
    }
    if (layers_[kLayerProps]) {
        for (size_t i = 0; i < level_.props.size(); ++i) selection_.push_back({Kind::Prop, static_cast<int32>(i)});
    }
    SetStatus(fmt::format("{} Objekte gewaehlt", selection_.size()));
}

String MapEditor::Describe(const Selection& s) const {
    const size_t i = static_cast<size_t>(std::max(0, s.index));
    switch (s.kind) {
        case Kind::Box: {
            const LevelBox& b = level_.boxes[i];
            return fmt::format("{} {} ({:.2g} x {:.2g} x {:.2g} m)", kSurfaceNames[static_cast<size_t>(b.surface)], i + 1, b.size.x, b.size.y,
                               b.size.z);
        }
        case Kind::Segment: return fmt::format("{} [{}]", level_.segments[i].name, SegmentGroupName(level_.segments[i].group));
        case Kind::Prop: return fmt::format("Deko: {}", std::filesystem::path(level_.props[i].model).filename().string());
        case Kind::Spawn: return i == 0 ? "Start Team Blau" : "Start Team Rot";
        case Kind::Core: return "Kern";
        case Kind::Exit: return i == 0 ? "Ausgang Team Blau" : "Ausgang Team Rot";
        case Kind::None: break;
    }
    return "";
}

// ============================================================================
// Editing
// ============================================================================

Math::Vector3 MapEditor::PlacementPoint() const {
    const Ray ray = MouseRay(screen_w_ * 0.5f, screen_h_ * 0.5f);
    Vector3 point = cam_pos_ + ray.direction * 8.0f;
    if (ray.direction.y < -0.05f) {
        const float32 t = -cam_pos_.y / ray.direction.y;
        if (t > 0.0f && t < 300.0f) point = cam_pos_ + ray.direction * t;
    }
    return Vector3(SnapValue(point.x), 0.0f, SnapValue(point.z));
}

float32 MapEditor::SnapValue(float32 value) const { return snap_ ? Snap(value, grid_) : value; }

float32 MapEditor::SnapCorner(float32 center, float32 size) const {
    return snap_ ? Snap(center - size * 0.5f, grid_) + size * 0.5f : center;
}

void MapEditor::AddBox(const Vector3& size, Surface surface, float32 bottom, bool select) {
    if (select) PushUndo(fmt::format("{} gesetzt", kSurfaceNames[static_cast<size_t>(surface)]));
    const Vector3 at = PlacementPoint();
    LevelBox box;
    box.size = size;
    box.center = Vector3(SnapCorner(at.x, size.x), bottom + size.y * 0.5f, SnapCorner(at.z, size.z));
    box.surface = surface;
    box.casts_shadow = surface != Surface::Ceiling;
    level_.boxes.push_back(box);
    if (select) Select({Kind::Box, static_cast<int32>(level_.boxes.size()) - 1}, false);
    MarkChanged();
}

void MapEditor::AddStairs() {
    PushUndo("Treppe gesetzt");
    const Vector3 at = PlacementPoint();
    selection_.clear();
    for (int32 i = 0; i < 4; ++i) {
        LevelBox step;
        const float32 height = 0.25f * static_cast<float32>(i + 1);
        step.size = Vector3(2.0f, height, 0.5f);
        step.center = Vector3(at.x, height * 0.5f, at.z - 0.5f * static_cast<float32>(i));
        step.surface = Surface::Step;
        level_.boxes.push_back(step);
        selection_.push_back({Kind::Box, static_cast<int32>(level_.boxes.size()) - 1});
    }
    MarkChanged();
}

void MapEditor::AddRoom() {
    PushUndo("Raum gesetzt");
    const Vector3 at = PlacementPoint();
    constexpr float32 half = 5.0f;
    constexpr float32 t = 0.5f;
    constexpr float32 door = 1.5f;
    selection_.clear();
    const auto add = [&](float32 x0, float32 x1, float32 z0, float32 z1) {
        LevelBox wall;
        wall.size = Vector3(x1 - x0, kWallHeight, z1 - z0);
        wall.center = Vector3(at.x + (x0 + x1) * 0.5f, kWallHeight * 0.5f, at.z + (z0 + z1) * 0.5f);
        wall.surface = Surface::Wall;
        level_.boxes.push_back(wall);
        selection_.push_back({Kind::Box, static_cast<int32>(level_.boxes.size()) - 1});
    };
    add(-half - t, half + t, -half - t, -half);   // north
    add(-half - t, -door, half, half + t);        // south with a 3 m door
    add(door, half + t, half, half + t);
    add(-half - t, -half, -half, half);           // west
    add(half, half + t, -half, half);             // east
    MarkChanged();
}

void MapEditor::AddSegment(const Vector3& size, const Vector3& move, float32 rotate, SegmentGroup group) {
    PushUndo("Bewegliche Wand gesetzt");
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
    Select({Kind::Segment, static_cast<int32>(level_.segments.size()) - 1}, false);
    MarkChanged();
}

void MapEditor::AddRotatingRoom() {
    PushUndo("Drehraum gesetzt");
    const size_t first = level_.segments.size();
    AddRotatingRoomSegments(level_, PlacementPoint(), fmt::format("Drehraum {}", first / 6 + 1));
    selection_.clear();
    for (size_t i = first; i < level_.segments.size(); ++i) selection_.push_back({Kind::Segment, static_cast<int32>(i)});
    SetStatus("Drehraum: dreht sich mit dem Muster 'Drehraum' um 90 Grad (Ecken brauchen 7 m Platz)");
    MarkChanged();
}

void MapEditor::AddProp(const String& model) {
    PushUndo("Deko gesetzt");
    LevelProp prop;
    prop.model = model;
    prop.position = PlacementPoint();
    prop.scale = prop_scale_;
    level_.props.push_back(prop);
    Select({Kind::Prop, static_cast<int32>(level_.props.size()) - 1}, false);
    MarkChanged();
}

void MapEditor::DeleteSelection() {
    if (selection_.empty()) return;
    PushUndo(fmt::format("{} Objekt(e) geloescht", selection_.size()));
    Array<int32> boxes, segments, props;
    bool blocked = false;
    for (const Selection& s : selection_) {
        if (s.kind == Kind::Box) boxes.push_back(s.index);
        else if (s.kind == Kind::Segment) segments.push_back(s.index);
        else if (s.kind == Kind::Prop) props.push_back(s.index);
        else if (s.kind == Kind::Core) level_.has_core = false;
        else blocked = true;
    }
    const auto erase = [](auto& list, Array<int32>& indices) {
        std::sort(indices.begin(), indices.end(), std::greater<int32>());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        for (const int32 i : indices) list.erase(list.begin() + i);
    };
    erase(level_.boxes, boxes);
    erase(level_.segments, segments);
    erase(level_.props, props);
    selection_.clear();
    if (blocked) SetStatus("Starts und Ausgaenge koennen nur verschoben werden", true);
    MarkChanged();
}

void MapEditor::CopySelection() {
    clipboard_ = {};
    Vector3 center = Vector3::Zero;
    int32 count = 0;
    for (const Selection& s : selection_) {
        Vector3 p;
        if ((s.kind == Kind::Box || s.kind == Kind::Segment || s.kind == Kind::Prop) && GetPosition(s, p)) {
            center = center + p;
            ++count;
        }
    }
    if (count == 0) return;
    center = center / static_cast<float32>(count);
    center.y = 0.0f;
    for (const Selection& s : selection_) {
        const size_t i = static_cast<size_t>(s.index);
        if (s.kind == Kind::Box) {
            LevelBox b = level_.boxes[i];
            b.center = b.center - center;
            clipboard_.boxes.push_back(b);
        } else if (s.kind == Kind::Segment) {
            MovingSegmentDef d = level_.segments[i];
            d.center = d.center - center;
            d.pivot = d.pivot - center;
            clipboard_.segments.push_back(d);
        } else if (s.kind == Kind::Prop) {
            LevelProp p = level_.props[i];
            p.position = p.position - center;
            clipboard_.props.push_back(p);
        }
    }
    SetStatus(fmt::format("{} Objekt(e) kopiert - Strg+V setzt sie in die Bildmitte", count));
}

void MapEditor::PasteClipboard() {
    if (clipboard_.Empty()) return;
    PushUndo("Eingefuegt");
    const Vector3 at = PlacementPoint();
    selection_.clear();
    for (LevelBox b : clipboard_.boxes) {
        b.center = b.center + at;
        level_.boxes.push_back(b);
        selection_.push_back({Kind::Box, static_cast<int32>(level_.boxes.size()) - 1});
    }
    for (MovingSegmentDef d : clipboard_.segments) {
        d.center = d.center + at;
        d.pivot = d.pivot + at;
        level_.segments.push_back(d);
        selection_.push_back({Kind::Segment, static_cast<int32>(level_.segments.size()) - 1});
    }
    for (LevelProp p : clipboard_.props) {
        p.position = p.position + at;
        level_.props.push_back(p);
        selection_.push_back({Kind::Prop, static_cast<int32>(level_.props.size()) - 1});
    }
    MarkChanged();
}

void MapEditor::DuplicateSelection() {
    if (selection_.empty()) return;
    const Clipboard saved = clipboard_;
    CopySelection();
    Vector3 center = Vector3::Zero;
    int32 count = 0;
    for (const Selection& s : selection_) {
        Vector3 p;
        if ((s.kind == Kind::Box || s.kind == Kind::Segment || s.kind == Kind::Prop) && GetPosition(s, p)) {
            center = center + p;
            ++count;
        }
    }
    if (count == 0) {
        clipboard_ = saved;
        return;
    }
    center = center / static_cast<float32>(count);
    PushUndo("Kopie erstellt");
    const Vector3 offset(center.x + std::max(grid_, 1.0f) * 2.0f, 0.0f, center.z);
    selection_.clear();
    for (LevelBox b : clipboard_.boxes) {
        b.center = b.center + offset;
        level_.boxes.push_back(b);
        selection_.push_back({Kind::Box, static_cast<int32>(level_.boxes.size()) - 1});
    }
    for (MovingSegmentDef d : clipboard_.segments) {
        d.center = d.center + offset;
        d.pivot = d.pivot + offset;
        d.name += " Kopie";
        level_.segments.push_back(d);
        selection_.push_back({Kind::Segment, static_cast<int32>(level_.segments.size()) - 1});
    }
    for (LevelProp p : clipboard_.props) {
        p.position = p.position + offset;
        level_.props.push_back(p);
        selection_.push_back({Kind::Prop, static_cast<int32>(level_.props.size()) - 1});
    }
    clipboard_ = saved;
    MarkChanged();
}

void MapEditor::RotateSelection() {
    if (selection_.empty()) return;
    PushUndo("Gedreht");
    // Turn the whole selection 90 degrees around its centre
    Vector3 center = Vector3::Zero;
    int32 count = 0;
    for (const Selection& s : selection_) {
        Vector3 p;
        if (GetPosition(s, p)) {
            center = center + p;
            ++count;
        }
    }
    center = count > 0 ? center / static_cast<float32>(count) : Vector3::Zero;
    center = Vector3(SnapValue(center.x), 0.0f, SnapValue(center.z));
    const auto turn = [](const Vector3& v) { return Vector3(-v.z, v.y, v.x); };
    const auto around = [&](const Vector3& p) { return center + turn(p - center); };
    const bool single = selection_.size() == 1;
    for (const Selection& s : selection_) {
        const size_t i = static_cast<size_t>(s.index);
        switch (s.kind) {
            case Kind::Box: {
                LevelBox& box = level_.boxes[i];
                std::swap(box.size.x, box.size.z);
                if (!single) box.center = around(box.center);
                break;
            }
            case Kind::Segment: {
                MovingSegmentDef& def = level_.segments[i];
                const Vector3 old_center = def.center;
                std::swap(def.size.x, def.size.z);
                def.move = turn(def.move);
                if (!single) def.center = around(def.center);
                def.pivot = def.center + turn(def.pivot - old_center);
                break;
            }
            case Kind::Exit: std::swap(level_.exits[i].size.x, level_.exits[i].size.z); break;
            case Kind::Prop:
                level_.props[i].yaw = std::fmod(level_.props[i].yaw + 90.0f, 360.0f);
                if (!single) level_.props[i].position = around(level_.props[i].position);
                break;
            case Kind::Spawn: level_.spawns[i].yaw = std::fmod(level_.spawns[i].yaw + 90.0f, 360.0f); break;
            default: break;
        }
    }
    MarkChanged();
}

void MapEditor::MoveSelection(const Vector3& delta) {
    for (const Selection& s : selection_) {
        const size_t i = static_cast<size_t>(std::max(0, s.index));
        switch (s.kind) {
            case Kind::Box: level_.boxes[i].center = level_.boxes[i].center + delta; break;
            case Kind::Segment:
                level_.segments[i].center = level_.segments[i].center + delta;
                level_.segments[i].pivot = level_.segments[i].pivot + delta;
                break;
            case Kind::Prop: level_.props[i].position = level_.props[i].position + delta; break;
            case Kind::Spawn: level_.spawns[i].position = level_.spawns[i].position + delta; break;
            case Kind::Core: level_.core_home = level_.core_home + delta; break;
            case Kind::Exit: level_.exits[i].center = level_.exits[i].center + delta; break;
            case Kind::None: break;
        }
    }
    MarkChanged();
}

bool MapEditor::GetPosition(const Selection& s, Vector3& out) const {
    const size_t i = static_cast<size_t>(std::max(0, s.index));
    switch (s.kind) {
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

Math::Vector3 MapEditor::SizeOf(const Selection& s) const {
    const size_t i = static_cast<size_t>(std::max(0, s.index));
    switch (s.kind) {
        case Kind::Box: return level_.boxes[i].size;
        case Kind::Segment: return level_.segments[i].size;
        case Kind::Exit: return level_.exits[i].size;
        default: return Vector3::Zero;
    }
}

bool MapEditor::IsDraggable(const Selection& selection) const {
    // Floors and ceilings span the whole map: moved only by numbers, never by accident
    if (selection.kind == Kind::Box) {
        const LevelBox& box = level_.boxes[static_cast<size_t>(selection.index)];
        const bool huge = box.size.x * box.size.z > 400.0f;
        return !((box.surface == Surface::Floor || box.surface == Surface::Ceiling) && huge);
    }
    return selection.kind != Kind::None;
}

MapEditor::Layer MapEditor::BoxLayer(const LevelBox& box) const {
    switch (box.surface) {
        case Surface::Floor:
        case Surface::SpawnA:
        case Surface::SpawnB: return kLayerFloors;
        case Surface::Wall:
        case Surface::Moving: return kLayerWalls;
        case Surface::Cover: return kLayerCover;
        case Surface::Step: return kLayerSteps;
        case Surface::Ceiling: return kLayerCeiling;
    }
    return kLayerWalls;
}

bool MapEditor::BoxVisible(const LevelBox& box) const { return layers_[BoxLayer(box)]; }

void MapEditor::ToggleTopView() {
    if (!top_view_) {
        saved_cam_pos_ = cam_pos_;
        saved_cam_yaw_ = cam_yaw_;
        saved_cam_pitch_ = cam_pitch_;
        Vector3 lo(std::numeric_limits<float32>::max()), hi(std::numeric_limits<float32>::lowest());
        for (const LevelBox& box : level_.boxes) {
            lo = Vector3::Min(lo, box.center - box.size * 0.5f);
            hi = Vector3::Max(hi, box.center + box.size * 0.5f);
        }
        if (lo.x > hi.x) lo = hi = Vector3::Zero;
        const Vector3 center = (lo + hi) * 0.5f;
        const float32 extent = std::max({hi.x - lo.x, hi.z - lo.z, 10.0f});
        cam_pos_ = Vector3(center.x, extent * 0.5f / std::tan(fov_ * kDegToRad * 0.5f) + 6.0f, center.z + 0.01f);
        cam_yaw_ = 0.0f;
        cam_pitch_ = -89.9f;
        top_view_ = true;
        SetStatus("Draufsicht (T schaltet zurueck)");
    } else {
        cam_pos_ = saved_cam_pos_;
        cam_yaw_ = saved_cam_yaw_;
        cam_pitch_ = saved_cam_pitch_;
        top_view_ = false;
    }
}

void MapEditor::FocusSelection() {
    Vector3 target;
    if (!GetPosition(Primary(), target)) return;
    const float32 distance = std::max(7.0f, SizeOf(Primary()).Length() * 1.3f);
    cam_pos_ = target - LookDirection(cam_yaw_, cam_pitch_) * distance;
    top_view_ = false;
}

// ============================================================================
// Camera, picking and viewport input
// ============================================================================

Renderer::CameraParams MapEditor::GetCamera(float32 aspect) const {
    return Renderer::CameraParams::LookAt(cam_pos_, cam_pos_ + LookDirection(cam_yaw_, cam_pitch_), fov_ * kDegToRad, aspect,
                                          0.05f, 600.0f);
}

bool MapEditor::WantsMouseLook(const Input::InputSystem& input) const {
    return open_ && input.IsMouseButtonDown(Input::MouseButton::Right) && !text_focus_;
}

MapEditor::Ray MapEditor::MouseRay(float32 mx, float32 my) const {
    const Vector3 forward = LookDirection(cam_yaw_, cam_pitch_);
    const Vector3 right = Vector3::Cross(forward, Vector3::Up).Normalized();
    const Vector3 up = Vector3::Cross(right, forward);
    const float32 tan_half = std::tan(fov_ * kDegToRad * 0.5f);
    const float32 aspect = screen_w_ / screen_h_;
    const float32 nx = (mx / screen_w_) * 2.0f - 1.0f;
    const float32 ny = 1.0f - (my / screen_h_) * 2.0f;
    return {cam_pos_, (forward + right * (nx * tan_half * aspect) + up * (ny * tan_half)).Normalized()};
}

bool MapEditor::Project(const Vector3& point, Vector2& out) const {
    const Vector3 forward = LookDirection(cam_yaw_, cam_pitch_);
    const Vector3 right = Vector3::Cross(forward, Vector3::Up).Normalized();
    const Vector3 up = Vector3::Cross(right, forward);
    const Vector3 d = point - cam_pos_;
    const float32 z = Vector3::Dot(d, forward);
    if (z <= 0.05f) return false;
    const float32 tan_half = std::tan(fov_ * kDegToRad * 0.5f);
    const float32 nx = Vector3::Dot(d, right) / (z * tan_half * (screen_w_ / screen_h_));
    const float32 ny = Vector3::Dot(d, up) / (z * tan_half);
    out = Vector2((nx + 1.0f) * 0.5f * screen_w_, (1.0f - ny) * 0.5f * screen_h_);
    return true;
}

bool MapEditor::OverUI(float32 mx, float32 my) const {
    if (my < kToolbar || my > screen_h_ - kStatusBar) return true;
    return PanelAt(mx, my) >= 0;
}

SegmentPose MapEditor::PreviewPose(const MovingSegmentDef& def) const {
    if (playing_ && config_) return def.PoseAt(SegmentAmount(config_->shifts, play_plan_, def, play_time_));
    return def.PoseAt((preview_state_ & GroupBit(def.group)) ? 1.0f : 0.0f);
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
        if (!BoxVisible(box)) continue;
        if (RayBox(ray.origin, ray.direction, box.center - box.size * 0.5f, box.center + box.size * 0.5f, t)) consider(t, Kind::Box, i);
    }
    if (layers_[kLayerMoving]) {
        for (size_t i = 0; i < level_.segments.size(); ++i) {
            const SegmentPose pose = PreviewPose(level_.segments[i]);
            if (RayOrientedBox(ray.origin, ray.direction, pose.center, pose.Rotation(), level_.segments[i].size, t)) {
                consider(t, Kind::Segment, i);
            }
        }
    }
    if (layers_[kLayerProps]) {
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
    }
    if (layers_[kLayerMarkers]) {
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
    if (playing_) {
        play_time_ += dt * static_cast<float32>(play_speed_);
        GroupMask groups = 0;
        for (const MovingSegmentDef& def : level_.segments) groups |= GroupBit(def.group);
        play_plan_ = ShiftPlan(level_.shift_mode, 7u, groups, config_->shifts.all_open_max_per);
    }

    // Autosave (never over the real file)
    autosave_timer_ += dt;
    if (dirty_ && autosave_timer_ > kAutosaveSeconds) {
        autosave_timer_ = 0.0f;
        String error;
        if (SaveLevel(level_, (std::filesystem::path(map_dir_) / "_autosave.json").string(), &error)) {
            SetStatus("Automatisch gesichert (Maps/_autosave.json)");
        }
    }

    // Fly camera
    if (mouse_look) {
        const Vector2 delta = input.GetMouseDelta();
        cam_yaw_ = std::fmod(cam_yaw_ + delta.x * 0.12f, 360.0f);
        cam_pitch_ = std::clamp(cam_pitch_ - delta.y * 0.12f, -89.9f, 89.0f);
        const float32 speed = (input.IsKeyDown(KeyCode::LeftShift) ? cam_speed_ * 3.0f : cam_speed_) * dt;
        const Vector3 forward = LookDirection(cam_yaw_, cam_pitch_);
        const Vector3 right = FlatRight(cam_yaw_);
        if (input.IsKeyDown(KeyCode::W)) cam_pos_ = cam_pos_ + forward * speed;
        if (input.IsKeyDown(KeyCode::S)) cam_pos_ = cam_pos_ - forward * speed;
        if (input.IsKeyDown(KeyCode::D)) cam_pos_ = cam_pos_ + right * speed;
        if (input.IsKeyDown(KeyCode::A)) cam_pos_ = cam_pos_ - right * speed;
        if (input.IsKeyDown(KeyCode::E)) cam_pos_.y += speed;
        if (input.IsKeyDown(KeyCode::Q)) cam_pos_.y -= speed;
        top_view_ = false;
        return;
    }
    if (text_focus_) return;

    const Vector2 mouse = input.GetMousePosition();
    const bool over_ui = OverUI(mouse.x, mouse.y) || dragged_panel_ >= 0;

    // Wheel in the viewport moves forward / back
    const float32 wheel = input.GetMouseScroll().y;
    if (!over_ui && wheel != 0.0f) cam_pos_ = cam_pos_ + LookDirection(cam_yaw_, cam_pitch_) * (wheel * 2.0f);

    // Keyboard editing
    const bool ctrl = input.IsKeyDown(KeyCode::LeftControl) || input.IsKeyDown(KeyCode::RightControl);
    const bool shift = input.IsKeyDown(KeyCode::LeftShift) || input.IsKeyDown(KeyCode::RightShift);
    if (ctrl && input.IsKeyPressed(KeyCode::Z)) Undo();
    if (ctrl && input.IsKeyPressed(KeyCode::Y)) Redo();
    if (ctrl && input.IsKeyPressed(KeyCode::S)) SaveCurrent();
    if (ctrl && input.IsKeyPressed(KeyCode::D)) DuplicateSelection();
    if (ctrl && input.IsKeyPressed(KeyCode::C)) CopySelection();
    if (ctrl && input.IsKeyPressed(KeyCode::V)) PasteClipboard();
    if (ctrl && input.IsKeyPressed(KeyCode::A)) SelectAll();
    if (!ctrl && input.IsKeyPressed(KeyCode::Delete)) DeleteSelection();
    if (!ctrl && input.IsKeyPressed(KeyCode::R)) RotateSelection();
    if (!ctrl && input.IsKeyPressed(KeyCode::T)) ToggleTopView();
    if (!ctrl && input.IsKeyPressed(KeyCode::F)) FocusSelection();
    if (input.IsKeyPressed(KeyCode::Escape)) selection_.clear();
    if (!ctrl && input.IsKeyPressed(KeyCode::G)) {
        size_t next = 0;
        for (size_t i = 0; i < 4; ++i) {
            if (std::abs(kGridSizes[i] - grid_) < 1.0e-4f) next = (i + 1) % 4;
        }
        grid_ = kGridSizes[next];
        SetStatus(fmt::format("Raster {}", kGridNames[next]));
    }
    Vector3 nudge = Vector3::Zero;
    if (input.IsKeyPressed(KeyCode::Left)) nudge.x -= grid_;
    if (input.IsKeyPressed(KeyCode::Right)) nudge.x += grid_;
    if (input.IsKeyPressed(KeyCode::Up)) nudge.z -= grid_;
    if (input.IsKeyPressed(KeyCode::Down)) nudge.z += grid_;
    if (input.IsKeyPressed(KeyCode::PageUp)) nudge.y += grid_;
    if (input.IsKeyPressed(KeyCode::PageDown)) nudge.y -= grid_;
    if (nudge.LengthSquared() > 0.0f && !selection_.empty()) {
        PushUndo("Verschoben");
        MoveSelection(nudge);
    }

    // Hover
    hover_ = {};
    Hit hit;
    const Ray ray = MouseRay(mouse.x, mouse.y);
    const bool hit_something = !over_ui && Pick(ray, hit);
    if (hit_something) hover_ = hit.selection;

    // Press: select, start dragging or a selection rectangle
    if (input.IsMouseButtonPressed(Input::MouseButton::Left) && !over_ui) {
        press_mouse_ = mouse;
        if (hit_something) {
            const bool already = IsSelected(hit.selection);
            if (shift) Select(hit.selection, true);
            else if (!already) Select(hit.selection, false);
            else {
                // Keep the group, make the clicked object primary
                selection_.erase(std::find(selection_.begin(), selection_.end(), hit.selection));
                selection_.push_back(hit.selection);
            }
            Vector3 position;
            dragging_ = !shift && IsSelected(hit.selection) && IsDraggable(hit.selection) && GetPosition(hit.selection, position);
            if (dragging_) {
                const Vector3 point = ray.origin + ray.direction * hit.distance;
                drag_plane_y_ = point.y;
                drag_offset_ = position - point;
                drag_start_pos_ = position;
                drag_moved_ = false;
                drag_vertical_ = input.IsKeyDown(KeyCode::LeftAlt);
            }
        } else {
            marquee_ = true;
            marquee_start_ = mouse;
            if (!shift) selection_.clear();
        }
    }

    // Drag the selection
    if (dragging_ && input.IsMouseButtonDown(Input::MouseButton::Left)) {
        if (drag_moved_ || (mouse - press_mouse_).Length() >= 4.0f) {
            Vector3 current;
            GetPosition(Primary(), current);
            Vector3 wanted = current;
            if (drag_vertical_) {
                wanted.y = drag_start_pos_.y + (press_mouse_.y - mouse.y) * 0.02f;
                wanted.y = SnapValue(wanted.y);
            } else {
                const Ray drag_ray = MouseRay(mouse.x, mouse.y);
                if (std::abs(drag_ray.direction.y) > 1.0e-4f) {
                    const float32 t = (drag_plane_y_ - drag_ray.origin.y) / drag_ray.direction.y;
                    if (t > 0.0f && t < 600.0f) {
                        const Vector3 p = drag_ray.origin + drag_ray.direction * t + drag_offset_;
                        const Vector3 size = SizeOf(Primary());
                        wanted.x = size.x > 0.0f ? SnapCorner(p.x, size.x) : SnapValue(p.x);
                        wanted.z = size.z > 0.0f ? SnapCorner(p.z, size.z) : SnapValue(p.z);
                    }
                }
            }
            const Vector3 delta = wanted - current;
            if (delta.LengthSquared() > 1.0e-8f) {
                if (!drag_moved_) PushUndo(selection_.size() > 1 ? "Gruppe verschoben" : "Verschoben");
                drag_moved_ = true;
                MoveSelection(delta);
            }
        }
    }

    // Selection rectangle
    if (marquee_ && !input.IsMouseButtonDown(Input::MouseButton::Left)) {
        marquee_ = false;
        const float32 x0 = std::min(marquee_start_.x, mouse.x), x1 = std::max(marquee_start_.x, mouse.x);
        const float32 y0 = std::min(marquee_start_.y, mouse.y), y1 = std::max(marquee_start_.y, mouse.y);
        if (x1 - x0 > 6.0f && y1 - y0 > 6.0f) {
            const auto inside = [&](const Vector3& p) {
                Vector2 s;
                return Project(p, s) && s.x >= x0 && s.x <= x1 && s.y >= y0 && s.y <= y1;
            };
            for (size_t i = 0; i < level_.boxes.size(); ++i) {
                const Selection s{Kind::Box, static_cast<int32>(i)};
                if (BoxVisible(level_.boxes[i]) && IsDraggable(s) && inside(level_.boxes[i].center) && !IsSelected(s)) selection_.push_back(s);
            }
            if (layers_[kLayerMoving]) {
                for (size_t i = 0; i < level_.segments.size(); ++i) {
                    const Selection s{Kind::Segment, static_cast<int32>(i)};
                    if (inside(level_.segments[i].center) && !IsSelected(s)) selection_.push_back(s);
                }
            }
            if (layers_[kLayerProps]) {
                for (size_t i = 0; i < level_.props.size(); ++i) {
                    const Selection s{Kind::Prop, static_cast<int32>(i)};
                    if (inside(level_.props[i].position) && !IsSelected(s)) selection_.push_back(s);
                }
            }
            SetStatus(fmt::format("{} Objekt(e) gewaehlt", selection_.size()));
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

void MapEditor::DrawOutline(const Selection& s, const Vector4& color) {
    Debug::DebugRenderer& debug = Debug::DebugRenderer::Instance();
    const size_t i = static_cast<size_t>(std::max(0, s.index));
    const float32 h = config_ ? config_->player.height : 1.8f;
    switch (s.kind) {
        case Kind::Box: debug.DrawWireBox(level_.boxes[i].center, level_.boxes[i].size + Vector3(0.03f), {}, color); break;
        case Kind::Segment: {
            const SegmentPose pose = PreviewPose(level_.segments[i]);
            debug.DrawWireBox(pose.center, level_.segments[i].size + Vector3(0.03f), pose.Rotation(), color);
            break;
        }
        case Kind::Prop: {
            const auto it = prop_models_.find(level_.props[i].model);
            const LevelProp& prop = level_.props[i];
            if (it != prop_models_.end() && it->second.IsValid()) {
                const Renderer::AABB& b = it->second.bounds;
                debug.DrawWireBox(prop.position + Vector3(0.0f, b.GetCenter().y * prop.scale, 0.0f), (b.max - b.min) * prop.scale,
                                  Quaternion::FromAxisAngle(Vector3::Up, prop.yaw * kDegToRad), color);
            } else {
                debug.DrawWireBox(prop.position + Vector3(0, 0.25f, 0), Vector3(0.55f), {}, color);
            }
            break;
        }
        case Kind::Spawn: debug.DrawWireBox(level_.spawns[i].position + Vector3(0, h * 0.5f, 0), Vector3(0.7f, h + 0.05f, 0.7f), {}, color); break;
        case Kind::Core: debug.DrawWireSphere(level_.core_home + Vector3(0, 0.4f, 0), 0.45f, color); break;
        case Kind::Exit: {
            const ZoneBox& exit = level_.exits[i];
            debug.DrawWireBox(Vector3(exit.center.x, kExitHeight * 0.5f, exit.center.z), Vector3(exit.size.x, kExitHeight, exit.size.z), {}, color);
            break;
        }
        case Kind::None: break;
    }
}

void MapEditor::Render(Renderer::SceneRenderer& renderer) {
    Debug::DebugRenderer& debug = Debug::DebugRenderer::Instance();

    for (size_t i = 0; i < level_.boxes.size(); ++i) {
        const LevelBox& box = level_.boxes[i];
        if (!BoxVisible(box)) continue;
        Renderer::Material material;
        material.albedo = grid_texture_;
        material.color = LookColor(box.color, box.surface);
        material.cast_shadows = box.casts_shadow;
        if (box.glow > 0.0f) {
            material.emissive = Vector3(material.color.x, material.color.y, material.color.z);
            material.emissive_strength = box.glow;
        }
        if (!box.collision) material.color = Vector4(material.color.x * 0.7f, material.color.y * 0.7f, material.color.z * 0.7f, 1.0f);
        if (IsSelected({Kind::Box, static_cast<int32>(i)})) {
            material.emissive = {0.9f, 0.7f, 0.1f};
            material.emissive_strength = std::max(material.emissive_strength, 0.3f);
        }
        renderer.DrawMesh(BoxMesh(renderer, box.size), Matrix4::Translation(box.center), material);
    }

    if (layers_[kLayerMoving]) {
        for (size_t i = 0; i < level_.segments.size(); ++i) {
            const MovingSegmentDef& def = level_.segments[i];
            const SegmentPose pose = PreviewPose(def);
            Renderer::Material material;
            material.albedo = grid_texture_;
            material.color = LookColor(def.color, Surface::Moving);
            if (def.glow > 0.0f) {
                material.emissive = Vector3(material.color.x, material.color.y, material.color.z);
                material.emissive_strength = def.glow;
            }
            if (IsSelected({Kind::Segment, static_cast<int32>(i)})) {
                material.emissive = {0.9f, 0.7f, 0.1f};
                material.emissive_strength = std::max(material.emissive_strength, 0.3f);
                // Both end states and the pivot / path
                const SegmentPose a = def.PoseAt(0.0f);
                const SegmentPose b = def.PoseAt(1.0f);
                debug.DrawWireBox(a.center, def.size + Vector3(0.02f), a.Rotation(), {0.3f, 1.0f, 0.4f, 1.0f});
                debug.DrawWireBox(b.center, def.size + Vector3(0.02f), b.Rotation(), {1.0f, 0.35f, 0.3f, 1.0f});
                if (std::abs(def.rotate) > 0.01f) debug.DrawWireSphere(def.pivot, 0.25f, kSelect);
                else debug.DrawArrow(a.center, b.center, kSelect);
            }
            renderer.DrawMesh(BoxMesh(renderer, def.size), Matrix4::Translation(pose.center) * pose.Rotation().ToMatrix(), material);
        }
    }

    if (layers_[kLayerProps]) {
        for (size_t i = 0; i < level_.props.size(); ++i) {
            const LevelProp& prop = level_.props[i];
            const Matrix4 transform = Matrix4::Translation(prop.position) *
                                      Quaternion::FromAxisAngle(Vector3::Up, prop.yaw * kDegToRad).ToMatrix() * Matrix4::Scale(prop.scale);
            if (const Renderer::Model* model = PropModel(renderer, prop.model)) {
                Renderer::DrawModel(renderer, *model, transform);
            } else {
                Renderer::Material missing;
                missing.color = {1.0f, 0.0f, 1.0f, 1.0f};
                renderer.DrawMesh(BoxMesh(renderer, Vector3(0.5f)), Matrix4::Translation(prop.position + Vector3(0, 0.25f, 0)), missing);
            }
        }
    }

    if (layers_[kLayerMarkers]) {
        const float32 h = config_ ? config_->player.height : 1.8f;
        for (size_t i = 0; i < 2; ++i) {
            const Team team = i == 0 ? Team::A : Team::B;
            const SpawnPoint& spawn = level_.spawns[i];
            Renderer::Material body;
            body.color = TeamColor(team);
            body.emissive = Vector3(body.color.x, body.color.y, body.color.z);
            body.emissive_strength = IsSelected({Kind::Spawn, static_cast<int32>(i)}) ? 1.2f : 0.4f;
            renderer.DrawMesh(BoxMesh(renderer, Vector3(0.6f, h, 0.6f)), Matrix4::Translation(spawn.position + Vector3(0, h * 0.5f, 0)), body);
            debug.DrawArrow(spawn.position + Vector3(0, 1.0f, 0), spawn.position + Vector3(0, 1.0f, 0) + FlatForward(spawn.yaw) * 1.5f,
                            TeamColor(team));
            if (labels_) debug.DrawText(spawn.position + Vector3(0, h + 0.4f, 0), i == 0 ? "START BLAU" : "START ROT", TeamColor(team));

            const ZoneBox& exit = level_.exits[i];
            Renderer::Material plate;
            plate.color = TeamColor(team);
            plate.emissive = Vector3(plate.color.x, plate.color.y, plate.color.z);
            plate.emissive_strength = IsSelected({Kind::Exit, static_cast<int32>(i)}) ? 1.5f : 0.7f;
            plate.cast_shadows = false;
            renderer.DrawMesh(BoxMesh(renderer, Vector3(exit.size.x, 0.04f, exit.size.z)),
                              Matrix4::Translation(Vector3(exit.center.x, 0.02f, exit.center.z)), plate);
            if (labels_) debug.DrawText(Vector3(exit.center.x, 1.5f, exit.center.z), i == 0 ? "AUSGANG BLAU" : "AUSGANG ROT", TeamColor(team));
        }
        if (level_.has_core) {
            Renderer::Material glow;
            glow.color = {1.0f, 0.9f, 0.4f, 1.0f};
            glow.emissive = {1.0f, 0.85f, 0.3f};
            glow.emissive_strength = IsSelected({Kind::Core, 0}) ? 4.0f : 2.0f;
            glow.cast_shadows = false;
            renderer.DrawMesh(sphere_mesh_, Matrix4::Translation(level_.core_home + Vector3(0, 0.4f, 0)) * Matrix4::Scale(0.3f), glow);
            if (labels_) debug.DrawText(level_.core_home + Vector3(0, 1.3f, 0), "KERN", {1.0f, 0.9f, 0.4f, 1.0f});
        }
    }
    if (group_labels_ && layers_[kLayerMoving]) {
        for (const MovingSegmentDef& def : level_.segments) {
            const SegmentPose pose = PreviewPose(def);
            debug.DrawText(pose.center + Vector3(0.0f, def.size.y * 0.5f + 0.3f, 0.0f), SegmentGroupName(def.group), {0.75f, 0.85f, 1.0f, 0.9f});
        }
    }

    // Selection, hover, measurements, ground grid and placement marker
    for (const Selection& s : selection_) DrawOutline(s, kSelect);
    if (hover_.kind != Kind::None && !IsSelected(hover_)) DrawOutline(hover_, kHover);
    if (!selection_.empty()) {
        Vector3 p;
        const Vector3 size = SizeOf(Primary());
        if (GetPosition(Primary(), p) && size.LengthSquared() > 0.0f) {
            debug.DrawText(p + Vector3(0.0f, size.y * 0.5f + 0.6f, 0.0f), fmt::format("{:.2f} x {:.2f} x {:.2f} m", size.x, size.y, size.z), kSelect);
        }
    }
    if (show_grid_) {
        const Vector3 at = PlacementPoint();
        debug.DrawGrid(Vector3(Snap(at.x, 10.0f), 0.02f, Snap(at.z, 10.0f)), 40.0f, 40, {0.4f, 0.55f, 0.8f, 0.35f});
    }
    const Vector3 at = PlacementPoint();
    debug.DrawWireBox(at + Vector3(0, 0.01f, 0), Vector3(grid_, 0.02f, grid_), {}, {1.0f, 1.0f, 1.0f, 0.8f});
}

// ============================================================================
// Widgets
// ============================================================================

bool MapEditor::RowVisible(const Layout& l, float32 height) const { return l.y >= l.top - 1.0f && l.y + height <= l.bottom + 1.0f; }

bool MapEditor::Hovered(const Layout& l, float32 x, float32 y, float32 w, float32 h) const {
    return l.interactive && mouse_.x >= x && mouse_.x < x + w && mouse_.y >= y && mouse_.y < y + h && mouse_.y >= l.top && mouse_.y <= l.bottom;
}

void MapEditor::Header(Layout& l, const String& text) {
    l.y += 4.0f;
    if (RowVisible(l, kRowHeight)) {
        l.ui->DrawText(text, l.x, l.y + 3.0f, kHeader, kFont);
        l.ui->DrawRect({l.x, l.y + kRowHeight - 4.0f, l.width, 1.0f}, {1.0f, 0.82f, 0.35f, 0.3f});
    }
    l.y += kRowHeight;
}

void MapEditor::Label(Layout& l, const String& text, const Vector4& color) {
    if (RowVisible(l, kRowHeight - 4.0f)) l.ui->DrawText(text, l.x, l.y + 3.0f, color, kFont - 1.0f);
    l.y += kRowHeight - 4.0f;
}

bool MapEditor::Button(Layout& l, const String& label, float32 x, float32 width, bool highlight) {
    const float32 h = kRowHeight - 3.0f;
    if (!RowVisible(l, h)) return false;
    const bool hovered = Hovered(l, x, l.y, width, h);
    l.ui->DrawRect({x, l.y, width, h}, highlight ? kButtonHighlight : hovered ? kButtonHover : kButtonColor);
    String text = label;
    while (text.size() > 3 && l.ui->GetTextWidth(text, kFont - 1.0f) > width - 6.0f) text.pop_back();
    const float32 tw = l.ui->GetTextWidth(text, kFont - 1.0f);
    l.ui->DrawText(text, x + (width - tw) * 0.5f, l.y + 3.0f, kText, kFont - 1.0f);
    const bool pressed = hovered && mouse_clicked_;
    if (pressed) ui_consumed_click_ = true;
    return pressed;
}

bool MapEditor::FullButton(Layout& l, const String& label, bool highlight) {
    const bool pressed = Button(l, label, l.x, l.width, highlight);
    l.y += kRowHeight;
    return pressed;
}

int32 MapEditor::ButtonRow(Layout& l, std::initializer_list<const char*> labels, int32 highlight) {
    const float32 gap = 4.0f;
    const float32 count = static_cast<float32>(labels.size());
    const float32 w = (l.width - gap * (count - 1.0f)) / count;
    int32 pressed = -1;
    int32 i = 0;
    for (const char* label : labels) {
        if (Button(l, label, l.x + static_cast<float32>(i) * (w + gap), w, i == highlight)) pressed = i;
        ++i;
    }
    l.y += kRowHeight;
    return pressed;
}

int32 MapEditor::ButtonGrid(Layout& l, const Array<String>& labels, int32 columns, int32 highlight) {
    const float32 gap = 4.0f;
    const float32 w = (l.width - gap * static_cast<float32>(columns - 1)) / static_cast<float32>(columns);
    int32 pressed = -1;
    for (size_t i = 0; i < labels.size(); ++i) {
        const int32 column = static_cast<int32>(i) % columns;
        if (Button(l, labels[i], l.x + static_cast<float32>(column) * (w + gap), w, static_cast<int32>(i) == highlight)) {
            pressed = static_cast<int32>(i);
        }
        if (column == columns - 1 || i + 1 == labels.size()) l.y += kRowHeight;
    }
    return pressed;
}

bool MapEditor::Number(Layout& l, const String& label, float32& value, float32 step, float32 min_value, float32 max_value) {
    const uint32 id = HashLabel(label, ++widget_counter_);
    const float32 label_w = l.width * 0.4f;
    const float32 bw = 22.0f;
    const float32 field_x = l.x + label_w + bw + 2.0f;
    const float32 field_w = l.width - label_w - 2.0f * (bw + 2.0f);
    const float32 h = kRowHeight - 3.0f;
    if (!RowVisible(l, h)) {
        l.y += kRowHeight;
        return false;
    }
    l.ui->DrawText(label, l.x, l.y + 3.0f, kText, kFont - 1.0f);

    const float32 before = value;
    if (Button(l, "-", l.x + label_w, bw)) value -= step;
    if (Button(l, "+", field_x + field_w + 2.0f, bw)) value += step;
    const bool hovered = Hovered(l, field_x, l.y, field_w, h);
    const bool typing = text_focus_ && focus_field_ == id;

    if (typing) {
        // Typed value: Enter applies, Esc cancels, a click elsewhere applies
        if (!l.input->GetTextInput().empty()) {
            number_edit_ += l.input->GetTextInput();
            l.input->ClearTextInput();
        }
        if (l.input->IsKeyPressed(Input::KeyCode::Backspace)) PopUtf8(number_edit_);
        const bool apply = l.input->IsKeyPressed(Input::KeyCode::Enter) || l.input->IsKeyPressed(Input::KeyCode::KeypadEnter) ||
                           (mouse_clicked_ && !hovered);
        if (apply || l.input->IsKeyPressed(Input::KeyCode::Escape)) {
            if (apply) {
                String text = number_edit_;
                std::replace(text.begin(), text.end(), ',', '.');
                char* end = nullptr;
                const float32 parsed = std::strtof(text.c_str(), &end);
                if (end != text.c_str() && std::isfinite(parsed)) value = parsed;
            }
            l.input->EndTextInput();
            text_focus_ = false;
        }
    } else {
        if (hovered && mouse_clicked_) {
            active_widget_ = id;
            widget_start_x_ = mouse_.x;
            widget_start_value_ = value;
            widget_dragged_ = false;
            ui_consumed_click_ = true;
        }
        if (active_widget_ == id) {
            if (!mouse_down_) {
                active_widget_ = 0;
                if (!widget_dragged_) {
                    // Click without dragging: type the value
                    if (!text_focus_) l.input->BeginTextInput();
                    l.input->ClearTextInput();
                    text_focus_ = true;
                    focus_field_ = id;
                    number_edit_.clear();
                }
            } else if (std::abs(mouse_.x - widget_start_x_) > 3.0f || widget_dragged_) {
                widget_dragged_ = true;
                const float32 steps = std::round((mouse_.x - widget_start_x_) / 8.0f);
                value = widget_start_value_ + steps * step;
            }
        }
    }
    value = std::clamp(value, min_value, max_value);
    if (std::abs(value - before) > 1.0e-6f) value = std::clamp(std::round(value / (step * 0.5f)) * step * 0.5f, min_value, max_value);

    const bool editing = text_focus_ && focus_field_ == id;
    l.ui->DrawRect({field_x, l.y, field_w, h}, editing ? kButtonHighlight : active_widget_ == id ? kButtonHighlight : hovered ? kButtonHover : kFieldColor);
    const String text = editing ? number_edit_ + (std::fmod(anim_time_, 1.0f) < 0.5f ? "|" : "") : fmt::format("{:.2f}", value);
    const float32 tw = l.ui->GetTextWidth(text, kFont - 1.0f);
    l.ui->DrawText(text, field_x + (field_w - tw) * 0.5f, l.y + 3.0f, kText, kFont - 1.0f);
    l.y += kRowHeight;

    const bool changed = std::abs(value - before) > 1.0e-6f;
    if (changed && !(widget_pushed_undo_ && active_widget_ == id)) {
        // One undo step per drag, click or typed value
        const float32 now = value;
        value = before;
        PushUndo(fmt::format("{} geaendert", label));
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
    const float32 label_w = l.width * 0.4f;
    const float32 bw = 22.0f;
    const float32 field_x = l.x + label_w + bw + 2.0f;
    const float32 field_w = l.width - label_w - 2.0f * (bw + 2.0f);
    const int32 count = static_cast<int32>(items.size());
    const int32 before = index;
    if (RowVisible(l, kRowHeight - 3.0f)) {
        l.ui->DrawText(label, l.x, l.y + 3.0f, kText, kFont - 1.0f);
        if (Button(l, "<", l.x + label_w, bw)) index = (index + count - 1) % count;
        if (Button(l, ">", field_x + field_w + 2.0f, bw)) index = (index + 1) % count;
        if (Button(l, "", field_x, field_w)) index = (index + 1) % count;
        index = std::clamp(index, 0, count - 1);
        String text = items[static_cast<size_t>(index)];
        while (text.size() > 4 && l.ui->GetTextWidth(text, kFont - 1.0f) > field_w - 8.0f) text = "..." + text.substr(4);
        const float32 tw = l.ui->GetTextWidth(text, kFont - 1.0f);
        l.ui->DrawText(text, field_x + (field_w - tw) * 0.5f, l.y + 3.0f, kText, kFont - 1.0f);
    }
    l.y += kRowHeight;
    return index != before;
}

bool MapEditor::Toggle(Layout& l, const String& label, bool& value) {
    const float32 box = kRowHeight - 8.0f;
    bool pressed = false;
    if (RowVisible(l, kRowHeight - 3.0f)) {
        const bool hovered = Hovered(l, l.x, l.y, l.width, kRowHeight - 3.0f);
        l.ui->DrawRect({l.x, l.y + 2.0f, box, box}, hovered ? kButtonHover : kFieldColor);
        if (value) l.ui->DrawRect({l.x + 4.0f, l.y + 6.0f, box - 8.0f, box - 8.0f}, {0.45f, 0.7f, 1.0f, 1.0f});
        l.ui->DrawText(label, l.x + box + 8.0f, l.y + 3.0f, kText, kFont - 1.0f);
        pressed = hovered && mouse_clicked_;
        if (pressed) {
            value = !value;
            ui_consumed_click_ = true;
        }
    }
    l.y += kRowHeight;
    return pressed;
}

void MapEditor::TextField(Layout& l, const String& label, String& text) {
    const float32 label_w = l.width * 0.3f;
    const float32 h = kRowHeight - 3.0f;
    const float32 fx = l.x + label_w;
    const float32 fw = l.width - label_w;
    if (!RowVisible(l, h)) {
        l.y += kRowHeight;
        return;
    }
    l.ui->DrawText(label, l.x, l.y + 3.0f, kText, kFont - 1.0f);
    const bool hovered = Hovered(l, fx, l.y, fw, h);
    const uint32 id = HashLabel(label, 0);
    bool focused = text_focus_ && focus_field_ == id;
    if (mouse_clicked_) {
        if (hovered && !focused) {
            if (!text_focus_) l.input->BeginTextInput();
            l.input->ClearTextInput();
            text_focus_ = true;
            focus_field_ = id;
            ui_consumed_click_ = true;
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

bool MapEditor::ColorSwatches(Layout& l, Vector4& color) {
    constexpr int32 kColumns = 7;
    const float32 gap = 4.0f;
    const float32 size = (l.width - gap * (kColumns - 1)) / kColumns;
    const int32 count = static_cast<int32>(std::size(kPalette));
    bool changed = false;
    for (int32 i = 0; i < count; ++i) {
        const int32 column = i % kColumns;
        const float32 x = l.x + static_cast<float32>(column) * (size + gap);
        const float32 h = 20.0f;
        if (RowVisible(l, h)) {
            const PaletteColor& p = kPalette[i];
            const bool hovered = Hovered(l, x, l.y, size, h);
            const bool current = SameColor(p.color, color);
            if (current || hovered) l.ui->DrawRect({x - 2.0f, l.y - 2.0f, size + 4.0f, h + 4.0f}, current ? kSelect : kHover);
            if (p.color.w > 0.0f) {
                l.ui->DrawRect({x, l.y, size, h}, p.color);
            } else {
                l.ui->DrawRect({x, l.y, size, h}, kFieldColor);
                l.ui->DrawText("Std", x + 4.0f, l.y + 2.0f, kText, kFont - 3.0f);
            }
            if (hovered && mouse_clicked_) {
                color = p.color;
                changed = true;
                ui_consumed_click_ = true;
            }
        }
        if (column == kColumns - 1 || i + 1 == count) l.y += 26.0f;
    }
    return changed;
}

// ============================================================================
// Windows
// ============================================================================

void MapEditor::SetupPanels() {
    const float32 right = screen_w_ - 330.0f;
    panels_[kPanelBuild] = {"Bauen", 10.0f, kToolbar + 10.0f, 300.0f, true, false, 300.0f, 0.0f};
    panels_[kPanelView] = {"Ansicht", 10.0f, kToolbar + 380.0f, 300.0f, true, true, 200.0f, 0.0f};
    panels_[kPanelObjects] = {"Objekte", 10.0f, kToolbar + 420.0f, 300.0f, true, true, 300.0f, 0.0f};
    panels_[kPanelProps] = {"Eigenschaften", right, kToolbar + 10.0f, 320.0f, true, false, 300.0f, 0.0f};
    panels_[kPanelStates] = {"Zustaende", right, screen_h_ - kStatusBar - 300.0f, 320.0f, true, false, 240.0f, 0.0f};
    panels_[kPanelFile] = {"Karte", screen_w_ * 0.5f - 160.0f, kToolbar + 10.0f, 320.0f, false, false, 300.0f, 0.0f};
    panels_[kPanelCheck] = {"Pruefung", screen_w_ * 0.5f - 200.0f, kToolbar + 60.0f, 400.0f, false, false, 200.0f, 0.0f};
    z_order_ = {kPanelObjects, kPanelView, kPanelBuild, kPanelStates, kPanelProps, kPanelFile, kPanelCheck};
    panels_ready_ = true;
}

float32 MapEditor::PanelHeight(const Panel& panel) const {
    if (panel.minimized) return kTitle;
    const float32 max_body = std::max(80.0f, screen_h_ - kStatusBar - panel.y - kTitle - 6.0f);
    return kTitle + std::min(panel.content_height + kPad, max_body);
}

int32 MapEditor::PanelAt(float32 x, float32 y) const {
    if (!panels_ready_) return -1;
    for (auto it = z_order_.rbegin(); it != z_order_.rend(); ++it) {
        const Panel& p = panels_[*it];
        if (p.open && x >= p.x && x < p.x + p.width && y >= p.y && y < p.y + PanelHeight(p)) return *it;
    }
    return -1;
}

void MapEditor::BringToFront(PanelId id) {
    z_order_.erase(std::remove(z_order_.begin(), z_order_.end(), static_cast<int32>(id)), z_order_.end());
    z_order_.push_back(id);
}

void MapEditor::OpenPanel(PanelId id) {
    panels_[id].open = true;
    panels_[id].minimized = false;
    BringToFront(id);
}

void MapEditor::DrawToolbar(UI::UIContext& ui, Input::InputSystem& input) {
    ui.DrawRect({0.0f, 0.0f, screen_w_, kToolbar}, kToolbarColor);
    Layout l{&ui, &input, 8.0f, 8.0f, screen_w_ - 16.0f, 0.0f, kToolbar, hovered_panel_ < 0};
    float32 x = 8.0f;
    const auto button = [&](const String& label, float32 w, bool highlight = false) {
        const bool pressed = Button(l, label, x, w, highlight);
        x += w + 4.0f;
        return pressed;
    };
    const auto gap = [&]() {
        ui.DrawRect({x + 3.0f, 8.0f, 1.0f, kToolbar - 16.0f}, {1, 1, 1, 0.2f});
        x += 10.0f;
    };
    if (button("Neu", 50.0f)) NewLevel();
    if (button("Oeffnen", 70.0f, panels_[kPanelFile].open)) {
        if (panels_[kPanelFile].open) panels_[kPanelFile].open = false;
        else OpenPanel(kPanelFile);
    }
    if (button(dirty_ ? "Speichern *" : "Speichern", 92.0f)) SaveCurrent();
    gap();
    if (button(fmt::format("<- Zurueck ({})", undo_.size()), 120.0f)) Undo();
    if (button(fmt::format("Vor ({}) ->", redo_.size()), 100.0f)) Redo();
    gap();
    if (button("Pruefen", 72.0f)) RunCheck();
    if (button("> Testen", 80.0f)) {
        problems_ = CheckLevelRoutes(*config_, level_);
        checked_ = true;
        if (!problems_.empty()) SetStatus("Hinweis: die Pruefung hat Probleme gefunden", true);
        request_ = Request::Play;
    }
    gap();
    const PanelId toggles[] = {kPanelBuild, kPanelProps, kPanelStates, kPanelView, kPanelObjects};
    for (const PanelId id : toggles) {
        if (button(panels_[id].title, ui.GetTextWidth(panels_[id].title, kFont - 1.0f) + 18.0f, panels_[id].open)) {
            if (panels_[id].open) panels_[id].open = false;
            else OpenPanel(id);
        }
    }
    gap();
    if (button(top_view_ ? "Perspektive" : "Draufsicht", 96.0f, top_view_)) ToggleTopView();
    // Map name and exit on the right
    x = screen_w_ - 90.0f;
    if (button("Menue", 82.0f)) {
        if (dirty_ && !confirm_exit_) {
            confirm_exit_ = true;
            SetStatus("Nicht gespeichert! Nochmal 'Menue' klicken zum Verlassen", true);
        } else {
            request_ = Request::Exit;
        }
    }
    const String title = fmt::format("{}{}", level_.name, dirty_ ? " (ungespeichert)" : "");
    ui.DrawText(title, screen_w_ - 100.0f - ui.GetTextWidth(title, kFont + 1.0f), 10.0f, {1, 1, 1, 0.85f}, kFont + 1.0f);
}

void MapEditor::DrawPanel(UI::UIContext& ui, Input::InputSystem& input, PanelId id) {
    Panel& panel = panels_[id];
    if (!panel.open) return;
    const float32 height = PanelHeight(panel);
    const bool active = hovered_panel_ == id || dragged_panel_ == id;
    ui.DrawRect({panel.x, panel.y, panel.width, height}, kPanelColor);
    ui.DrawRect({panel.x, panel.y, panel.width, kTitle}, active ? kTitleActive : kTitleColor);
    ui.DrawText(panel.title, panel.x + 10.0f, panel.y + 5.0f, kText, kFont);

    // Title bar buttons: minimise / restore and close
    Layout title{&ui, &input, panel.x, panel.y + 3.0f, panel.width, panel.y, panel.y + kTitle, hovered_panel_ == id};
    if (Button(title, panel.minimized ? "+" : "_", panel.x + panel.width - 52.0f, 22.0f)) panel.minimized = !panel.minimized;
    if (Button(title, "x", panel.x + panel.width - 26.0f, 22.0f)) panel.open = false;
    if (!panel.open || panel.minimized) return;

    // Body with scrolling
    const float32 body_top = panel.y + kTitle;
    const float32 body_bottom = panel.y + height - 4.0f;
    const float32 visible = body_bottom - body_top;
    if (hovered_panel_ == id && scroll_ != 0.0f) panel.scroll -= scroll_ * 40.0f;
    panel.scroll = std::clamp(panel.scroll, 0.0f, std::max(0.0f, panel.content_height + kPad - visible));

    Layout l{&ui, &input, panel.x + kPad, body_top + 6.0f - panel.scroll, panel.width - 2.0f * kPad, body_top, body_bottom,
             hovered_panel_ == id && dragged_panel_ < 0};
    const float32 start = l.y;
    switch (id) {
        case kPanelBuild: DrawBuildPanel(l); break;
        case kPanelProps: DrawPropertiesPanel(l); break;
        case kPanelStates: DrawStatesPanel(l); break;
        case kPanelView: DrawViewPanel(l); break;
        case kPanelObjects: DrawObjectsPanel(l); break;
        case kPanelFile: DrawFilePanel(l); break;
        case kPanelCheck: DrawCheckPanel(l); break;
        case kPanelCount: break;
    }
    panel.content_height = l.y - start;
    // Scroll bar
    if (panel.content_height + kPad > visible) {
        const float32 ratio = visible / (panel.content_height + kPad);
        const float32 bar_h = std::max(20.0f, visible * ratio);
        const float32 bar_y = body_top + (visible - bar_h) * (panel.scroll / std::max(1.0f, panel.content_height + kPad - visible));
        ui.DrawRect({panel.x + panel.width - 5.0f, bar_y, 3.0f, bar_h}, {1, 1, 1, 0.35f});
    }
}

void MapEditor::DrawBuildPanel(Layout& l) {
    const int32 tab = ButtonRow(l, {"Struktur", "Bewegung", "Spiel", "Deko"}, build_tab_);
    if (tab >= 0) build_tab_ = tab;
    l.y += 4.0f;
    switch (build_tab_) {
        case 0: {
            Label(l, "Waende", kDim);
            int32 p = ButtonGrid(l, {"Wand 4 m", "Wand 8 m", "Wand 2 m", "Niedrig 1,2 m", "Saeule", "Raum 10x10"}, 3);
            if (p == 0) AddBox({4.0f, kWallHeight, 0.5f}, Surface::Wall, 0.0f);
            if (p == 1) AddBox({8.0f, kWallHeight, 0.5f}, Surface::Wall, 0.0f);
            if (p == 2) AddBox({2.0f, kWallHeight, 0.5f}, Surface::Wall, 0.0f);
            if (p == 3) AddBox({4.0f, 1.2f, 0.5f}, Surface::Wall, 0.0f);
            if (p == 4) AddBox({1.0f, kWallHeight, 1.0f}, Surface::Wall, 0.0f);
            if (p == 5) AddRoom();
            Label(l, "Boden und Hoehen", kDim);
            p = ButtonGrid(l, {"Boden 8x8", "Plattform", "Stufe", "Treppe", "Decke 8x8"}, 3);
            if (p == 0) AddBox({8.0f, 0.5f, 8.0f}, Surface::Floor, -0.5f);
            if (p == 1) AddBox({4.0f, 1.0f, 4.0f}, Surface::Floor, 0.0f);
            if (p == 2) AddBox({2.0f, 0.25f, 1.0f}, Surface::Step, 0.0f);
            if (p == 3) AddStairs();
            if (p == 4) AddBox({8.0f, 0.5f, 8.0f}, Surface::Ceiling, kWallHeight);
            Label(l, "Deckung", kDim);
            p = ButtonGrid(l, {"Niedrig", "Hoch", "Kiste", "Lang"}, 4);
            if (p == 0) AddBox({1.5f, 1.0f, 0.6f}, Surface::Cover, 0.0f);
            if (p == 1) AddBox({1.5f, 1.8f, 0.6f}, Surface::Cover, 0.0f);
            if (p == 2) AddBox({1.2f, 1.2f, 1.2f}, Surface::Cover, 0.0f);
            if (p == 3) AddBox({4.0f, 1.0f, 0.6f}, Surface::Cover, 0.0f);
            break;
        }
        case 1: {
            Array<String> groups;
            for (uint32 g = 0; g < static_cast<uint32>(SegmentGroup::Count); ++g) groups.push_back(SegmentGroupName(static_cast<SegmentGroup>(g)));
            Choice(l, "Gruppe neu", new_group_, groups);
            const SegmentGroup group = static_cast<SegmentGroup>(new_group_);
            const int32 p = ButtonGrid(l, {"Schiebetor", "Senktuer", "Drehwand", "Seitenwand", "Drehraum"}, 2);
            if (p == 0) AddSegment({4.0f, kWallHeight, 0.5f}, {4.0f, 0.0f, 0.0f}, 0.0f, group);
            if (p == 1) AddSegment({3.0f, kWallHeight, 0.5f}, {0.0f, -(kWallHeight + 0.1f), 0.0f}, 0.0f, group);
            if (p == 2) AddSegment({6.0f, kWallHeight, 0.4f}, Vector3::Zero, 90.0f, group);
            if (p == 3) AddSegment({8.0f, kWallHeight, 0.4f}, {0.0f, 0.0f, -2.0f}, 0.0f, group);
            if (p == 4) AddRotatingRoom();
            Label(l, "Gruen = Zustand 0, Rot = Zustand 1.", kDim);
            Label(l, "Welche Gruppe wann faehrt: Fenster Zustaende.", kDim);
            break;
        }
        case 2: {
            Label(l, "Setzt das Objekt in die Bildmitte:", kDim);
            const Vector3 at = PlacementPoint();
            const int32 p = ButtonGrid(l, {"Start Blau", "Start Rot", "Kern", "Ausgang Blau", "Ausgang Rot", "Startplatte"}, 2);
            if (p >= 0) PushUndo("Spielobjekt versetzt");
            if (p == 0) { level_.spawns[0].position = at; Select({Kind::Spawn, 0}, false); }
            if (p == 1) { level_.spawns[1].position = at; Select({Kind::Spawn, 1}, false); }
            if (p == 2) { level_.core_home = at; level_.has_core = true; Select({Kind::Core, 0}, false); }
            if (p == 3) { level_.exits[0].center = at + Vector3(0, 1.0f, 0); Select({Kind::Exit, 0}, false); }
            if (p == 4) { level_.exits[1].center = at + Vector3(0, 1.0f, 0); Select({Kind::Exit, 1}, false); }
            if (p == 5) AddBox({1.5f, 0.02f, 1.5f}, Surface::SpawnA, 0.0f, false);
            if (p >= 0) MarkChanged();
            bool has_core = level_.has_core;
            if (Toggle(l, "Karte hat einen Kern", has_core)) {
                PushUndo("Kern umgeschaltet");
                level_.has_core = has_core;
                MarkChanged();
            }
            break;
        }
        case 3: {
            Number(l, "Groesse", prop_scale_, 0.1f, 0.01f, 100.0f);
            if (FullButton(l, "Liste neu laden")) RefreshFileLists();
            if (prop_files_.empty()) {
                Label(l, "Keine Modelle in Assets/Models/Props.", kDim);
                Label(l, "GLB, GLTF, FBX oder OBJ dort ablegen.", kDim);
            }
            for (const String& file : prop_files_) {
                const String name = std::filesystem::path(file).stem().string();
                if (FullButton(l, name)) AddProp(file);
            }
            break;
        }
        default: break;
    }
}

void MapEditor::DrawPropertiesPanel(Layout& l) {
    if (selection_.empty()) {
        Label(l, "Nichts gewaehlt.", kDim);
        Label(l, "Klick waehlt, Shift+Klick fuegt hinzu,", kDim);
        Label(l, "Ziehen auf leerer Flaeche waehlt ein Rechteck.", kDim);
        Header(l, "Karte");
        TextField(l, "Titel", level_.name);
        bool toggle_mode = level_.shift_mode == ShiftMode::ToggleAll;
        if (Toggle(l, "Alle Waende wechseln jedes Mal", toggle_mode)) {
            PushUndo("Modus geaendert");
            level_.shift_mode = toggle_mode ? ShiftMode::ToggleAll : ShiftMode::Patterns;
            MarkChanged();
        }
        Label(l, fmt::format("{} Bloecke, {} bewegliche Waende, {} Deko", level_.boxes.size(), level_.segments.size(), level_.props.size()), kDim);
        return;
    }

    const Selection primary = Primary();
    const size_t i = static_cast<size_t>(std::max(0, primary.index));
    bool changed = false;
    const auto vector_rows = [&](const char* name, Vector3& v, float32 step, float32 min_value, float32 max_value) {
        changed |= Number(l, fmt::format("{} X", name), v.x, step, min_value, max_value);
        changed |= Number(l, fmt::format("{} Y", name), v.y, step, min_value, max_value);
        changed |= Number(l, fmt::format("{} Z", name), v.z, step, min_value, max_value);
    };

    if (selection_.size() > 1) {
        Label(l, fmt::format("{} Objekte gewaehlt (Werte: zuletzt gewaehltes)", selection_.size()), kHeader);
    }
    Label(l, Describe(primary), kHeader);

    // Position of the primary object; the delta moves the whole group
    Vector3 position;
    if (GetPosition(primary, position)) {
        Vector3 edited = position;
        Header(l, "Position");
        vector_rows("Pos", edited, grid_, -1000.0f, 1000.0f);
        if ((edited - position).LengthSquared() > 1.0e-10f) {
            const Vector3 delta = edited - position;
            // Number() already recorded the undo step; apply to the whole selection
            MoveSelection(delta);
        }
    }

    switch (primary.kind) {
        case Kind::Box: {
            LevelBox& box = level_.boxes[i];
            Header(l, "Groesse");
            vector_rows("Groesse", box.size, grid_, 0.05f, 1000.0f);
            Header(l, "Art und Aussehen");
            int32 surface = static_cast<int32>(box.surface);
            if (Choice(l, "Art", surface, kSurfaceNames)) {
                PushUndo("Art geaendert");
                for (const Selection& s : selection_) {
                    if (s.kind != Kind::Box) continue;
                    LevelBox& b = level_.boxes[static_cast<size_t>(s.index)];
                    b.surface = static_cast<Surface>(surface);
                    b.casts_shadow = b.surface != Surface::Ceiling;
                }
                changed = true;
            }
            bool collision = box.collision;
            if (Toggle(l, "Kollision (man stoesst dagegen)", collision)) {
                PushUndo("Kollision geaendert");
                for (const Selection& s : selection_) {
                    if (s.kind == Kind::Box) level_.boxes[static_cast<size_t>(s.index)].collision = collision;
                }
                changed = true;
            }
            break;
        }
        case Kind::Segment: {
            MovingSegmentDef& def = level_.segments[i];
            Header(l, "Bewegung");
            TextField(l, "Name", def.name);
            int32 group = static_cast<int32>(def.group);
            Array<String> groups;
            for (uint32 g = 0; g < static_cast<uint32>(SegmentGroup::Count); ++g) groups.push_back(SegmentGroupName(static_cast<SegmentGroup>(g)));
            if (Choice(l, "Gruppe", group, groups)) {
                PushUndo("Gruppe geaendert");
                for (const Selection& s : selection_) {
                    if (s.kind == Kind::Segment) level_.segments[static_cast<size_t>(s.index)].group = static_cast<SegmentGroup>(group);
                }
                changed = true;
            }
            vector_rows("Weg", def.move, grid_, -100.0f, 100.0f);
            changed |= Number(l, "Drehung", def.rotate, 15.0f, -180.0f, 180.0f);
            if (std::abs(def.rotate) > 0.01f) vector_rows("Drehpunkt", def.pivot, grid_, -1000.0f, 1000.0f);
            Label(l, fmt::format("Fahrt dauert {:.1f} s (max {:.1f} m/s)", SegmentMoveDuration(config_->shifts, def), config_->shifts.max_speed),
                  {0.7f, 0.8f, 1.0f, 1.0f});
            Header(l, "Groesse");
            vector_rows("Groesse", def.size, grid_, 0.05f, 1000.0f);
            break;
        }
        case Kind::Prop: {
            LevelProp& prop = level_.props[i];
            Header(l, "Modell");
            changed |= Number(l, "Drehung", prop.yaw, 15.0f, -360.0f, 360.0f);
            changed |= Number(l, "Skalierung", prop.scale, 0.05f, 0.01f, 100.0f);
            break;
        }
        case Kind::Spawn: changed |= Number(l, "Blickrichtung", level_.spawns[i].yaw, 15.0f, -360.0f, 360.0f); break;
        case Kind::Exit: {
            ZoneBox& exit = level_.exits[i];
            changed |= Number(l, "Breite X", exit.size.x, grid_, 0.5f, 50.0f);
            changed |= Number(l, "Breite Z", exit.size.z, grid_, 0.5f, 50.0f);
            break;
        }
        default: break;
    }

    // Colour and glow for blocks and moving walls (applies to all selected)
    if (primary.kind == Kind::Box || primary.kind == Kind::Segment) {
        Header(l, "Farbe");
        Vector4 color = primary.kind == Kind::Box ? level_.boxes[i].color : level_.segments[i].color;
        float32 glow = primary.kind == Kind::Box ? level_.boxes[i].glow : level_.segments[i].glow;
        const Vector4 old_color = color;
        const float32 old_glow = glow;
        bool look_changed = false;
        if (ColorSwatches(l, color)) {
            PushUndo("Farbe geaendert");
            look_changed = true;
        }
        if (color.w > 0.0f) {
            float32 r = color.x, g = color.y, b = color.z;
            look_changed |= Number(l, "Rot", r, 0.05f, 0.0f, 1.0f);
            look_changed |= Number(l, "Gruen", g, 0.05f, 0.0f, 1.0f);
            look_changed |= Number(l, "Blau", b, 0.05f, 0.0f, 1.0f);
            color = Vector4(r, g, b, 1.0f);
        }
        look_changed |= Number(l, "Leuchten", glow, 0.25f, 0.0f, 10.0f);
        if (look_changed) {
            for (const Selection& s : selection_) {
                if (s.kind == Kind::Box) {
                    LevelBox& b = level_.boxes[static_cast<size_t>(s.index)];
                    if (!SameColor(color, old_color) || color.w != old_color.w) b.color = color;
                    if (glow != old_glow) b.glow = glow;
                } else if (s.kind == Kind::Segment) {
                    MovingSegmentDef& d = level_.segments[static_cast<size_t>(s.index)];
                    if (!SameColor(color, old_color) || color.w != old_color.w) d.color = color;
                    if (glow != old_glow) d.glow = glow;
                }
            }
            if (primary.kind == Kind::Box) {
                level_.boxes[i].color = color;
                level_.boxes[i].glow = glow;
            } else {
                level_.segments[i].color = color;
                level_.segments[i].glow = glow;
            }
            changed = true;
        }
    }
    if (changed) MarkChanged();

    Header(l, "Aktionen");
    const int32 pressed = ButtonRow(l, {"Kopie", "Drehen 90", "Loeschen"});
    if (pressed == 0) DuplicateSelection();
    if (pressed == 1) RotateSelection();
    if (pressed == 2) DeleteSelection();
    if (FullButton(l, "Kamera zum Objekt (F)")) FocusSelection();
}

void MapEditor::DrawStatesPanel(Layout& l) {
    GroupMask groups = 0;
    for (const MovingSegmentDef& def : level_.segments) groups |= GroupBit(def.group);
    Label(l, "Zeigt die Karte so, wie sie nach einem Muster aussieht:", kDim);
    const ShiftPattern patterns[] = {ShiftPattern::GateSwap, ShiftPattern::Rotate, ShiftPattern::CoreRebuild, ShiftPattern::Squeeze,
                                     ShiftPattern::AllOpen};
    Array<String> names = {"Startzustand"};
    for (const ShiftPattern p : patterns) {
        const bool used = (PatternGroups(p) & groups) != 0;
        names.push_back(fmt::format("{}{}", PatternName(p), used ? "" : " (-)"));
    }
    const int32 pressed = ButtonGrid(l, names, 2, playing_ ? -1 : preview_pattern_);
    if (pressed >= 0) {
        playing_ = false;
        preview_pattern_ = pressed;
        preview_state_ = pressed == 0 ? ShiftPlan::InitialState() : PatternPreviewState(patterns[pressed - 1]);
    }
    if (preview_pattern_ > 0 && !playing_) {
        String moved;
        const GroupMask pg = PatternGroups(patterns[preview_pattern_ - 1]) & groups;
        for (uint32 g = 0; g < static_cast<uint32>(SegmentGroup::Count); ++g) {
            if (pg & GroupBit(static_cast<SegmentGroup>(g))) moved += String(moved.empty() ? "" : ", ") + SegmentGroupName(static_cast<SegmentGroup>(g));
        }
        Label(l, moved.empty() ? "Dieses Muster bewegt hier nichts" : "Bewegt: " + moved, {0.7f, 0.85f, 1.0f, 1.0f});
    }

    Header(l, "Ablauf wie im Spiel");
    const int32 play = ButtonRow(l, {playing_ ? "Pause" : "Abspielen", "Neu starten"}, playing_ ? 0 : -1);
    if (play == 0) playing_ = !playing_;
    if (play == 1) {
        play_time_ = 0.0f;
        playing_ = true;
    }
    int32 speed_index = 0;
    for (int32 s = 0; s < 3; ++s) {
        if (kSpeeds[s] == play_speed_) speed_index = s;
    }
    if (Choice(l, "Tempo", speed_index, kSpeedNames)) play_speed_ = kSpeeds[speed_index];
    if (config_) {
        const ShiftPhase phase = EvaluateShiftPhase(config_->shifts, play_time_);
        const ShiftPlan plan(level_.shift_mode, 7u, groups, config_->shifts.all_open_max_per);
        const String now = phase.moving ? fmt::format("Verschiebung {}: {} (faehrt)", phase.next, PatternName(plan.GetPattern(phase.next)))
                                        : fmt::format("Naechste: {} in {:.0f} s", PatternName(plan.GetPattern(phase.next)), phase.time_to_next);
        Label(l, fmt::format("Rundenzeit {:.0f} s - {}", play_time_, now), playing_ ? Vector4(0.6f, 1.0f, 0.6f, 1.0f) : kDim);
    }

    Header(l, "Gruppen einzeln");
    for (uint32 g = 0; g < static_cast<uint32>(SegmentGroup::Count); ++g) {
        const SegmentGroup group = static_cast<SegmentGroup>(g);
        if (!(groups & GroupBit(group))) continue;
        bool on = (preview_state_ & GroupBit(group)) != 0;
        if (Toggle(l, fmt::format("{} im Zustand 1", SegmentGroupName(group)), on)) {
            preview_state_ ^= GroupBit(group);
            playing_ = false;
            preview_pattern_ = -1;
        }
    }
    if (!groups) Label(l, "Noch keine beweglichen Waende.", kDim);
}

void MapEditor::DrawViewPanel(Layout& l) {
    int32 grid_index = 1;
    for (int32 i = 0; i < 4; ++i) {
        if (std::abs(kGridSizes[i] - grid_) < 1.0e-4f) grid_index = i;
    }
    if (Choice(l, "Raster", grid_index, kGridNames)) grid_ = kGridSizes[grid_index];
    Toggle(l, "Am Raster einrasten", snap_);
    Toggle(l, "Bodenraster zeigen", show_grid_);
    Toggle(l, "Beschriftungen", labels_);
    Toggle(l, "Wandgruppen beschriften", group_labels_);
    Number(l, "Kameratempo", cam_speed_, 2.0f, 2.0f, 80.0f);
    Header(l, "Ebenen (sichtbar und waehlbar)");
    for (int32 i = 0; i < kLayerCount; ++i) Toggle(l, kLayerNames[i], layers_[i]);
}

void MapEditor::DrawObjectsPanel(Layout& l) {
    const auto entry = [&](const Selection& s) {
        const bool selected = IsSelected(s);
        if (FullButton(l, Describe(s), selected)) {
            Select(s, false);
            FocusSelection();
        }
    };
    Header(l, "Spielobjekte");
    entry({Kind::Spawn, 0});
    entry({Kind::Spawn, 1});
    if (level_.has_core) entry({Kind::Core, 0});
    entry({Kind::Exit, 0});
    entry({Kind::Exit, 1});
    if (!level_.segments.empty()) Header(l, fmt::format("Bewegliche Waende ({})", level_.segments.size()));
    for (size_t i = 0; i < level_.segments.size(); ++i) entry({Kind::Segment, static_cast<int32>(i)});
    if (!level_.props.empty()) Header(l, fmt::format("Deko ({})", level_.props.size()));
    for (size_t i = 0; i < level_.props.size(); ++i) entry({Kind::Prop, static_cast<int32>(i)});
    Header(l, fmt::format("Bloecke ({})", level_.boxes.size()));
    for (size_t i = 0; i < level_.boxes.size(); ++i) entry({Kind::Box, static_cast<int32>(i)});
}

void MapEditor::DrawFilePanel(Layout& l) {
    TextField(l, "Datei", file_name_);
    TextField(l, "Titel", level_.name);
    const int32 pressed = ButtonRow(l, {"Speichern", "Neue Karte"});
    if (pressed == 0) SaveCurrent();
    if (pressed == 1) NewLevel();
    Header(l, "Karte oeffnen");
    for (size_t i = 0; i < map_names_.size(); ++i) {
        const bool builtin = std::find(BuiltinLevelNames().begin(), BuiltinLevelNames().end(), map_names_[i]) != BuiltinLevelNames().end();
        const std::filesystem::path file = std::filesystem::path(map_dir_) / (map_names_[i] + ".json");
        std::error_code ec;
        const bool has_file = std::filesystem::exists(file, ec);
        const String label = fmt::format("{}{}", map_names_[i], has_file ? (builtin ? "  (bearbeitet)" : "") : "  (eingebaut)");
        if (FullButton(l, label, static_cast<int32>(i) == map_index_)) {
            if (dirty_) PushUndo("Karte gewechselt");
            map_index_ = static_cast<int32>(i);
            LoadLevelByName(map_names_[i]);
        }
    }
    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::path(map_dir_) / "_autosave.json", ec)) {
        Header(l, "Sicherung");
        if (FullButton(l, "Automatische Sicherung laden")) {
            PushUndo("Sicherung geladen");
            Level level;
            String error;
            if (LoadLevel((std::filesystem::path(map_dir_) / "_autosave.json").string(), level, &error)) {
                level_ = level;
                selection_.clear();
                MarkChanged();
                SetStatus("Automatische Sicherung geladen - unter neuem Namen speichern");
            } else {
                SetStatus(error, true);
            }
        }
    }
}

void MapEditor::DrawCheckPanel(Layout& l) {
    if (!checked_) {
        Label(l, "Noch nicht geprueft (oder seitdem geaendert).", kDim);
    } else if (problems_.empty()) {
        Label(l, "Alles in Ordnung:", {0.5f, 1.0f, 0.6f, 1.0f});
        Label(l, "In jedem Wandzustand erreichen beide Teams", kDim);
        Label(l, "den Kern und der Kern beide Ausgaenge.", kDim);
    } else {
        for (const String& problem : problems_) Label(l, problem, {1.0f, 0.55f, 0.45f, 1.0f});
    }
    if (FullButton(l, "Erneut pruefen")) RunCheck();
}

void MapEditor::DrawUI(UI::UIContext& ui, Input::InputSystem& input, float32 width, float32 height) {
    screen_w_ = std::max(1.0f, width);
    screen_h_ = std::max(1.0f, height);
    if (!panels_ready_) SetupPanels();
    mouse_ = input.GetMousePosition();
    mouse_down_ = input.IsMouseButtonDown(Input::MouseButton::Left);
    const bool right_down = input.IsMouseButtonDown(Input::MouseButton::Right);
    mouse_clicked_ = input.IsMouseButtonPressed(Input::MouseButton::Left) && !right_down && !dragging_ && !marquee_;
    mouse_released_ = input.IsMouseButtonReleased(Input::MouseButton::Left);
    scroll_ = right_down ? 0.0f : input.GetMouseScroll().y;
    widget_counter_ = 0;
    ui_consumed_click_ = false;
    if (!mouse_down_) widget_pushed_undo_ = false;

    // Window interaction: topmost window under the mouse, title bar dragging, keep windows on screen
    hovered_panel_ = (dragging_ || marquee_) ? -1 : PanelAt(mouse_.x, mouse_.y);
    if (mouse_clicked_ && hovered_panel_ >= 0) {
        Panel& panel = panels_[hovered_panel_];
        BringToFront(static_cast<PanelId>(hovered_panel_));
        if (mouse_.y < panel.y + kTitle && mouse_.x < panel.x + panel.width - 56.0f) {
            dragged_panel_ = hovered_panel_;
            panel_grab_ = Vector2(mouse_.x - panel.x, mouse_.y - panel.y);
        }
    }
    if (dragged_panel_ >= 0) {
        if (!mouse_down_) {
            dragged_panel_ = -1;
        } else {
            Panel& panel = panels_[dragged_panel_];
            panel.x = std::clamp(mouse_.x - panel_grab_.x, 0.0f, screen_w_ - 60.0f);
            panel.y = std::clamp(mouse_.y - panel_grab_.y, kToolbar, screen_h_ - kStatusBar - kTitle);
        }
    }

    // Selection rectangle
    if (marquee_) {
        const float32 x0 = std::min(marquee_start_.x, mouse_.x), x1 = std::max(marquee_start_.x, mouse_.x);
        const float32 y0 = std::min(marquee_start_.y, mouse_.y), y1 = std::max(marquee_start_.y, mouse_.y);
        ui.DrawRect({x0, y0, x1 - x0, y1 - y0}, {0.3f, 0.55f, 1.0f, 0.15f});
        ui.DrawRectOutline({x0, y0, x1 - x0, y1 - y0}, {0.4f, 0.65f, 1.0f, 0.9f}, 1.0f);
    }

    for (const int32 id : z_order_) DrawPanel(ui, input, static_cast<PanelId>(id));
    DrawToolbar(ui, input);

    // Status bar: message, hovered object, help
    ui.DrawRect({0.0f, height - kStatusBar, width, kStatusBar}, kToolbarColor);
    String left;
    Vector4 color = kDim;
    if (status_time_ > 0.0f) {
        left = status_;
        color = status_error_ ? Vector4(1.0f, 0.45f, 0.4f, 1.0f) : Vector4(0.5f, 1.0f, 0.6f, 1.0f);
    } else if (hover_.kind != Kind::None) {
        left = Describe(hover_);
        color = kText;
    } else {
        left = "Rechte Maus+WASD fliegen | Klick waehlen, Shift+Klick mehrere | Ziehen verschieben (Alt: hoch) | R drehen | "
               "Entf | Strg+Z/Y | Strg+C/V/D | T Draufsicht | F zeigen";
    }
    ui.DrawText(left, 10.0f, height - kStatusBar + 6.0f, color, kFont - 1.0f);
    const String right = fmt::format("Raster {:.2f} m  |  {} gewaehlt", grid_, selection_.size());
    ui.DrawText(right, width - 10.0f - ui.GetTextWidth(right, kFont - 1.0f), height - kStatusBar + 6.0f, kDim, kFont - 1.0f);
}

} // namespace Shift

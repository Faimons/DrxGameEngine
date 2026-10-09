/**
 * DRX Engine - Debug & Profiler System Implementation
 */

#include "Debug/Public/DebugSystem.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace DRX::Debug {

// ============================================================================
// ScopedProfiler
// ============================================================================

ScopedProfiler::ScopedProfiler(const char* name) {
    sample_index_ = Profiler::Instance().BeginSample(name);
}

ScopedProfiler::~ScopedProfiler() {
    Profiler::Instance().EndSample(sample_index_);
}

// ============================================================================
// Profiler
// ============================================================================

Profiler& Profiler::Instance() {
    static Profiler instance;
    return instance;
}

uint64 Profiler::GetTimestamp() const {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();
}

void Profiler::BeginFrame() {
    if (!enabled_) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Store previous frame
    previous_frame_ = std::move(current_frame_);

    // Start new frame
    current_frame_ = ProfileFrame{};
    current_frame_.frame_number = frame_number_++;
    current_frame_.frame_start = GetTimestamp();

    sample_stack_.clear();
    current_depth_ = 0;
}

void Profiler::EndFrame() {
    if (!enabled_) return;

    std::lock_guard<std::mutex> lock(mutex_);

    current_frame_.frame_end = GetTimestamp();

    // Add to history
    frame_history_.push_back(current_frame_);
    if (frame_history_.size() > max_history_size_) {
        frame_history_.erase(frame_history_.begin());
    }
}

uint32 Profiler::BeginSample(const char* name) {
    if (!enabled_) return 0;

    std::lock_guard<std::mutex> lock(mutex_);

    ProfileSample sample;
    sample.name = name;
    sample.start_time = GetTimestamp();
    sample.end_time = 0;
    sample.depth = current_depth_;
    sample.parent_index = sample_stack_.empty() ? UINT32_MAX : sample_stack_.back();
    sample.call_count = 1;

    uint32 index = static_cast<uint32>(current_frame_.samples.size());
    current_frame_.samples.push_back(sample);

    sample_stack_.push_back(index);
    current_depth_++;

    return index;
}

void Profiler::EndSample(uint32 index) {
    if (!enabled_) return;

    std::lock_guard<std::mutex> lock(mutex_);

    if (index < current_frame_.samples.size()) {
        current_frame_.samples[index].end_time = GetTimestamp();
    }

    if (!sample_stack_.empty()) {
        sample_stack_.pop_back();
    }
    current_depth_ = std::max(0u, current_depth_ - 1);
}

void Profiler::BeginGPUTimer(const char* name) {
    // GPU timing would be implemented with Vulkan timestamp queries
    GPUTimerQuery query;
    query.name = name;
    query.start_query = GetTimestamp(); // Placeholder
    gpu_timers_.push_back(query);
}

void Profiler::EndGPUTimer(const char* name) {
    for (auto& timer : gpu_timers_) {
        if (timer.name == name && timer.end_query == 0) {
            timer.end_query = GetTimestamp();
            timer.duration_ms = (timer.end_query - timer.start_query) / 1000000.0;
            break;
        }
    }
}

void Profiler::TrackAllocation(void* ptr, uint64 size, const char* category) {
    std::lock_guard<std::mutex> lock(mutex_);

    allocation_map_[ptr] = {size, category};

    memory_stats_.total_allocated += size;
    memory_stats_.current_usage += size;
    memory_stats_.allocation_count++;
    memory_stats_.peak_usage = std::max(memory_stats_.peak_usage, memory_stats_.current_usage);
    memory_stats_.category_usage[category] += size;
}

void Profiler::TrackDeallocation(void* ptr) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = allocation_map_.find(ptr);
    if (it != allocation_map_.end()) {
        uint64 size = it->second.first;
        const String& category = it->second.second;

        memory_stats_.total_freed += size;
        memory_stats_.current_usage -= size;
        memory_stats_.deallocation_count++;
        memory_stats_.category_usage[category] -= size;

        allocation_map_.erase(it);
    }
}

float64 Profiler::GetAverageFrameTime() const {
    if (frame_history_.empty()) return 0.0;

    float64 total = 0.0;
    for (const auto& frame : frame_history_) {
        total += frame.GetFrameTimeMs();
    }
    return total / frame_history_.size();
}

float64 Profiler::GetAverageFPS() const {
    float64 avgFrameTime = GetAverageFrameTime();
    return avgFrameTime > 0.0 ? 1000.0 / avgFrameTime : 0.0;
}

float64 Profiler::GetSampleAverageTime(const String& name) const {
    float64 total = 0.0;
    uint32 count = 0;

    for (const auto& frame : frame_history_) {
        for (const auto& sample : frame.samples) {
            if (sample.name == name) {
                total += sample.GetDurationMs();
                count++;
            }
        }
    }

    return count > 0 ? total / count : 0.0;
}

String Profiler::ExportToJSON() const {
    std::ostringstream json;
    json << std::fixed << std::setprecision(3);

    json << "{\n";
    json << "  \"frames\": [\n";

    for (size_t i = 0; i < frame_history_.size(); ++i) {
        const auto& frame = frame_history_[i];
        json << "    {\n";
        json << "      \"frame_number\": " << frame.frame_number << ",\n";
        json << "      \"frame_time_ms\": " << frame.GetFrameTimeMs() << ",\n";
        json << "      \"samples\": [\n";

        for (size_t j = 0; j < frame.samples.size(); ++j) {
            const auto& sample = frame.samples[j];
            json << "        {\n";
            json << "          \"name\": \"" << sample.name << "\",\n";
            json << "          \"duration_ms\": " << sample.GetDurationMs() << ",\n";
            json << "          \"depth\": " << sample.depth << "\n";
            json << "        }";
            if (j < frame.samples.size() - 1) json << ",";
            json << "\n";
        }

        json << "      ]\n";
        json << "    }";
        if (i < frame_history_.size() - 1) json << ",";
        json << "\n";
    }

    json << "  ],\n";
    json << "  \"memory\": {\n";
    json << "    \"current_usage\": " << memory_stats_.current_usage << ",\n";
    json << "    \"peak_usage\": " << memory_stats_.peak_usage << ",\n";
    json << "    \"allocation_count\": " << memory_stats_.allocation_count << "\n";
    json << "  }\n";
    json << "}\n";

    return json.str();
}

void Profiler::ExportToFile(const String& path) const {
    std::ofstream file(path);
    if (file.is_open()) {
        file << ExportToJSON();
    }
}

// ============================================================================
// DebugRenderer
// ============================================================================

namespace {
constexpr float32 kPi = 3.14159265358979323846f;

// Any unit vector perpendicular to `dir`
Math::Vector3 Perpendicular(const Math::Vector3& dir) {
    Math::Vector3 p = Math::Vector3::Cross(dir, Math::Vector3::Up);
    if (p.LengthSquared() < 1e-6f) p = Math::Vector3::Cross(dir, Math::Vector3::Right);
    return p.Normalized();
}
} // namespace

DebugRenderer& DebugRenderer::Instance() {
    static DebugRenderer instance;
    return instance;
}

void DebugRenderer::Clear() {
    lines_.clear();
    labels_.clear();
}

void DebugRenderer::Advance(float32 delta_time) {
    auto expired = [delta_time](auto& item) {
        item.remaining -= delta_time;
        return item.remaining <= 0.0f;
    };
    lines_.erase(std::remove_if(lines_.begin(), lines_.end(), expired), lines_.end());
    labels_.erase(std::remove_if(labels_.begin(), labels_.end(), expired), labels_.end());
}

bool DebugRenderer::ProjectToScreen(const Math::Matrix4& view_projection, const Math::Vector3& position,
                                    float32 screen_width, float32 screen_height, Math::Vector2& out) {
    const Math::Vector4 clip = view_projection * Math::Vector4(position, 1.0f);
    if (clip.w <= 1e-5f) return false;
    const float32 x = clip.x / clip.w;
    const float32 y = clip.y / clip.w;
    // Vulkan clip space: y points down, so NDC maps directly to top-left screen coordinates
    out = Math::Vector2((x * 0.5f + 0.5f) * screen_width, (y * 0.5f + 0.5f) * screen_height);
    return true;
}

void DebugRenderer::DrawLine(const Math::Vector3& start, const Math::Vector3& end,
                             const Math::Vector4& color, float32 duration) {
    if (!enabled_) return;
    lines_.push_back({start, end, color, duration});
}

void DebugRenderer::DrawWireBox(const Math::Vector3& center, const Math::Vector3& size,
                                const Math::Quaternion& rotation, const Math::Vector4& color, float32 duration) {
    const Math::Vector3 half = size * 0.5f;
    Math::Vector3 corners[8];
    for (int i = 0; i < 8; ++i) {
        const Math::Vector3 local((i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z);
        corners[i] = center + rotation.Rotate(local);
    }
    static constexpr int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                          {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& edge : kEdges) {
        DrawLine(corners[edge[0]], corners[edge[1]], color, duration);
    }
}

void DebugRenderer::DrawWireSphere(const Math::Vector3& center, float32 radius,
                                   const Math::Vector4& color, float32 duration) {
    constexpr int kSegments = 24;
    for (int plane = 0; plane < 3; ++plane) {
        for (int i = 0; i < kSegments; ++i) {
            const float32 a1 = static_cast<float32>(i) / kSegments * 2.0f * kPi;
            const float32 a2 = static_cast<float32>(i + 1) / kSegments * 2.0f * kPi;
            auto point = [&](float32 a) {
                const float32 c = std::cos(a) * radius, s = std::sin(a) * radius;
                switch (plane) {
                    case 0: return center + Math::Vector3(c, s, 0);
                    case 1: return center + Math::Vector3(c, 0, s);
                    default: return center + Math::Vector3(0, c, s);
                }
            };
            DrawLine(point(a1), point(a2), color, duration);
        }
    }
}

void DebugRenderer::DrawCapsule(const Math::Vector3& start, const Math::Vector3& end, float32 radius,
                                const Math::Vector4& color, float32 duration) {
    DrawWireSphere(start, radius, color, duration);
    DrawWireSphere(end, radius, color, duration);

    const Math::Vector3 axis = end - start;
    const Math::Vector3 dir = axis.LengthSquared() > 1e-10f ? axis.Normalized() : Math::Vector3::Up;
    const Math::Vector3 right = Perpendicular(dir) * radius;
    const Math::Vector3 forward = Math::Vector3::Cross(right, dir).Normalized() * radius;
    DrawLine(start + right, end + right, color, duration);
    DrawLine(start - right, end - right, color, duration);
    DrawLine(start + forward, end + forward, color, duration);
    DrawLine(start - forward, end - forward, color, duration);
}

void DebugRenderer::DrawArrow(const Math::Vector3& start, const Math::Vector3& end,
                              const Math::Vector4& color, float32 duration) {
    DrawLine(start, end, color, duration);
    const float32 length = (end - start).Length();
    if (length < 1e-5f) return;

    const Math::Vector3 dir = (end - start) / length;
    const Math::Vector3 right = Perpendicular(dir);
    const Math::Vector3 up = Math::Vector3::Cross(right, dir).Normalized();
    const float32 head = length * 0.1f;
    const Math::Vector3 base = end - dir * head;
    DrawLine(end, base + right * head * 0.5f, color, duration);
    DrawLine(end, base - right * head * 0.5f, color, duration);
    DrawLine(end, base + up * head * 0.5f, color, duration);
    DrawLine(end, base - up * head * 0.5f, color, duration);
}

void DebugRenderer::DrawFrustum(const Math::Matrix4& view_projection, const Math::Vector4& color, float32 duration) {
    const Math::Matrix4 inv_vp = view_projection.Inverse();
    // Vulkan NDC: depth 0 (near) to 1 (far)
    const Math::Vector3 ndc[8] = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0},
                                  {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
    Math::Vector3 world[8];
    for (int i = 0; i < 8; ++i) {
        const Math::Vector4 p = inv_vp * Math::Vector4(ndc[i], 1.0f);
        world[i] = Math::Vector3(p.x, p.y, p.z) / p.w;
    }
    for (int i = 0; i < 4; ++i) {
        DrawLine(world[i], world[(i + 1) % 4], color, duration);
        DrawLine(world[4 + i], world[4 + (i + 1) % 4], color, duration);
        DrawLine(world[i], world[4 + i], color, duration);
    }
}

void DebugRenderer::DrawGrid(const Math::Vector3& center, float32 size, uint32 divisions,
                             const Math::Vector4& color, float32 duration) {
    divisions = std::max(1u, divisions);
    const float32 step = size / static_cast<float32>(divisions);
    const float32 half = size * 0.5f;
    for (uint32 i = 0; i <= divisions; ++i) {
        const float32 offset = -half + static_cast<float32>(i) * step;
        DrawLine(center + Math::Vector3(-half, 0, offset), center + Math::Vector3(half, 0, offset), color, duration);
        DrawLine(center + Math::Vector3(offset, 0, -half), center + Math::Vector3(offset, 0, half), color, duration);
    }
}

void DebugRenderer::DrawAxis(const Math::Vector3& position, float32 size, float32 duration) {
    DrawArrow(position, position + Math::Vector3(size, 0, 0), {1, 0, 0, 1}, duration);
    DrawArrow(position, position + Math::Vector3(0, size, 0), {0, 1, 0, 1}, duration);
    DrawArrow(position, position + Math::Vector3(0, 0, size), {0, 0, 1, 1}, duration);
}

void DebugRenderer::DrawText(const Math::Vector3& position, const String& text,
                             const Math::Vector4& color, float32 duration) {
    if (!enabled_) return;
    labels_.push_back({position, text, color, duration});
}

void DebugRenderer::DrawRay(const Math::Vector3& origin, const Math::Vector3& direction, float32 length,
                            const Math::Vector4& color, float32 duration) {
    DrawArrow(origin, origin + direction * length, color, duration);
}

void DebugRenderer::DrawHitPoint(const Math::Vector3& point, const Math::Vector3& normal, float32 duration) {
    DrawWireSphere(point, 0.05f, {1, 0, 0, 1}, duration);
    DrawArrow(point, point + normal * 0.3f, {0, 1, 0, 1}, duration);
}

void DebugRenderer::DrawCollider(const Math::Vector3& position, const Math::Quaternion& rotation,
                                 ColliderShape shape, const Math::Vector3& size, bool is_trigger) {
    const Math::Vector4 color = is_trigger ? Math::Vector4(0, 1, 0, 0.6f) : Math::Vector4(0, 1, 1, 0.6f);
    switch (shape) {
        case ColliderShape::Box:
            DrawWireBox(position, size, rotation, color);
            break;
        case ColliderShape::Sphere:
            DrawWireSphere(position, size.x, color);
            break;
        case ColliderShape::Capsule: {
            const float32 half_segment = std::max(0.0f, size.y * 0.5f - size.x);
            const Math::Vector3 axis = rotation.Rotate(Math::Vector3(0, half_segment, 0));
            DrawCapsule(position - axis, position + axis, size.x, color);
            break;
        }
    }
}

// ============================================================================
// CVar
// ============================================================================

CVar::CVar(const String& name, int32 default_value, const String& description)
    : name_(name), description_(description), type_(CVarType::Int),
      int_value(default_value), default_int(default_value) {
    Console::Instance().RegisterCVar(this);
}

CVar::CVar(const String& name, float32 default_value, const String& description)
    : name_(name), description_(description), type_(CVarType::Float),
      float_value(default_value), default_float(default_value) {
    Console::Instance().RegisterCVar(this);
}

CVar::CVar(const String& name, bool default_value, const String& description)
    : name_(name), description_(description), type_(CVarType::Bool),
      bool_value(default_value), default_bool(default_value) {
    Console::Instance().RegisterCVar(this);
}

CVar::CVar(const String& name, const String& default_value, const String& description)
    : name_(name), description_(description), type_(CVarType::String),
      string_value_(default_value), default_string_(default_value) {
    Console::Instance().RegisterCVar(this);
}

int32 CVar::GetInt() const {
    return type_ == CVarType::Int ? int_value : static_cast<int32>(float_value);
}

float32 CVar::GetFloat() const {
    return type_ == CVarType::Float ? float_value : static_cast<float32>(int_value);
}

bool CVar::GetBool() const {
    return type_ == CVarType::Bool ? bool_value : (int_value != 0);
}

String CVar::GetString() const {
    if (type_ == CVarType::String) return string_value_;
    if (type_ == CVarType::Int) return std::to_string(int_value);
    if (type_ == CVarType::Float) return std::to_string(float_value);
    if (type_ == CVarType::Bool) return bool_value ? "true" : "false";
    return "";
}

void CVar::SetInt(int32 value) {
    if (type_ == CVarType::Int) int_value = value;
    else if (type_ == CVarType::Float) float_value = static_cast<float32>(value);
    else if (type_ == CVarType::Bool) bool_value = (value != 0);
}

void CVar::SetFloat(float32 value) {
    if (type_ == CVarType::Float) float_value = value;
    else if (type_ == CVarType::Int) int_value = static_cast<int32>(value);
}

void CVar::SetBool(bool value) {
    if (type_ == CVarType::Bool) bool_value = value;
    else if (type_ == CVarType::Int) int_value = value ? 1 : 0;
}

void CVar::SetString(const String& value) {
    if (type_ == CVarType::String) {
        string_value_ = value;
    } else if (type_ == CVarType::Int) {
        int_value = std::stoi(value);
    } else if (type_ == CVarType::Float) {
        float_value = std::stof(value);
    } else if (type_ == CVarType::Bool) {
        bool_value = (value == "true" || value == "1");
    }
}

void CVar::Reset() {
    switch (type_) {
        case CVarType::Int: int_value = default_int; break;
        case CVarType::Float: float_value = default_float; break;
        case CVarType::Bool: bool_value = default_bool; break;
        case CVarType::String: string_value_ = default_string_; break;
    }
}

// ============================================================================
// Console
// ============================================================================

Console& Console::Instance() {
    static Console instance;
    return instance;
}

void Console::Initialize() {
    if (initialized_) return;

    RegisterBuiltInCommands();
    initialized_ = true;

    Print("DRX Engine Console initialized");
}

void Console::Shutdown() {
    commands_.clear();
    command_help_.clear();
    cvars_.clear();
    command_history_.clear();
    output_.clear();
    initialized_ = false;
}

void Console::Execute(const String& command) {
    if (command.empty()) return;

    command_history_.push_back(command);

    Array<String> args = ParseCommand(command);
    if (args.empty()) return;

    String cmd_name = args[0];
    args.erase(args.begin());

    // Check for CVar first
    auto cvar_it = cvars_.find(cmd_name);
    if (cvar_it != cvars_.end()) {
        if (args.empty()) {
            // Print current value
            Print(cmd_name + " = " + cvar_it->second->GetString());
        } else {
            // Set value
            cvar_it->second->SetString(args[0]);
            Print(cmd_name + " set to " + args[0]);
        }
        return;
    }

    // Check for command
    auto cmd_it = commands_.find(cmd_name);
    if (cmd_it != commands_.end()) {
        cmd_it->second(args);
        return;
    }

    PrintError("Unknown command: " + cmd_name);
}

void Console::ExecuteFile(const String& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        PrintError("Failed to open file: " + path);
        return;
    }

    String line;
    while (std::getline(file, line)) {
        if (!line.empty() && line[0] != '#') {
            Execute(line);
        }
    }
}

void Console::RegisterCommand(const String& name, ConsoleCommandFunc func, const String& help) {
    commands_[name] = func;
    command_help_[name] = help;
}

void Console::UnregisterCommand(const String& name) {
    commands_.erase(name);
    command_help_.erase(name);
}

void Console::RegisterCVar(CVar* cvar) {
    cvars_[cvar->GetName()] = cvar;
}

void Console::UnregisterCVar(const String& name) {
    cvars_.erase(name);
}

CVar* Console::GetCVar(const String& name) {
    auto it = cvars_.find(name);
    return it != cvars_.end() ? it->second : nullptr;
}

void Console::Print(const String& message) {
    output_.push_back(message);
    if (output_.size() > max_output_lines_) {
        output_.erase(output_.begin());
    }
}

void Console::PrintWarning(const String& message) {
    Print("[Warning] " + message);
}

void Console::PrintError(const String& message) {
    Print("[Error] " + message);
}

void Console::ClearOutput() {
    output_.clear();
}

Array<String> Console::GetAutoComplete(const String& partial) const {
    Array<String> results;

    for (const auto& [name, _] : commands_) {
        if (name.find(partial) == 0) {
            results.push_back(name);
        }
    }

    for (const auto& [name, _] : cvars_) {
        if (name.find(partial) == 0) {
            results.push_back(name);
        }
    }

    return results;
}

void Console::Cmd_Help(const Array<String>& args) {
    if (args.empty()) {
        Print("Available commands:");
        for (const auto& [name, help] : command_help_) {
            Print("  " + name + " - " + help);
        }
    } else {
        auto it = command_help_.find(args[0]);
        if (it != command_help_.end()) {
            Print(args[0] + ": " + it->second);
        } else {
            PrintError("Unknown command: " + args[0]);
        }
    }
}

void Console::Cmd_ListCVars(const Array<String>& /*args*/) {
    Print("Console Variables:");
    for (const auto& [name, cvar] : cvars_) {
        Print("  " + name + " = " + cvar->GetString() + " (" + cvar->GetDescription() + ")");
    }
}

void Console::Cmd_Set(const Array<String>& args) {
    if (args.size() < 2) {
        PrintError("Usage: set <cvar> <value>");
        return;
    }

    CVar* cvar = GetCVar(args[0]);
    if (cvar) {
        cvar->SetString(args[1]);
        Print(args[0] + " set to " + args[1]);
    } else {
        PrintError("Unknown CVar: " + args[0]);
    }
}

void Console::Cmd_Get(const Array<String>& args) {
    if (args.empty()) {
        PrintError("Usage: get <cvar>");
        return;
    }

    CVar* cvar = GetCVar(args[0]);
    if (cvar) {
        Print(args[0] + " = " + cvar->GetString());
    } else {
        PrintError("Unknown CVar: " + args[0]);
    }
}

void Console::Cmd_Exec(const Array<String>& args) {
    if (args.empty()) {
        PrintError("Usage: exec <file>");
        return;
    }
    ExecuteFile(args[0]);
}

void Console::Cmd_Clear(const Array<String>& /*args*/) {
    ClearOutput();
}

void Console::RegisterBuiltInCommands() {
    RegisterCommand("help", [this](const Array<String>& args) { Cmd_Help(args); },
                    "Display help information");
    RegisterCommand("listcvars", [this](const Array<String>& args) { Cmd_ListCVars(args); },
                    "List all console variables");
    RegisterCommand("set", [this](const Array<String>& args) { Cmd_Set(args); },
                    "Set a console variable");
    RegisterCommand("get", [this](const Array<String>& args) { Cmd_Get(args); },
                    "Get a console variable value");
    RegisterCommand("exec", [this](const Array<String>& args) { Cmd_Exec(args); },
                    "Execute a config file");
    RegisterCommand("clear", [this](const Array<String>& args) { Cmd_Clear(args); },
                    "Clear console output");
}

Array<String> Console::ParseCommand(const String& command) const {
    Array<String> args;
    std::istringstream stream(command);
    String token;
    bool in_quotes = false;
    String current;

    while (stream >> token) {
        if (!in_quotes && token[0] == '"') {
            in_quotes = true;
            current = token.substr(1);
        } else if (in_quotes) {
            if (token.back() == '"') {
                current += " " + token.substr(0, token.size() - 1);
                args.push_back(current);
                current.clear();
                in_quotes = false;
            } else {
                current += " " + token;
            }
        } else {
            args.push_back(token);
        }
    }

    if (!current.empty()) {
        args.push_back(current);
    }

    return args;
}

// ============================================================================
// StatsOverlay
// ============================================================================

StatsOverlay& StatsOverlay::Instance() {
    static StatsOverlay instance;
    return instance;
}

void StatsOverlay::Update(float32 delta_time) {
    frame_times_.push_back(delta_time * 1000.0f);
    if (frame_times_.size() > 100) {
        frame_times_.erase(frame_times_.begin());
    }

    fps_update_timer_ += delta_time;
    if (fps_update_timer_ >= 0.5f) {
        fps_update_timer_ = 0;

        float32 avg = 0;
        for (float32 t : frame_times_) {
            avg += t;
        }
        avg /= frame_times_.size();
        current_fps_ = 1000.0f / avg;
    }
}

void StatsOverlay::Render() {
    if (!visible_) return;

    // Would render using UI system
    // Display FPS, frame time, memory usage, draw calls, etc.
}

} // namespace DRX::Debug

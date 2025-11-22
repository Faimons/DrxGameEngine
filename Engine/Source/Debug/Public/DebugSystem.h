/**
 * DRX Engine - Debug & Profiler System
 * Performance profiling, debug rendering, and diagnostics
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Vector4.h"
#include "Core/Public/Math/Matrix4.h"

#include <chrono>
#include <mutex>

namespace DRX::Debug {

/**
 * Profile sample data
 */
struct ProfileSample {
    String name;
    uint64 start_time;
    uint64 end_time;
    uint32 depth;
    uint32 parent_index;
    uint32 call_count;

    float64 GetDurationMs() const {
        return (end_time - start_time) / 1000000.0;
    }
};

/**
 * Profile frame data
 */
struct ProfileFrame {
    uint64 frame_number;
    uint64 frame_start;
    uint64 frame_end;
    Array<ProfileSample> samples;

    float64 GetFrameTimeMs() const {
        return (frame_end - frame_start) / 1000000.0;
    }
};

/**
 * Memory stats
 */
struct MemoryStats {
    uint64 total_allocated = 0;
    uint64 total_freed = 0;
    uint64 current_usage = 0;
    uint64 peak_usage = 0;
    uint32 allocation_count = 0;
    uint32 deallocation_count = 0;

    HashMap<String, uint64> category_usage;
};

/**
 * GPU timing query
 */
struct GPUTimerQuery {
    String name;
    uint64 start_query;
    uint64 end_query;
    float64 duration_ms;
};

/**
 * Scoped profiler - RAII profile scope
 */
class ScopedProfiler {
public:
    ScopedProfiler(const char* name);
    ~ScopedProfiler();

private:
    uint32 sample_index_;
};

/**
 * Profiler system
 */
class Profiler {
public:
    static Profiler& Instance();

    void BeginFrame();
    void EndFrame();

    // CPU Profiling
    uint32 BeginSample(const char* name);
    void EndSample(uint32 index);

    // GPU Profiling
    void BeginGPUTimer(const char* name);
    void EndGPUTimer(const char* name);

    // Memory tracking
    void TrackAllocation(void* ptr, uint64 size, const char* category = "General");
    void TrackDeallocation(void* ptr);

    // Data access
    const ProfileFrame& GetCurrentFrame() const { return current_frame_; }
    const ProfileFrame& GetPreviousFrame() const { return previous_frame_; }
    const Array<ProfileFrame>& GetFrameHistory() const { return frame_history_; }
    const MemoryStats& GetMemoryStats() const { return memory_stats_; }

    // Statistics
    float64 GetAverageFrameTime() const;
    float64 GetAverageFPS() const;
    float64 GetSampleAverageTime(const String& name) const;

    // Settings
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }
    void SetHistorySize(uint32 size) { max_history_size_ = size; }

    // Export
    String ExportToJSON() const;
    void ExportToFile(const String& path) const;

private:
    Profiler() = default;

    uint64 GetTimestamp() const;

    bool enabled_ = true;
    uint64 frame_number_ = 0;
    uint32 current_depth_ = 0;

    ProfileFrame current_frame_;
    ProfileFrame previous_frame_;
    Array<ProfileFrame> frame_history_;
    uint32 max_history_size_ = 120;

    Array<uint32> sample_stack_;

    MemoryStats memory_stats_;
    HashMap<void*, std::pair<uint64, String>> allocation_map_;

    Array<GPUTimerQuery> gpu_timers_;

    mutable std::mutex mutex_;
};

// Profiling macros
#define DRX_PROFILE_SCOPE(name) DRX::Debug::ScopedProfiler _profiler_##__LINE__(name)
#define DRX_PROFILE_FUNCTION() DRX_PROFILE_SCOPE(__FUNCTION__)

/**
 * Debug draw types
 */
enum class DebugDrawType : uint32 {
    Line,
    Box,
    Sphere,
    Capsule,
    Arrow,
    Text,
    Frustum,
    Grid,
    Axis
};

/**
 * Debug draw command
 */
struct DebugDrawCommand {
    DebugDrawType type;
    Math::Vector3 position;
    Math::Vector3 size;
    Math::Quaternion rotation;
    Math::Vector4 color;
    float32 duration;
    bool depth_test;
    String text;
};

/**
 * Debug renderer - draws debug shapes
 */
class DebugRenderer {
public:
    static DebugRenderer& Instance();

    void Initialize();
    void Shutdown();
    void Render(const Math::Matrix4& view_projection);
    void Clear();

    // Draw functions
    void DrawLine(const Math::Vector3& start, const Math::Vector3& end,
                  const Math::Vector4& color = {1, 1, 1, 1}, float32 duration = 0,
                  bool depth_test = true);

    void DrawBox(const Math::Vector3& center, const Math::Vector3& size,
                 const Math::Vector4& color = {1, 1, 1, 1}, float32 duration = 0,
                 bool depth_test = true);

    void DrawWireBox(const Math::Vector3& center, const Math::Vector3& size,
                     const Math::Quaternion& rotation = {},
                     const Math::Vector4& color = {1, 1, 1, 1}, float32 duration = 0,
                     bool depth_test = true);

    void DrawSphere(const Math::Vector3& center, float32 radius,
                    const Math::Vector4& color = {1, 1, 1, 1}, float32 duration = 0,
                    bool depth_test = true);

    void DrawWireSphere(const Math::Vector3& center, float32 radius,
                        const Math::Vector4& color = {1, 1, 1, 1}, float32 duration = 0,
                        bool depth_test = true);

    void DrawCapsule(const Math::Vector3& start, const Math::Vector3& end, float32 radius,
                     const Math::Vector4& color = {1, 1, 1, 1}, float32 duration = 0,
                     bool depth_test = true);

    void DrawArrow(const Math::Vector3& start, const Math::Vector3& end,
                   const Math::Vector4& color = {1, 1, 1, 1}, float32 duration = 0,
                   bool depth_test = true);

    void DrawFrustum(const Math::Matrix4& view_projection,
                     const Math::Vector4& color = {1, 1, 1, 1}, float32 duration = 0);

    void DrawGrid(const Math::Vector3& center, float32 size, uint32 divisions,
                  const Math::Vector4& color = {0.5f, 0.5f, 0.5f, 1}, float32 duration = 0);

    void DrawAxis(const Math::Vector3& position, float32 size = 1.0f, float32 duration = 0);

    void DrawText(const Math::Vector3& position, const String& text,
                  const Math::Vector4& color = {1, 1, 1, 1}, float32 duration = 0);

    // Ray/collision visualization
    void DrawRay(const Math::Vector3& origin, const Math::Vector3& direction, float32 length,
                 const Math::Vector4& color = {1, 1, 0, 1}, float32 duration = 0);

    void DrawHitPoint(const Math::Vector3& point, const Math::Vector3& normal,
                      float32 duration = 0);

    // Physics debug
    void DrawCollider(const Math::Vector3& position, const Math::Quaternion& rotation,
                      int32 shape_type, const Math::Vector3& size, bool is_trigger = false);

    // Settings
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

private:
    DebugRenderer() = default;

    void UpdateDurations(float32 delta_time);
    void BuildLineGeometry();

    bool enabled_ = true;
    Array<DebugDrawCommand> commands_;
    Array<DebugDrawCommand> persistent_commands_;

    // Rendering resources
    // VkBuffer line_vertex_buffer_;
    // VkPipeline debug_pipeline_;
};

/**
 * Console variable types
 */
enum class CVarType : uint32 {
    Int,
    Float,
    Bool,
    String
};

/**
 * Console variable
 */
class CVar {
public:
    CVar(const String& name, int32 default_value, const String& description = "");
    CVar(const String& name, float32 default_value, const String& description = "");
    CVar(const String& name, bool default_value, const String& description = "");
    CVar(const String& name, const String& default_value, const String& description = "");

    const String& GetName() const { return name_; }
    const String& GetDescription() const { return description_; }
    CVarType GetType() const { return type_; }

    int32 GetInt() const;
    float32 GetFloat() const;
    bool GetBool() const;
    String GetString() const;

    void SetInt(int32 value);
    void SetFloat(float32 value);
    void SetBool(bool value);
    void SetString(const String& value);

    void Reset();

private:
    String name_;
    String description_;
    CVarType type_;

    union {
        int32 int_value;
        float32 float_value;
        bool bool_value;
    };
    String string_value_;

    union {
        int32 default_int;
        float32 default_float;
        bool default_bool;
    };
    String default_string_;
};

/**
 * Console command callback
 */
using ConsoleCommandFunc = std::function<void(const Array<String>& args)>;

/**
 * Debug console system
 */
class Console {
public:
    static Console& Instance();

    void Initialize();
    void Shutdown();

    // Command execution
    void Execute(const String& command);
    void ExecuteFile(const String& path);

    // Command registration
    void RegisterCommand(const String& name, ConsoleCommandFunc func, const String& help = "");
    void UnregisterCommand(const String& name);

    // CVar registration
    void RegisterCVar(CVar* cvar);
    void UnregisterCVar(const String& name);
    CVar* GetCVar(const String& name);

    // Output
    void Print(const String& message);
    void PrintWarning(const String& message);
    void PrintError(const String& message);

    // History
    const Array<String>& GetHistory() const { return command_history_; }
    const Array<String>& GetOutput() const { return output_; }
    void ClearOutput();

    // Auto-complete
    Array<String> GetAutoComplete(const String& partial) const;

    // Built-in commands
    void Cmd_Help(const Array<String>& args);
    void Cmd_ListCVars(const Array<String>& args);
    void Cmd_Set(const Array<String>& args);
    void Cmd_Get(const Array<String>& args);
    void Cmd_Exec(const Array<String>& args);
    void Cmd_Clear(const Array<String>& args);
    void Cmd_Quit(const Array<String>& args);

private:
    Console() = default;

    void RegisterBuiltInCommands();
    Array<String> ParseCommand(const String& command) const;

    HashMap<String, ConsoleCommandFunc> commands_;
    HashMap<String, String> command_help_;
    HashMap<String, CVar*> cvars_;

    Array<String> command_history_;
    Array<String> output_;
    uint32 max_output_lines_ = 1000;

    bool initialized_ = false;
};

/**
 * Stats overlay
 */
class StatsOverlay {
public:
    static StatsOverlay& Instance();

    void Update(float32 delta_time);
    void Render();

    void SetVisible(bool visible) { visible_ = visible; }
    bool IsVisible() const { return visible_; }

    void SetShowFPS(bool show) { show_fps_ = show; }
    void SetShowFrameTime(bool show) { show_frame_time_ = show; }
    void SetShowMemory(bool show) { show_memory_ = show; }
    void SetShowDrawCalls(bool show) { show_draw_calls_ = show; }

private:
    StatsOverlay() = default;

    bool visible_ = false;
    bool show_fps_ = true;
    bool show_frame_time_ = true;
    bool show_memory_ = true;
    bool show_draw_calls_ = true;

    float32 fps_update_timer_ = 0;
    float32 current_fps_ = 0;
    Array<float32> frame_times_;
};

} // namespace DRX::Debug

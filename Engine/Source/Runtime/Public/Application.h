#pragma once

/**
 * DRX Engine - Application framework
 * Owns window, graphics device, scene renderer, input, UI and audio, and runs the game loop:
 *
 *   per frame:  input -> OnFixedUpdate (0..n times, fixed step) -> OnUpdate -> OnRender -> OnUI -> present
 *
 * A game derives from Application and overrides the On* hooks.
 *
 * Command line:
 *   --frames N          exit after N frames (automated runs)
 *   --screenshot FILE   save a PNG of the last frame before exiting
 *   --width W --height H
 *   --fullscreen
 *   --no-vsync
 *   --validation / --no-validation
 *   --config FILE       game configuration (JSON), available through GetConfig()
 *   --set KEY=VALUE     overrides a configuration value (repeatable)
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Config.h"
#include "HAL/Public/Window.h"
#include "HAL/Public/GraphicsDevice.h"
#include "Renderer/Public/SceneRenderer.h"
#include "Input/Public/InputSystem.h"
#include "UI/Public/UISystem.h"

namespace DRX::Runtime {

struct ApplicationConfig {
    String title = "DRX Engine";
    uint32 width = 1600;
    uint32 height = 900;
    bool fullscreen = false;
    bool vsync = true;
#ifdef DRX_DEBUG
    bool validation = true;
#else
    bool validation = false;
#endif
    float32 fixed_timestep = 1.0f / 60.0f;
    uint32 max_fixed_steps_per_frame = 8;   // avoids the spiral of death after long hitches
    float32 max_frame_time = 0.25f;
    bool enable_audio = true;
    String config_path;                     // optional game configuration
    uint64 exit_after_frames = 0;           // 0 = run until closed
    String screenshot_on_exit;
    Renderer::SceneRendererConfig renderer;
};

class Application {
public:
    explicit Application(ApplicationConfig config = {});
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Parses the command line, runs the loop and returns the process exit code
    int Run(int argc = 0, char** argv = nullptr);
    void RequestQuit() { quit_requested_ = true; }

    // Systems
    HAL::Window& GetWindow() { return *window_; }
    HAL::GraphicsDevice& GetDevice() { return *device_; }
    Renderer::SceneRenderer& GetRenderer() { return *renderer_; }
    Input::InputSystem& GetInput() { return Input::InputSystem::Instance(); }
    UI::UIContext& GetUI() { return UI::UIContext::Instance(); }
    Config& GetConfig() { return game_config_; }
    const ApplicationConfig& GetAppConfig() const { return config_; }
    bool IsAudioAvailable() const { return audio_available_; }

    // Camera and lighting used for the next rendered frame
    Renderer::CameraParams& GetCamera() { return camera_; }
    Renderer::EnvironmentSettings& GetEnvironment() { return environment_; }
    float32 GetAspectRatio() const;

    // Timing
    float64 GetTime() const { return time_; }
    uint64 GetFrameCount() const { return frame_count_; }
    float32 GetFPS() const { return fps_; }
    float32 GetFixedTimestep() const { return config_.fixed_timestep; }
    float32 GetInterpolationAlpha() const { return interpolation_alpha_; }   // for rendering between fixed steps

    // Mouse capture for first-person control
    void SetMouseCaptured(bool captured);
    bool IsMouseCaptured() const { return mouse_captured_; }

    // Saves a PNG of the next frame (also bound to F12)
    void TakeScreenshot(const String& path = {});

protected:
    virtual bool OnInitialize() { return true; }
    virtual void OnShutdown() {}
    virtual void OnFixedUpdate(float32 /*fixed_dt*/) {}
    virtual void OnUpdate(float32 /*dt*/) {}
    virtual void OnRender(Renderer::SceneRenderer& /*renderer*/) {}
    virtual void OnUI(UI::UIContext& /*ui*/) {}
    virtual void OnResize(uint32 /*width*/, uint32 /*height*/) {}

private:
    bool ParseArguments(int argc, char** argv);
    bool InitializeSystems();
    void ShutdownSystems();
    void RunFrame();
    void ForwardInputToUI();

    ApplicationConfig config_;
    Config game_config_;

    UniquePtr<HAL::Window> window_;
    UniquePtr<HAL::GraphicsDevice> device_;
    UniquePtr<Renderer::SceneRenderer> renderer_;

    Renderer::CameraParams camera_;
    Renderer::EnvironmentSettings environment_;

    bool audio_available_ = false;
    bool quit_requested_ = false;
    bool mouse_captured_ = false;
    bool fullscreen_ = false;

    float64 time_ = 0.0;
    float64 last_frame_time_ = 0.0;
    float64 accumulator_ = 0.0;
    float32 interpolation_alpha_ = 0.0f;
    uint64 frame_count_ = 0;
    float32 fps_ = 0.0f;
    float32 fps_timer_ = 0.0f;
    uint32 fps_frames_ = 0;
    float32 config_reload_timer_ = 0.0f;
    String pending_screenshot_;
};

} // namespace DRX::Runtime

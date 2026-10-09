/**
 * DRX Engine - Application framework implementation
 */

#include "Runtime/Public/Application.h"
#include "Core/Public/Log.h"
#include "Core/Public/Platform.h"
#include "Audio/Public/AudioSystem.h"
#include "Particles/Public/ParticleSystem.h"
#include "Debug/Public/DebugSystem.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>

namespace DRX::Runtime {

namespace {

constexpr float32 kDefaultFov = 1.3962634f;  // 80 degrees

bool ParseUInt(const char* text, uint64& out) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);
    if (end == text || *end != '\0') return false;
    out = value;
    return true;
}

String TimestampedScreenshotName() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef DRX_PLATFORM_WINDOWS
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "screenshot_%Y%m%d_%H%M%S.png", &local);
    return buffer;
}

} // namespace

Application::Application(ApplicationConfig config)
    : config_(std::move(config)) {}

Application::~Application() {
    ShutdownSystems();
}

void Application::SetFixedTimestep(float32 seconds) {
    config_.fixed_timestep = std::clamp(seconds, 0.001f, 0.2f);
    accumulator_ = 0.0;
}

float32 Application::GetAspectRatio() const {
    if (!device_) return 16.0f / 9.0f;
    const VkExtent2D extent = device_->GetSwapChainExtent();
    return extent.height > 0 ? static_cast<float32>(extent.width) / static_cast<float32>(extent.height) : 1.0f;
}

bool Application::ParseArguments(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const String arg = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };

        uint64 number = 0;
        if (arg == "--frames") {
            const char* v = next();
            if (!v || !ParseUInt(v, config_.exit_after_frames)) {
                DRX_LOG_ERROR("--frames expects a number");
                return false;
            }
        } else if (arg == "--screenshot") {
            const char* v = next();
            if (!v) { DRX_LOG_ERROR("--screenshot expects a file name"); return false; }
            config_.screenshot_on_exit = v;
        } else if (arg == "--width" || arg == "--height") {
            const char* v = next();
            if (!v || !ParseUInt(v, number) || number < 64 || number > 16384) {
                DRX_LOG_ERROR("{} expects a size between 64 and 16384", arg);
                return false;
            }
            (arg == "--width" ? config_.width : config_.height) = static_cast<uint32>(number);
        } else if (arg == "--fullscreen") {
            config_.fullscreen = true;
        } else if (arg == "--no-vsync") {
            config_.vsync = false;
        } else if (arg == "--validation") {
            config_.validation = true;
        } else if (arg == "--no-validation") {
            config_.validation = false;
        } else if (arg == "--config") {
            const char* v = next();
            if (!v) { DRX_LOG_ERROR("--config expects a file name"); return false; }
            config_.config_path = v;
        } else if (arg == "--set") {
            const char* v = next();
            const String assignment = v ? v : "";
            const size_t eq = assignment.find('=');
            if (eq == String::npos || eq == 0) {
                DRX_LOG_ERROR("--set expects key=value");
                return false;
            }
            game_config_.AddOverride(assignment.substr(0, eq), assignment.substr(eq + 1));
        } else {
            DRX_LOG_WARNING("Ignoring unknown argument '{}'", arg);
        }
    }
    return true;
}

int Application::Run(int argc, char** argv) {
    if (!ParseArguments(argc, argv)) {
        return 2;
    }

    if (!InitializeSystems()) {
        ShutdownSystems();
        return 1;
    }

    if (!OnInitialize()) {
        DRX_LOG_FATAL("Application initialization failed");
        OnShutdown();
        ShutdownSystems();
        return 1;
    }

    DRX_LOG_INFO("Entering main loop");
    last_frame_time_ = Platform::GetTimeSeconds();
    while (!quit_requested_ && !window_->ShouldClose()) {
        RunFrame();
        if (config_.exit_after_frames > 0 && frame_count_ >= config_.exit_after_frames) {
            quit_requested_ = true;
        }
    }
    DRX_LOG_INFO("Leaving main loop after {} frames", frame_count_);

    device_->WaitIdle();
    OnShutdown();
    ShutdownSystems();
    return 0;
}

bool Application::InitializeSystems() {
    if (!Platform::Initialize()) {
        DRX_LOG_FATAL("Platform initialization failed");
        return false;
    }

    if (!config_.config_path.empty()) {
        if (!game_config_.LoadFromFile(config_.config_path)) {
            DRX_LOG_FATAL("Could not load configuration {}", config_.config_path);
            return false;
        }
        game_config_.LogAll();
    }

    HAL::WindowConfig window_config;
    window_config.title = config_.title;
    window_config.width = config_.width;
    window_config.height = config_.height;
    window_config.vsync = config_.vsync;
    window_config.fullscreen = config_.fullscreen;

    window_ = MakeUnique<HAL::Window>();
    if (!window_->Initialize(window_config)) {
        DRX_LOG_FATAL("Failed to create window");
        return false;
    }
    fullscreen_ = config_.fullscreen;

    HAL::GraphicsConfig graphics_config;
    graphics_config.enable_validation = config_.validation;
    graphics_config.vsync = config_.vsync;

    device_ = MakeUnique<HAL::GraphicsDevice>();
    if (!device_->Initialize(window_.get(), graphics_config)) {
        DRX_LOG_FATAL("Failed to initialize Vulkan");
        return false;
    }

    renderer_ = MakeUnique<Renderer::SceneRenderer>();
    if (!renderer_->Initialize(device_.get(), config_.renderer)) {
        DRX_LOG_FATAL("Failed to initialize renderer");
        return false;
    }

    if (!GetInput().Initialize(window_->GetNativeHandle())) {
        DRX_LOG_FATAL("Failed to initialize input");
        return false;
    }

    // UI text input arrives through the character callback
    GetInput().SetCharCallback([](uint32 codepoint) { UI::UIContext::Instance().AddCharacter(codepoint); });

    UI::UIContext& ui = GetUI();
    ui.Initialize(static_cast<float32>(window_->GetWidth()), static_cast<float32>(window_->GetHeight()));
    Renderer::SceneRenderer* renderer = renderer_.get();
    ui.SetTextMeasureFunc([renderer](const String& text, float32 size) { return renderer->MeasureText(text, size); });

    window_->SetResizeCallback([this](const HAL::ResizeEvent& event) {
        if (event.width == 0 || event.height == 0) return;
        device_->NotifyResized();
        GetUI().SetScreenSize(static_cast<float32>(event.width), static_cast<float32>(event.height));
        OnResize(event.width, event.height);
    });

    if (config_.enable_audio) {
        audio_available_ = Audio::AudioSystem::Instance().Initialize();
        if (!audio_available_) {
            DRX_LOG_WARNING("Audio unavailable - continuing without sound");
        }
    }

    Debug::Console& console = Debug::Console::Instance();
    console.Initialize();
    console.RegisterCommand("quit", [this](const Array<String>&) { RequestQuit(); }, "Quit the application");

    Particles::ParticleManager::Instance().Initialize();

    camera_ = Renderer::CameraParams::LookAt(Math::Vector3(0, 2, 5), Math::Vector3::Zero, kDefaultFov, GetAspectRatio());
    return true;
}

void Application::ShutdownSystems() {
    Debug::Console::Instance().UnregisterCommand("quit");   // the handler refers to this application
    Debug::DebugRenderer::Instance().Clear();
    Particles::ParticleManager::Instance().Shutdown();

    if (audio_available_) {
        Audio::AudioSystem::Instance().Shutdown();
        audio_available_ = false;
    }

    if (window_) {
        GetInput().Shutdown();
        GetUI().Shutdown();
    }

    if (renderer_) {
        renderer_->Shutdown();
        renderer_.reset();
    }
    if (device_) {
        device_->Shutdown();
        device_.reset();
    }
    if (window_) {
        window_->Shutdown();
        window_.reset();
        HAL::Window::ShutdownGLFW();
        Platform::Shutdown();
    }
}

void Application::SetMouseCaptured(bool captured) {
    mouse_captured_ = captured;
    GetInput().SetCursorMode(!captured, captured);
}

void Application::TakeScreenshot(const String& path) {
    pending_screenshot_ = path.empty() ? TimestampedScreenshotName() : path;
}

void Application::ForwardInputToUI() {
    using Input::KeyCode;
    using Input::MouseButton;

    UI::UIContext& ui = GetUI();
    Input::InputSystem& input = GetInput();

    // While the mouse drives the camera the UI must not react to it
    if (mouse_captured_) {
        ui.SetMousePosition(-1e6f, -1e6f);
        for (int32 b = 0; b < 3; ++b) ui.SetMouseButton(b, false);
    } else {
        const Math::Vector2 mouse = input.GetMousePosition();
        ui.SetMousePosition(mouse.x, mouse.y);
        ui.SetMouseButton(0, input.IsMouseButtonDown(MouseButton::Left));
        ui.SetMouseButton(1, input.IsMouseButtonDown(MouseButton::Right));
        ui.SetMouseButton(2, input.IsMouseButtonDown(MouseButton::Middle));
        ui.SetMouseScroll(input.GetMouseScroll().y);
    }

    if (input.IsKeyPressed(KeyCode::Backspace)) ui.SetKeyState(UI::UIKey::Backspace, true);
    if (input.IsKeyPressed(KeyCode::Enter) || input.IsKeyPressed(KeyCode::KeypadEnter)) ui.SetKeyState(UI::UIKey::Enter, true);
    if (input.IsKeyPressed(KeyCode::Escape)) ui.SetKeyState(UI::UIKey::Escape, true);
}

void Application::RunFrame() {
    Input::InputSystem& input = GetInput();

    input.BeginFrame();
    window_->PollEvents();
    input.Update();

    const float64 now = Platform::GetTimeSeconds();
    const auto frame_time = static_cast<float32>(std::clamp(now - last_frame_time_, 0.0, static_cast<float64>(config_.max_frame_time)));
    last_frame_time_ = now;
    time_ += frame_time;

    // Engine shortcuts
    if (input.IsKeyPressed(Input::KeyCode::F11)) {
        fullscreen_ = !fullscreen_;
        window_->SetFullscreen(fullscreen_);
    }
    if (input.IsKeyPressed(Input::KeyCode::F12)) {
        TakeScreenshot();
    }

    // Hot-reload the game configuration roughly twice per second
    if (!game_config_.GetPath().empty()) {
        config_reload_timer_ += frame_time;
        if (config_reload_timer_ >= 0.5f) {
            config_reload_timer_ = 0.0f;
            game_config_.ReloadIfChanged();
        }
    }

    // Debug shapes from the previous frame have been drawn; expire them before new ones are added
    Debug::DebugRenderer::Instance().Advance(frame_time);

    // Fixed-step simulation
    accumulator_ += frame_time;
    uint32 steps = 0;
    while (accumulator_ >= config_.fixed_timestep && steps < config_.max_fixed_steps_per_frame) {
        OnFixedUpdate(config_.fixed_timestep);
        accumulator_ -= config_.fixed_timestep;
        ++steps;
    }
    if (steps == config_.max_fixed_steps_per_frame) {
        accumulator_ = 0.0;  // drop the backlog instead of falling further behind
    }
    interpolation_alpha_ = static_cast<float32>(accumulator_ / config_.fixed_timestep);

    OnUpdate(frame_time);
    Particles::ParticleManager::Instance().Update(frame_time);
    if (audio_available_) {
        // The listener follows the camera (view matrix rows hold the camera basis)
        const Math::Matrix4& view = camera_.view;
        Audio::AudioListener& listener = Audio::AudioSystem::Instance().GetListener();
        listener.SetPosition(camera_.position);
        listener.SetOrientation(Math::Vector3(-view.m[0][2], -view.m[1][2], -view.m[2][2]),
                                Math::Vector3(view.m[0][1], view.m[1][1], view.m[2][1]));
        Audio::AudioSystem::Instance().Update(frame_time);
    }

    // FPS counter
    fps_timer_ += frame_time;
    ++fps_frames_;
    if (fps_timer_ >= 0.5f) {
        fps_ = static_cast<float32>(fps_frames_) / fps_timer_;
        fps_timer_ = 0.0f;
        fps_frames_ = 0;
    }

    ++frame_count_;

    if (window_->IsMinimized()) {
        Platform::Sleep(10);
        return;
    }

    const bool last_frame = config_.exit_after_frames > 0 && frame_count_ >= config_.exit_after_frames;
    if (last_frame && !config_.screenshot_on_exit.empty()) {
        pending_screenshot_ = config_.screenshot_on_exit;
    }

    if (!device_->BeginFrame()) {
        return;  // swap chain out of date; it has been recreated
    }

    renderer_->BeginFrame(camera_, environment_);
    OnRender(*renderer_);

    const Debug::DebugRenderer& debug = Debug::DebugRenderer::Instance();
    for (const auto& line : debug.GetLines()) {
        renderer_->DrawLine(line.start, line.end, line.color);
    }

    UI::UIContext& ui = GetUI();
    ForwardInputToUI();
    ui.BeginFrame();
    OnUI(ui);
    if (!debug.GetLabels().empty()) {
        const Math::Matrix4 view_projection = camera_.projection * camera_.view;
        for (const auto& label : debug.GetLabels()) {
            Math::Vector2 screen;
            if (Debug::DebugRenderer::ProjectToScreen(view_projection, label.position, ui.GetScreenWidth(),
                                                      ui.GetScreenHeight(), screen)) {
                ui.DrawText(label.text, screen.x - ui.GetTextWidth(label.text) * 0.5f, screen.y, label.color);
            }
        }
    }
    ui.EndFrame();
    renderer_->DrawUI(ui.GetDrawCommands(), ui.GetScreenWidth(), ui.GetScreenHeight());

    if (!pending_screenshot_.empty()) {
        renderer_->RequestScreenshot(pending_screenshot_);
        pending_screenshot_.clear();
    }

    renderer_->Render();
    device_->EndFrame();

    if (renderer_->HasPendingScreenshot()) {
        renderer_->FinishScreenshot();
    }
}

} // namespace DRX::Runtime

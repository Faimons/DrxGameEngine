/**
 * DRX Engine - Main Application Entry Point
 * Demonstrates basic engine functionality
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Platform.h"
#include "Core/Public/Log.h"
#include "Core/Public/Assert.h"
#include "HAL/Public/Window.h"
#include "HAL/Public/GraphicsDevice.h"
#include "Renderer/Public/Renderer.h"
#include "Scene/Public/Entity.h"

#include <iostream>

using namespace DRX;

class Application {
public:
    bool Initialize() {
        DRX_LOG_INFO("DRX Engine v1.0.0 starting...");

        // Initialize platform
        if (!Platform::Initialize()) {
            DRX_LOG_ERROR("Failed to initialize platform");
            return false;
        }

        // Create window
        HAL::WindowConfig window_config;
        window_config.title = "DRX Engine - Triangle Demo";
        window_config.width = 1280;
        window_config.height = 720;
        window_config.vsync = true;

        window_ = MakeUnique<HAL::Window>();
        if (!window_->Initialize(window_config)) {
            DRX_LOG_ERROR("Failed to create window");
            return false;
        }

        // Setup window callbacks
        window_->SetResizeCallback([this](const HAL::ResizeEvent& event) {
            OnResize(event.width, event.height);
        });

        window_->SetKeyCallback([this](const HAL::KeyEvent& event) {
            OnKey(event);
        });

        // Create graphics device
        HAL::GraphicsConfig graphics_config;
        graphics_config.enable_validation = true;
        graphics_config.vsync = true;

        graphics_ = MakeUnique<HAL::GraphicsDevice>();
        if (!graphics_->Initialize(window_.get(), graphics_config)) {
            DRX_LOG_ERROR("Failed to create graphics device");
            return false;
        }

        // Create renderer
        renderer_ = MakeUnique<Renderer::ForwardRenderer>();
        if (!renderer_->Initialize(graphics_.get())) {
            DRX_LOG_ERROR("Failed to create renderer");
            return false;
        }

        // Create scene
        scene_ = MakeUnique<Scene::Scene>("MainScene");

        // Create camera entity
        auto camera = scene_->CreateEntity("MainCamera");
        camera.GetTransform().transform.position = Math::Vector3(0, 0, 3);
        auto& cam_comp = camera.AddComponent<Scene::CameraComponent>();
        cam_comp.is_primary = true;

        // Create triangle entity
        auto triangle = scene_->CreateEntity("Triangle");
        triangle.GetTransform().transform.position = Math::Vector3(0, 0, 0);

        DRX_LOG_INFO("Application initialized successfully");
        return true;
    }

    void Run() {
        DRX_LOG_INFO("Entering main loop");

        float64 last_time = Platform::GetTimeSeconds();

        while (!window_->ShouldClose()) {
            // Calculate delta time
            float64 current_time = Platform::GetTimeSeconds();
            float32 delta_time = static_cast<float32>(current_time - last_time);
            last_time = current_time;

            // Poll events
            window_->PollEvents();

            // Skip rendering if minimized
            if (window_->IsMinimized()) {
                Platform::Sleep(10);
                continue;
            }

            // Update scene
            scene_->Update(delta_time);

            // Begin frame
            if (!graphics_->BeginFrame()) {
                continue;
            }

            // Render
            renderer_->BeginFrame();
            renderer_->DrawTriangle();
            renderer_->EndFrame();

            // End frame
            graphics_->EndFrame();

            // Calculate FPS
            frame_count_++;
            fps_timer_ += delta_time;
            if (fps_timer_ >= 1.0f) {
                fps_ = frame_count_;
                frame_count_ = 0;
                fps_timer_ = 0.0f;

                String title = "DRX Engine - FPS: " + std::to_string(fps_);
                window_->SetTitle(title);
            }
        }

        DRX_LOG_INFO("Exiting main loop");
    }

    void Shutdown() {
        DRX_LOG_INFO("Shutting down...");

        graphics_->WaitIdle();

        scene_.reset();
        renderer_.reset();
        graphics_.reset();
        window_.reset();

        Platform::Shutdown();

        DRX_LOG_INFO("Shutdown complete");
    }

private:
    void OnResize(uint32 width, uint32 height) {
        if (width == 0 || height == 0) return;

        DRX_LOG_INFO("Window resized: %ux%u", width, height);

        graphics_->RecreateSwapChain();
        renderer_->OnResize(width, height);
    }

    void OnKey(const HAL::KeyEvent& event) {
        if (event.action == HAL::InputAction::Press) {
            if (event.key == HAL::KeyCode::Escape) {
                window_->Close();
            }
            if (event.key == HAL::KeyCode::F11) {
                fullscreen_ = !fullscreen_;
                window_->SetFullscreen(fullscreen_);
            }
        }
    }

    UniquePtr<HAL::Window> window_;
    UniquePtr<HAL::GraphicsDevice> graphics_;
    UniquePtr<Renderer::ForwardRenderer> renderer_;
    UniquePtr<Scene::Scene> scene_;

    bool fullscreen_ = false;
    uint32 frame_count_ = 0;
    float32 fps_timer_ = 0.0f;
    uint32 fps_ = 0;
};

int main(int argc, char** argv) {
    DRX_UNUSED(argc);
    DRX_UNUSED(argv);

    Application app;

    if (!app.Initialize()) {
        DRX_LOG_FATAL("Failed to initialize application");
        return 1;
    }

    app.Run();
    app.Shutdown();

    return 0;
}

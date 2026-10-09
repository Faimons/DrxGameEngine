/**
 * DRX Engine - Sandbox
 * Graybox room, procedural props, particles, debug lines, HUD and a free-fly camera.
 *
 * Controls: hold right mouse to look, WASD move, Space/Ctrl up/down, Shift fast,
 *           G toggle debug lines, F11 fullscreen, F12 screenshot, Esc quit
 */

#include "Runtime/Public/Application.h"
#include "Renderer/Public/MeshBuilder.h"
#include "Procedural/Public/ProceduralMesh.h"
#include "Particles/Public/ParticleSystem.h"
#include "Core/Public/Log.h"

#include <algorithm>
#include <cmath>

using namespace DRX;
using Math::Matrix4;
using Math::Vector3;
using Math::Vector4;

namespace {

constexpr float32 kPi = 3.14159265358979323846f;
constexpr float32 kDegToRad = kPi / 180.0f;

struct Prop {
    MeshHandle mesh;
    Matrix4 transform;
    Renderer::Material material;
    bool spins = false;
};

class SandboxApp final : public Runtime::Application {
public:
    explicit SandboxApp(Runtime::ApplicationConfig config) : Application(std::move(config)) {}

protected:
    bool OnInitialize() override {
        Renderer::SceneRenderer& renderer = GetRenderer();
        Input::InputSystem& input = GetInput();

        input.RegisterAction("Move", Input::InputActionType::Axis2D);
        input.BindKeyAxis("Move", Input::KeyCode::D, 1.0f, 0);
        input.BindKeyAxis("Move", Input::KeyCode::A, -1.0f, 0);
        input.BindKeyAxis("Move", Input::KeyCode::W, 1.0f, 1);
        input.BindKeyAxis("Move", Input::KeyCode::S, -1.0f, 1);
        input.RegisterAction("Lift", Input::InputActionType::Axis);
        input.BindKeyAxis("Lift", Input::KeyCode::Space, 1.0f);
        input.BindKeyAxis("Lift", Input::KeyCode::LeftControl, -1.0f);

        // Textures
        const auto grid = Renderer::MeshBuilder::GridTexture(256, 4, {0.55f, 0.55f, 0.57f, 1.0f},
                                                             {0.42f, 0.42f, 0.45f, 1.0f}, {0.3f, 0.3f, 0.33f, 1.0f});
        grid_texture_ = renderer.CreateTexture(256, 256, grid.data(), true, true, "Grid");
        const auto accent = Renderer::MeshBuilder::GridTexture(256, 2, {0.85f, 0.45f, 0.15f, 1.0f},
                                                               {0.7f, 0.35f, 0.1f, 1.0f}, {0.5f, 0.25f, 0.08f, 1.0f});
        accent_texture_ = renderer.CreateTexture(256, 256, accent.data(), true, true, "Accent");

        BuildRoom();
        BuildProps();
        BuildParticles();
        ApplyConfig();
        const Config& config = GetConfig();
        position_ = Vector3(config.GetFloat("camera.start_x", position_.x), config.GetFloat("camera.start_y", position_.y),
                            config.GetFloat("camera.start_z", position_.z));
        yaw_ = config.GetFloat("camera.start_yaw", 0.0f) * kDegToRad;
        pitch_ = config.GetFloat("camera.start_pitch", pitch_ / kDegToRad) * kDegToRad;
        GetConfig().SetReloadCallback([this](const Config&) { ApplyConfig(); });
        return true;
    }

    void OnShutdown() override {
        Renderer::SceneRenderer& renderer = GetRenderer();
        for (MeshHandle mesh : owned_meshes_) renderer.DestroyMesh(mesh);
        renderer.DestroyTexture(grid_texture_);
        renderer.DestroyTexture(accent_texture_);
        if (fire_) Particles::ParticleManager::Instance().DestroySystem(fire_);
    }

    void OnUpdate(float32 dt) override {
        Input::InputSystem& input = GetInput();
        if (input.IsKeyPressed(Input::KeyCode::Escape)) RequestQuit();
        if (input.IsKeyPressed(Input::KeyCode::G)) show_debug_ = !show_debug_;

        const bool look = input.IsMouseButtonDown(Input::MouseButton::Right);
        if (look != IsMouseCaptured()) SetMouseCaptured(look);
        if (look) {
            const Math::Vector2 delta = input.GetMouseDelta();
            yaw_ += delta.x * mouse_sensitivity_ * kDegToRad;
            pitch_ = std::clamp(pitch_ - delta.y * mouse_sensitivity_ * kDegToRad, -1.55f, 1.55f);
        }

        const Vector3 forward(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), -std::cos(pitch_) * std::cos(yaw_));
        const Vector3 right = Vector3::Cross(forward, Vector3::Up).Normalized();
        const Math::Vector2 move = input.GetActionAxis2D("Move");
        const float32 lift = input.GetActionValue("Lift");
        float32 speed = move_speed_;
        if (input.IsKeyDown(Input::KeyCode::LeftShift)) speed *= fast_multiplier_;

        position_ += (forward * move.y + right * move.x + Vector3::Up * lift) * (speed * dt);

        Renderer::CameraParams& camera = GetCamera();
        camera = Renderer::CameraParams::LookAt(position_, position_ + forward, fov_degrees_ * kDegToRad,
                                                GetAspectRatio(), 0.05f, 500.0f);
        spin_angle_ += dt * 0.8f;
    }

    void OnRender(Renderer::SceneRenderer& renderer) override {
        for (const Prop& prop : props_) {
            const Matrix4 transform = prop.spins ? prop.transform * Matrix4::RotationY(spin_angle_) : prop.transform;
            renderer.DrawMesh(prop.mesh, transform, prop.material);
        }
        if (fire_ && particles_enabled_) renderer.DrawParticles(*fire_);

        if (show_debug_) {
            renderer.DrawGrid(Vector3(0, 0.01f, 0), 40.0f, 1.0f, {1, 1, 1, 0.15f});
            renderer.DrawWireBox(Renderer::AABB{{-6, 0, -6}, {6, 4, 6}}, {1.0f, 0.8f, 0.1f, 1.0f});
            renderer.DrawWireSphere(Vector3(4, 1.5f, -3), 0.75f, {0.2f, 1.0f, 0.4f, 1.0f});
            renderer.DrawLine(Vector3::Zero, Vector3(1, 0, 0), {1, 0, 0, 1});
            renderer.DrawLine(Vector3::Zero, Vector3(0, 1, 0), {0, 1, 0, 1});
            renderer.DrawLine(Vector3::Zero, Vector3(0, 0, 1), {0, 0, 1, 1});
        }
    }

    void OnUI(UI::UIContext& ui) override {
        const float32 w = ui.GetScreenWidth();
        const float32 h = ui.GetScreenHeight();
        const Renderer::RenderStats& stats = GetRenderer().GetStats();

        ui.DrawRect({10, 10, 300, 112}, {0, 0, 0, 0.55f});
        ui.DrawText(fmt::format("{:.0f} FPS  ({:.2f} ms)", GetFPS(), GetFPS() > 0 ? 1000.0f / GetFPS() : 0.0f), 20, 18,
                    {1, 1, 1, 1}, 28.0f);
        ui.DrawText(fmt::format("Draws {}  Tris {}", stats.draw_calls, stats.triangles), 20, 52, {0.8f, 0.9f, 1, 1});
        ui.DrawText(fmt::format("Culled {}  Shadow casters {}", stats.meshes_culled, stats.shadow_casters), 20, 72,
                    {0.8f, 0.9f, 1, 1});
        ui.DrawText(fmt::format("Particles {}  Pos {:.1f} {:.1f} {:.1f}", stats.particles, position_.x, position_.y,
                                position_.z),
                    20, 92, {0.8f, 0.9f, 1, 1});

        // Crosshair
        const Vector4 cross{1, 1, 1, 0.85f};
        ui.DrawLine({w * 0.5f - 8, h * 0.5f}, {w * 0.5f - 3, h * 0.5f}, cross, 2.0f);
        ui.DrawLine({w * 0.5f + 3, h * 0.5f}, {w * 0.5f + 8, h * 0.5f}, cross, 2.0f);
        ui.DrawLine({w * 0.5f, h * 0.5f - 8}, {w * 0.5f, h * 0.5f - 3}, cross, 2.0f);
        ui.DrawLine({w * 0.5f, h * 0.5f + 3}, {w * 0.5f, h * 0.5f + 8}, cross, 2.0f);

        const String help = "RMB look   WASD move   Space/Ctrl up/down   Shift fast   G debug   F12 screenshot";
        ui.DrawText(help, (w - ui.GetTextWidth(help)) * 0.5f, h - 30, {1, 1, 1, 0.7f});
    }

private:
    MeshHandle Own(MeshHandle mesh) {
        if (mesh.IsValid()) owned_meshes_.push_back(mesh);
        return mesh;
    }

    void AddProp(MeshHandle mesh, const Matrix4& transform, const Renderer::Material& material, bool spins = false) {
        if (mesh.IsValid()) props_.push_back({mesh, transform, material, spins});
    }

    void AddBox(const Vector3& center, const Vector3& size, TextureHandle texture, const Vector4& color = {1, 1, 1, 1}) {
        Renderer::Material material;
        material.albedo = texture;
        material.color = color;
        AddProp(Own(GetRenderer().CreateMesh(Renderer::MeshBuilder::Box(size), "Box")), Matrix4::Translation(center),
                material);
    }

    // 12 x 12 m room, 4 m ceiling, open front wall with a doorway and an opening in the roof
    void BuildRoom() {
        Renderer::Material floor;
        floor.albedo = grid_texture_;
        AddProp(Own(GetRenderer().CreateMesh(Renderer::MeshBuilder::Plane(60, 60, 1), "Ground")), Matrix4::Identity(),
                floor);

        constexpr float32 kSize = 12.0f, kHeight = 4.0f, kWall = 0.3f;
        const float32 half = kSize * 0.5f;
        AddBox({0, kHeight * 0.5f, -half}, {kSize, kHeight, kWall}, grid_texture_);
        AddBox({-half, kHeight * 0.5f, 0}, {kWall, kHeight, kSize}, grid_texture_);
        AddBox({half, kHeight * 0.5f, 0}, {kWall, kHeight, kSize}, grid_texture_);
        AddBox({-3.75f, kHeight * 0.5f, half}, {4.5f, kHeight, kWall}, grid_texture_);
        AddBox({3.75f, kHeight * 0.5f, half}, {4.5f, kHeight, kWall}, grid_texture_);
        AddBox({0, kHeight - 0.5f, half}, {3.0f, 1.0f, kWall}, grid_texture_);
        AddBox({0, kHeight + kWall * 0.5f, -3.0f}, {kSize, kWall, 6.0f}, grid_texture_);

        // Cover blocks
        AddBox({-2.5f, 0.5f, -2.0f}, {1.0f, 1.0f, 2.0f}, accent_texture_);
        AddBox({2.0f, 0.75f, 1.0f}, {2.0f, 1.5f, 0.6f}, accent_texture_);
        AddBox({-4.0f, 1.0f, 3.5f}, {1.2f, 2.0f, 1.2f}, grid_texture_, {0.7f, 0.8f, 1.0f, 1.0f});
    }

    void BuildProps() {
        Renderer::SceneRenderer& renderer = GetRenderer();
        auto procedural = [&](const Procedural::Mesh& mesh, const char* name) {
            return Own(renderer.CreateMesh(Procedural::ToMeshData(mesh), name));
        };

        Renderer::Material metal;
        metal.roughness = 0.35f;
        metal.metallic = 0.8f;
        metal.color = {0.75f, 0.75f, 0.8f, 1.0f};
        AddProp(procedural(Procedural::WeaponFactory::CreateRifle(1.0f), "Rifle"),
                Matrix4::Translation(2.0f, 1.75f, 1.0f), metal, true);

        Renderer::Material plain;
        AddProp(procedural(Procedural::CharacterFactory::CreateSoldier(1.8f), "Soldier"),
                Matrix4::Translation(-1.0f, 0.0f, -4.0f), plain);
        AddProp(procedural(Procedural::EnvironmentFactory::CreateBarrel(0.4f, 1.0f), "Barrel"),
                Matrix4::Translation(4.5f, 0.0f, 4.5f), plain);
        AddProp(procedural(Procedural::EnvironmentFactory::CreateCrate(1.0f), "Crate"),
                Matrix4::Translation(-4.5f, 0.0f, -4.5f), plain);

        for (int i = 0; i < 6; ++i) {
            const float32 angle = static_cast<float32>(i) * kPi / 3.0f;
            const Vector3 pos(std::cos(angle) * 18.0f, 0.0f, std::sin(angle) * 18.0f);
            AddProp(procedural(Procedural::EnvironmentFactory::CreateTree(5.0f + static_cast<float32>(i % 3)), "Tree"),
                    Matrix4::Translation(pos), plain);
            AddProp(procedural(Procedural::EnvironmentFactory::CreateRock(1.0f + 0.3f * static_cast<float32>(i % 2)),
                               "Rock"),
                    Matrix4::Translation(pos + Vector3(3.0f, 0.0f, 1.5f)), plain);
        }

        Renderer::Material glow;
        glow.color = {0.2f, 0.9f, 1.0f, 1.0f};
        glow.emissive = {0.2f, 0.9f, 1.0f};
        glow.emissive_strength = 4.0f;
        glow.cast_shadows = false;
        AddProp(Own(renderer.CreateMesh(Renderer::MeshBuilder::Sphere(0.25f), "Orb")),
                Matrix4::Translation(4.0f, 1.5f, -3.0f), glow);

        Renderer::Material capsule;
        capsule.color = {0.9f, 0.2f, 0.2f, 1.0f};
        AddProp(Own(renderer.CreateMesh(Renderer::MeshBuilder::Capsule(0.35f, 1.8f), "Capsule")),
                Matrix4::Translation(1.0f, 0.9f, -4.0f), capsule);
        AddProp(Own(renderer.CreateMesh(Renderer::MeshBuilder::Cylinder(0.3f, 3.6f), "Pillar")),
                Matrix4::Translation(-2.0f, 1.8f, 2.5f), plain);
    }

    void BuildParticles() {
        fire_ = Particles::ParticleManager::Instance().CreateSystem("Fire");
        fire_->position = Vector3(4.5f, 1.05f, 4.5f);

        Particles::ParticleEmitter* flame = fire_->AddEmitter();
        flame->SetMaxParticles(400);
        flame->SetEmissionRate(120.0f);
        flame->lifetime = {0.4f, 0.9f};
        flame->start_size = {0.25f, 0.45f};
        flame->start_speed = {0.6f, 1.4f};
        flame->start_color = {{1.0f, 0.55f, 0.15f, 1.0f}, {1.0f, 0.8f, 0.3f, 1.0f}};
        flame->emission_shape.shape = Particles::EmissionShape::Cone;
        flame->emission_shape.radius = 0.25f;
        flame->emission_shape.angle = 12.0f;
        flame->blend_mode = Particles::ParticleEmitter::BlendMode::Additive;
        auto* flame_force = flame->AddModule<Particles::ForceModule>();
        flame_force->gravity = {0, 1.5f, 0};
        auto* flame_size = flame->AddModule<Particles::SizeOverLifetimeModule>();
        flame_size->size_curve = {{0.0f, 1.0f}, {1.0f, 0.2f}};
        auto* flame_color = flame->AddModule<Particles::ColorOverLifetimeModule>();
        flame_color->gradient = {{0.0f, {1.0f, 0.7f, 0.2f, 1.0f}}, {1.0f, {0.8f, 0.1f, 0.0f, 0.0f}}};

        Particles::ParticleEmitter* smoke = fire_->AddEmitter();
        smoke->position = Vector3(0, 0.6f, 0);
        smoke->SetMaxParticles(200);
        smoke->SetEmissionRate(25.0f);
        smoke->lifetime = {2.0f, 3.5f};
        smoke->start_size = {0.3f, 0.6f};
        smoke->start_speed = {0.4f, 0.9f};
        smoke->start_color = {{0.25f, 0.25f, 0.25f, 0.5f}, {0.4f, 0.4f, 0.4f, 0.6f}};
        smoke->emission_shape.shape = Particles::EmissionShape::Cone;
        smoke->emission_shape.radius = 0.2f;
        smoke->emission_shape.angle = 20.0f;
        auto* smoke_force = smoke->AddModule<Particles::ForceModule>();
        smoke_force->gravity = {0.2f, 0.4f, 0};
        smoke_force->drag = 0.3f;
        auto* smoke_size = smoke->AddModule<Particles::SizeOverLifetimeModule>();
        smoke_size->size_curve = {{0.0f, 1.0f}, {1.0f, 3.5f}};
        auto* smoke_color = smoke->AddModule<Particles::ColorOverLifetimeModule>();
        smoke_color->gradient = {{0.0f, {0.3f, 0.3f, 0.3f, 0.0f}}, {0.15f, {0.35f, 0.35f, 0.35f, 0.5f}},
                                 {1.0f, {0.5f, 0.5f, 0.5f, 0.0f}}};
        fire_->Play();
    }

    void ApplyConfig() {
        const Config& config = GetConfig();
        fov_degrees_ = std::clamp(config.GetFloat("camera.fov_degrees", fov_degrees_), 40.0f, 120.0f);
        move_speed_ = config.GetFloat("camera.move_speed", move_speed_);
        fast_multiplier_ = config.GetFloat("camera.fast_multiplier", fast_multiplier_);
        mouse_sensitivity_ = config.GetFloat("camera.mouse_sensitivity", mouse_sensitivity_);

        Renderer::EnvironmentSettings& env = GetEnvironment();
        env.sun_intensity = config.GetFloat("environment.sun_intensity", env.sun_intensity);
        env.fog_density = config.GetFloat("environment.fog_density", env.fog_density);
        env.exposure = config.GetFloat("environment.exposure", env.exposure);
        env.shadows_enabled = config.GetBool("environment.shadows", env.shadows_enabled);
        particles_enabled_ = config.GetBool("particles.enabled", particles_enabled_);
    }

    Array<MeshHandle> owned_meshes_;
    Array<Prop> props_;
    TextureHandle grid_texture_;
    TextureHandle accent_texture_;
    SharedPtr<Particles::ParticleSystem> fire_;

    Vector3 position_{0.0f, 1.7f, 10.0f};
    float32 yaw_ = 0.0f;
    float32 pitch_ = -0.08f;
    float32 spin_angle_ = 0.0f;

    float32 fov_degrees_ = 90.0f;
    float32 move_speed_ = 6.0f;
    float32 fast_multiplier_ = 4.0f;
    float32 mouse_sensitivity_ = 0.12f;
    bool particles_enabled_ = true;
    bool show_debug_ = true;
};

} // namespace

int main(int argc, char** argv) {
    Runtime::ApplicationConfig config;
    config.title = "DRX Sandbox";
    config.config_path = DRX_SANDBOX_CONFIG;
    SandboxApp app(std::move(config));
    return app.Run(argc, argv);
}

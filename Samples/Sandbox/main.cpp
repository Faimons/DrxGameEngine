/**
 * DRX Engine - Sandbox
 * Graybox room with procedural props, particles, physics, a first-person player with a
 * hitscan rifle, target dummies, HUD and synthesized sounds.
 *
 * Tab switches between play mode (first person) and the free-fly camera.
 *   Play: mouse look, WASD move, Space jump, Shift sprint, Ctrl crouch, LMB fire, R reload
 *   Fly:  hold RMB to look, WASD move, Space/Ctrl up/down, Shift fast
 *   Both: G debug lines, H alarm, F11 fullscreen, F12 screenshot, Esc quit
 */

#include "Runtime/Public/Application.h"
#include "Renderer/Public/MeshBuilder.h"
#include "Procedural/Public/ProceduralMesh.h"
#include "Particles/Public/ParticleSystem.h"
#include "Audio/Public/SoundSynth.h"
#include "Game/Public/FPSFramework.h"
#include "Debug/Public/DebugSystem.h"
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

struct Dummy {
    Game::Enemy* enemy = nullptr;
    Vector3 home;
    float32 respawn_timer = 0.0f;
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

        const auto grid = Renderer::MeshBuilder::GridTexture(256, 4, {0.55f, 0.55f, 0.57f, 1.0f},
                                                             {0.42f, 0.42f, 0.45f, 1.0f}, {0.3f, 0.3f, 0.33f, 1.0f});
        grid_texture_ = renderer.CreateTexture(256, 256, grid.data(), true, true, "Grid");
        const auto accent = Renderer::MeshBuilder::GridTexture(256, 2, {0.85f, 0.45f, 0.15f, 1.0f},
                                                               {0.7f, 0.35f, 0.1f, 1.0f}, {0.5f, 0.25f, 0.08f, 1.0f});
        accent_texture_ = renderer.CreateTexture(256, 256, accent.data(), true, true, "Accent");
        dummy_mesh_ = Own(renderer.CreateMesh(Renderer::MeshBuilder::Capsule(0.35f, 1.8f), "Dummy"));
        head_mesh_ = Own(renderer.CreateMesh(Renderer::MeshBuilder::Sphere(0.16f), "DummyHead"));

        Audio::AudioSystem& audio = Audio::AudioSystem::Instance();
        shot_sound_ = audio.CreateClip("synth:gunshot", Audio::Synth::Gunshot());
        hit_sound_ = audio.CreateClip("synth:hit", Audio::Synth::Hit());
        headshot_sound_ = audio.CreateClip("synth:headshot", Audio::Synth::Headshot());
        empty_sound_ = audio.CreateClip("synth:click", Audio::Synth::Click());
        alarm_sound_ = audio.CreateClip("synth:alarm", Audio::Synth::Alarm(2.5f));

        if (!Game::GameWorld::Instance().Initialize()) return false;
        Game::GameWorld::Instance().AddStaticBox({0, -0.5f, 0}, {60, 1, 60});

        BuildRoom();
        BuildProps();
        BuildParticles();
        ApplyConfig();
        SpawnGameplay();

        const Config& config = GetConfig();
        fly_position_ = Vector3(config.GetFloat("camera.start_x", fly_position_.x),
                                config.GetFloat("camera.start_y", fly_position_.y),
                                config.GetFloat("camera.start_z", fly_position_.z));
        fly_yaw_ = config.GetFloat("camera.start_yaw", 0.0f) * kDegToRad;
        fly_pitch_ = config.GetFloat("camera.start_pitch", fly_pitch_ / kDegToRad) * kDegToRad;
        SetPlayMode(config.GetString("sandbox.start_mode", "play") == "play");
        GetConfig().SetReloadCallback([this](const Config&) { ApplyConfig(); });
        return true;
    }

    void OnShutdown() override {
        Game::GameWorld::Instance().Shutdown();
        Renderer::SceneRenderer& renderer = GetRenderer();
        for (MeshHandle mesh : owned_meshes_) renderer.DestroyMesh(mesh);
        renderer.DestroyTexture(grid_texture_);
        renderer.DestroyTexture(accent_texture_);
        if (fire_) Particles::ParticleManager::Instance().DestroySystem(fire_);
    }

    void OnFixedUpdate(float32 dt) override {
        Input::InputSystem& input = GetInput();
        if (play_mode_ && player_) {
            player_->SetMoveInput(input.GetActionAxis2D("Move"));
            player_->SetJumpInput(input.IsKeyDown(Input::KeyCode::Space));
            player_->SetSprintInput(input.IsKeyDown(Input::KeyCode::LeftShift));
            player_->SetCrouchInput(input.IsKeyDown(Input::KeyCode::LeftControl));
        } else if (player_) {
            player_->SetMoveInput({0, 0});
            player_->SetJumpInput(false);
        }
        previous_feet_ = player_ ? player_->GetPosition() : Vector3::Zero;
        Game::GameWorld::Instance().FixedUpdate(dt);
    }

    void OnUpdate(float32 dt) override {
        Input::InputSystem& input = GetInput();
        if (input.IsKeyPressed(Input::KeyCode::Escape)) RequestQuit();
        if (input.IsKeyPressed(Input::KeyCode::Tab)) SetPlayMode(!play_mode_);
        if (input.IsKeyPressed(Input::KeyCode::G)) show_debug_ = !show_debug_;
        if (input.IsKeyPressed(Input::KeyCode::H)) {
            Audio::AudioSystem::Instance().PlayOneShot(alarm_sound_, Vector3(0.0f, 3.5f, -5.5f));
        }

        if (play_mode_) UpdatePlayer(input);
        else UpdateFlyCamera(input, dt);

        Game::GameWorld::Instance().Update(dt);
        UpdateDummies(dt);
        hit_marker_timer_ = std::max(0.0f, hit_marker_timer_ - dt);
        spin_angle_ += dt * 0.8f;
    }

    void OnRender(Renderer::SceneRenderer& renderer) override {
        for (const Prop& prop : props_) {
            const Matrix4 transform = prop.spins ? prop.transform * Matrix4::RotationY(spin_angle_) : prop.transform;
            renderer.DrawMesh(prop.mesh, transform, prop.material);
        }

        for (const Dummy& dummy : dummies_) {
            if (!dummy.enemy->IsAlive()) continue;
            const Vector3 feet = dummy.enemy->GetPosition();
            const float32 health = dummy.enemy->GetHealth().GetHealthPercent();
            Renderer::Material body;
            body.color = {0.9f, 0.25f + 0.5f * (1.0f - health), 0.2f, 1.0f};
            renderer.DrawMesh(dummy_mesh_, Matrix4::Translation(feet + Vector3(0, 0.9f, 0)), body);
            Renderer::Material head;
            head.color = {1.0f, 0.85f, 0.2f, 1.0f};
            head.emissive = {1.0f, 0.7f, 0.1f};
            head.emissive_strength = 0.6f;
            renderer.DrawMesh(head_mesh_, Matrix4::Translation(feet + Vector3(0, 1.62f, 0)), head);
        }

        if (fire_ && particles_enabled_) renderer.DrawParticles(*fire_);

        if (show_debug_) {
            renderer.DrawGrid(Vector3(0, 0.01f, 0), 40.0f, 1.0f, {1, 1, 1, 0.15f});
            renderer.DrawWireBox(Renderer::AABB{{-6, 0, -6}, {6, 4, 6}}, {1.0f, 0.8f, 0.1f, 1.0f});
            if (!play_mode_ && player_) {
                const Vector3 feet = player_->GetPosition();
                renderer.DrawWireBox(Renderer::AABB{feet - Vector3(0.35f, 0, 0.35f), feet + Vector3(0.35f, 1.8f, 0.35f)},
                                     {0.2f, 1.0f, 0.4f, 1.0f});
            }
        }
    }

    void OnUI(UI::UIContext& ui) override {
        const float32 w = ui.GetScreenWidth();
        const float32 h = ui.GetScreenHeight();
        const Renderer::RenderStats& stats = GetRenderer().GetStats();

        ui.DrawRect({10, 10, 320, 112}, {0, 0, 0, 0.55f});
        ui.DrawText(fmt::format("{:.0f} FPS  ({:.2f} ms)", GetFPS(), GetFPS() > 0 ? 1000.0f / GetFPS() : 0.0f), 20, 18,
                    {1, 1, 1, 1}, 28.0f);
        ui.DrawText(fmt::format("Draws {}  Tris {}", stats.draw_calls, stats.triangles), 20, 52, {0.8f, 0.9f, 1, 1});
        ui.DrawText(fmt::format("Culled {}  Shadow casters {}", stats.meshes_culled, stats.shadow_casters), 20, 72,
                    {0.8f, 0.9f, 1, 1});
        ui.DrawText(fmt::format("{}  Particles {}", play_mode_ ? "PLAY" : "FLY", stats.particles), 20, 92,
                    {0.8f, 0.9f, 1, 1});

        if (play_mode_ && player_) {
            DrawCrosshair(ui, w * 0.5f, h * 0.5f);
            DrawHitMarker(ui, w * 0.5f, h * 0.5f);
            const Game::Weapon* weapon = player_->GetCurrentWeapon();
            ui.DrawRect({20, h - 70, 220, 50}, {0, 0, 0, 0.5f});
            ui.DrawText(fmt::format("HP {:.0f}", player_->GetHealth().GetHealth()), 32, h - 62, {1, 1, 1, 1}, 28.0f);
            if (weapon) {
                const String ammo = weapon->IsReloading()
                                        ? String("RELOAD")
                                        : fmt::format("{} / {}", weapon->GetAmmoInMagazine(), weapon->GetReserveAmmo());
                ui.DrawRect({w - 240, h - 70, 220, 50}, {0, 0, 0, 0.5f});
                ui.DrawText(ammo, w - 228, h - 62, {1, 1, 1, 1}, 28.0f);
            }
        }

        const String help = play_mode_
                                ? "WASD move  Space jump  Shift sprint  Ctrl crouch  LMB fire  R reload  Tab fly  G debug"
                                : "RMB look  WASD move  Space/Ctrl up/down  Shift fast  Tab play  G debug  F12 screenshot";
        ui.DrawText(help, (w - ui.GetTextWidth(help)) * 0.5f, h - 30, {1, 1, 1, 0.7f});
    }

private:
    void SetPlayMode(bool play) {
        play_mode_ = play;
        SetMouseCaptured(play);
        if (player_) player_->SetFireInput(false);
    }

    void UpdatePlayer(Input::InputSystem& input) {
        if (!player_) return;
        player_->AddLookInput(input.GetMouseDelta());
        player_->SetFireInput(input.IsMouseButtonDown(Input::MouseButton::Left));
        player_->SetReloadInput(input.IsKeyPressed(Input::KeyCode::R));

        // Render between the last two fixed steps
        const float32 alpha = GetInterpolationAlpha();
        const Vector3 feet = previous_feet_ + (player_->GetPosition() - previous_feet_) * alpha;
        const Vector3 eye = feet + (player_->GetCameraPosition() - player_->GetPosition());
        GetCamera() = Renderer::CameraParams::LookAt(eye, eye + player_->GetAimDirection(),
                                                     player_->GetCurrentFOV(fov_degrees_) * kDegToRad, GetAspectRatio(),
                                                     0.05f, 500.0f);
    }

    void UpdateFlyCamera(Input::InputSystem& input, float32 dt) {
        const bool look = input.IsMouseButtonDown(Input::MouseButton::Right);
        if (look != IsMouseCaptured()) SetMouseCaptured(look);
        if (look) {
            const Math::Vector2 delta = input.GetMouseDelta();
            fly_yaw_ += delta.x * mouse_sensitivity_ * kDegToRad;
            fly_pitch_ = std::clamp(fly_pitch_ - delta.y * mouse_sensitivity_ * kDegToRad, -1.55f, 1.55f);
        }
        const Vector3 forward(std::cos(fly_pitch_) * std::sin(fly_yaw_), std::sin(fly_pitch_),
                              -std::cos(fly_pitch_) * std::cos(fly_yaw_));
        const Vector3 right = Vector3::Cross(forward, Vector3::Up).Normalized();
        const Math::Vector2 move = input.GetActionAxis2D("Move");
        const float32 lift = input.GetActionValue("Lift");
        float32 speed = move_speed_;
        if (input.IsKeyDown(Input::KeyCode::LeftShift)) speed *= fast_multiplier_;
        fly_position_ += (forward * move.y + right * move.x + Vector3::Up * lift) * (speed * dt);

        GetCamera() = Renderer::CameraParams::LookAt(fly_position_, fly_position_ + forward, fov_degrees_ * kDegToRad,
                                                     GetAspectRatio(), 0.05f, 500.0f);
    }

    void UpdateDummies(float32 dt) {
        for (Dummy& dummy : dummies_) {
            if (dummy.enemy->IsAlive()) continue;
            dummy.respawn_timer -= dt;
            if (dummy.respawn_timer <= 0.0f) {
                dummy.enemy->GetHealth().Revive();
                dummy.enemy->SetPosition(dummy.home);
                dummy.respawn_timer = 3.0f;
            }
        }
    }

    static void DrawCrosshair(UI::UIContext& ui, float32 cx, float32 cy) {
        const Vector4 color{1, 1, 1, 0.85f};
        ui.DrawLine({cx - 9, cy}, {cx - 3, cy}, color, 2.0f);
        ui.DrawLine({cx + 3, cy}, {cx + 9, cy}, color, 2.0f);
        ui.DrawLine({cx, cy - 9}, {cx, cy - 3}, color, 2.0f);
        ui.DrawLine({cx, cy + 3}, {cx, cy + 9}, color, 2.0f);
    }

    void DrawHitMarker(UI::UIContext& ui, float32 cx, float32 cy) const {
        if (hit_marker_timer_ <= 0.0f) return;
        const Vector4 color = hit_marker_critical_ ? Vector4(1, 0.2f, 0.2f, 1) : Vector4(1, 1, 1, 1);
        for (const float32 sx : {-1.0f, 1.0f}) {
            for (const float32 sy : {-1.0f, 1.0f}) {
                ui.DrawLine({cx + sx * 6, cy + sy * 6}, {cx + sx * 13, cy + sy * 13}, color, 2.0f);
            }
        }
    }

    void SpawnGameplay() {
        Game::GameWorld& world = Game::GameWorld::Instance();
        const Config& config = GetConfig();

        Game::PlayerConfig player_config;
        player_config.walk_speed = config.GetFloat("player.walk_speed", player_config.walk_speed);
        player_config.run_speed = config.GetFloat("player.run_speed", player_config.run_speed);
        player_config.jump_height = config.GetFloat("player.jump_height", player_config.jump_height);
        player_config.look_sensitivity = mouse_sensitivity_;
        player_ = world.SpawnPlayer({0, 0, 9}, player_config, 0.0f);
        previous_feet_ = player_->GetPosition();

        Game::WeaponConfig rifle;
        rifle.name = "Rifle";
        rifle.damage = config.GetFloat("weapon.damage", 25.0f);
        rifle.fire_rate = config.GetFloat("weapon.fire_rate", 10.0f);
        rifle.spread = config.GetFloat("weapon.spread", 0.4f);
        rifle.recoil = config.GetFloat("weapon.recoil", 0.5f);
        rifle.magazine_size = config.GetInt("weapon.magazine", 30);
        rifle.max_ammo = config.GetInt("weapon.reserve", 120);
        auto weapon = MakeShared<Game::Weapon>(rifle);
        weapon->SetOnShot([this](const Game::ShotResult& shot) { OnShot(shot); });
        weapon->SetOnEmpty([this] { Audio::AudioSystem::Instance().PlayOneShot2D(empty_sound_, 0.6f); });
        player_->GiveWeapon(weapon);

        Game::EnemyConfig dummy_config;
        dummy_config.sight_range = 0.0f;   // passive targets
        dummy_config.hearing_range = 0.0f;
        for (const Vector3& home : {Vector3(-3.0f, 0, -4.0f), Vector3(0.5f, 0, -4.5f), Vector3(3.5f, 0, -2.5f)}) {
            dummies_.push_back({world.SpawnEnemy(dummy_config, home), home, 3.0f});
        }
    }

    void OnShot(const Game::ShotResult& shot) {
        Audio::AudioSystem& audio = Audio::AudioSystem::Instance();
        audio.PlayOneShot2D(shot_sound_, 0.7f);

        // Tracer starts slightly below the eye so it is visible
        Debug::DebugRenderer& debug = Debug::DebugRenderer::Instance();
        debug.DrawLine(shot.origin + Vector3(0, -0.12f, 0), shot.end, {1.0f, 0.9f, 0.5f, 1.0f}, 0.06f);
        if (shot.hit) debug.DrawHitPoint(shot.end, shot.normal, 1.5f);

        if (shot.target) {
            hit_marker_timer_ = 0.15f;
            hit_marker_critical_ = shot.critical;
            audio.PlayOneShot2D(shot.critical ? headshot_sound_ : hit_sound_, 0.8f);
        }
    }

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
        Game::GameWorld::Instance().AddStaticBox(center, size);
    }

    // Collision from the mesh bounds (good enough for crates, barrels and pillars)
    void AddBoundsCollider(MeshHandle mesh, const Vector3& position) {
        const Renderer::AABB bounds = GetRenderer().GetMeshBounds(mesh);
        if (bounds.IsValid()) {
            Game::GameWorld::Instance().AddStaticBox(position + bounds.GetCenter(), bounds.max - bounds.min);
        }
    }

    // 12 x 12 m room, 4 m ceiling, open front wall with a doorway and a partial roof
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

        // Cover and a small staircase
        AddBox({-2.5f, 0.5f, -2.0f}, {1.0f, 1.0f, 2.0f}, accent_texture_);
        AddBox({2.0f, 0.75f, 1.0f}, {2.0f, 1.5f, 0.6f}, accent_texture_);
        AddBox({-4.0f, 1.0f, 3.5f}, {1.2f, 2.0f, 1.2f}, grid_texture_, {0.7f, 0.8f, 1.0f, 1.0f});
        for (int i = 0; i < 4; ++i) {
            const float32 step = 0.25f * static_cast<float32>(i + 1);
            AddBox({4.5f, step * 0.5f, 1.5f + 0.35f * static_cast<float32>(i)}, {1.4f, step, 0.35f}, grid_texture_,
                   {0.8f, 0.9f, 0.8f, 1.0f});
        }
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
        const MeshHandle barrel = procedural(Procedural::EnvironmentFactory::CreateBarrel(0.4f, 1.0f), "Barrel");
        AddProp(barrel, Matrix4::Translation(4.5f, 0.0f, 4.5f), plain);
        AddBoundsCollider(barrel, {4.5f, 0.0f, 4.5f});
        const MeshHandle crate = procedural(Procedural::EnvironmentFactory::CreateCrate(1.0f), "Crate");
        AddProp(crate, Matrix4::Translation(-4.5f, 0.0f, -4.5f), plain);
        AddBoundsCollider(crate, {-4.5f, 0.0f, -4.5f});

        for (int i = 0; i < 6; ++i) {
            const float32 angle = static_cast<float32>(i) * kPi / 3.0f;
            const Vector3 pos(std::cos(angle) * 18.0f, 0.0f, std::sin(angle) * 18.0f);
            AddProp(procedural(Procedural::EnvironmentFactory::CreateTree(5.0f + static_cast<float32>(i % 3)), "Tree"),
                    Matrix4::Translation(pos), plain);
            Game::GameWorld::Instance().AddStaticBox(pos + Vector3(0, 1.5f, 0), {0.5f, 3.0f, 0.5f});   // trunk
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
                Matrix4::Translation(4.0f, 2.5f, -3.0f), glow);

        const MeshHandle pillar = Own(renderer.CreateMesh(Renderer::MeshBuilder::Cylinder(0.3f, 3.6f), "Pillar"));
        AddProp(pillar, Matrix4::Translation(-2.0f, 1.8f, 2.5f), plain);
        AddBoundsCollider(pillar, {-2.0f, 1.8f, 2.5f});
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
        flame->AddModule<Particles::ForceModule>()->gravity = {0, 1.5f, 0};
        flame->AddModule<Particles::SizeOverLifetimeModule>()->size_curve = {{0.0f, 1.0f}, {1.0f, 0.2f}};
        flame->AddModule<Particles::ColorOverLifetimeModule>()->gradient = {{0.0f, {1.0f, 0.7f, 0.2f, 1.0f}},
                                                                            {1.0f, {0.8f, 0.1f, 0.0f, 0.0f}}};

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
        smoke->AddModule<Particles::SizeOverLifetimeModule>()->size_curve = {{0.0f, 1.0f}, {1.0f, 3.5f}};
        smoke->AddModule<Particles::ColorOverLifetimeModule>()->gradient = {{0.0f, {0.3f, 0.3f, 0.3f, 0.0f}},
                                                                            {0.15f, {0.35f, 0.35f, 0.35f, 0.5f}},
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
    Array<Dummy> dummies_;
    TextureHandle grid_texture_;
    TextureHandle accent_texture_;
    MeshHandle dummy_mesh_;
    MeshHandle head_mesh_;
    SharedPtr<Particles::ParticleSystem> fire_;
    SharedPtr<Audio::AudioClip> shot_sound_, hit_sound_, headshot_sound_, empty_sound_, alarm_sound_;

    Game::Player* player_ = nullptr;
    Vector3 previous_feet_;
    bool play_mode_ = true;
    float32 hit_marker_timer_ = 0.0f;
    bool hit_marker_critical_ = false;

    Vector3 fly_position_{0.0f, 1.7f, 10.0f};
    float32 fly_yaw_ = 0.0f;
    float32 fly_pitch_ = -0.08f;
    float32 spin_angle_ = 0.0f;

    float32 fov_degrees_ = 90.0f;
    float32 move_speed_ = 6.0f;
    float32 fast_multiplier_ = 4.0f;
    float32 mouse_sensitivity_ = 0.12f;
    bool particles_enabled_ = true;
    bool show_debug_ = false;
};

} // namespace

int main(int argc, char** argv) {
    Runtime::ApplicationConfig config;
    config.title = "DRX Sandbox";
    config.config_path = DRX_SANDBOX_CONFIG;
    SandboxApp app(std::move(config));
    return app.Run(argc, argv);
}

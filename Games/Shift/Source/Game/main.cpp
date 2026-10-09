/**
 * SHIFT - Game client (phase 1 and 2: movement, pistol, target dummy)
 *
 * Controls: mouse look, WASD move, Space jump, LMB fire, R reload,
 *           Esc releases the mouse (Esc again quits), click to capture, F11 fullscreen, F12 screenshot
 */

#include "../Core/ShiftConfig.h"
#include "../Core/ShiftLevel.h"
#include "../Core/ShiftWorld.h"

#include "Audio/Public/SoundSynth.h"
#include "Core/Public/Log.h"
#include "Debug/Public/DebugSystem.h"
#include "Renderer/Public/MeshBuilder.h"
#include "Runtime/Public/Application.h"

#include <algorithm>
#include <cmath>

using namespace DRX;
using Math::Matrix4;
using Math::Vector3;
using Math::Vector4;

namespace Shift {
namespace {

constexpr float32 kDegToRad = 3.14159265358979323846f / 180.0f;

Vector4 SurfaceColor(Surface surface) {
    switch (surface) {
        case Surface::Floor: return {0.55f, 0.56f, 0.58f, 1.0f};
        case Surface::Wall: return {0.72f, 0.72f, 0.74f, 1.0f};
        case Surface::Ceiling: return {0.6f, 0.6f, 0.62f, 1.0f};
        case Surface::Cover: return {0.85f, 0.55f, 0.25f, 1.0f};
        case Surface::Step: return {0.6f, 0.75f, 0.6f, 1.0f};
        case Surface::SpawnA: return {0.2f, 0.45f, 1.0f, 1.0f};
        case Surface::SpawnB: return {1.0f, 0.3f, 0.25f, 1.0f};
    }
    return {1, 1, 1, 1};
}

Vector4 TeamColor(Team team) {
    return team == Team::A ? Vector4(0.25f, 0.5f, 1.0f, 1.0f) : Vector4(0.95f, 0.3f, 0.25f, 1.0f);
}

struct RenderBox {
    MeshHandle mesh;
    Matrix4 transform;
    Renderer::Material material;
};

class ShiftApp final : public Runtime::Application {
public:
    explicit ShiftApp(Runtime::ApplicationConfig config) : Application(std::move(config)) {}

protected:
    bool OnInitialize() override {
        config_.Load(GetConfig());
        SetFixedTimestep(config_.GetTickInterval());
        DRX_LOG_INFO("SHIFT: {} ticks/s, pistol {} dmg x{}/s, magazine {}", config_.net.tick_rate,
                     config_.weapons.pistol.damage, config_.weapons.pistol.fire_rate, config_.weapons.pistol.magazine);

        if (!world_.Initialize(config_)) return false;
        level_ = BuildTestRoom();
        level_.Apply(world_);
        local_id_ = world_.AddPlayer(Team::A);
        dummy_id_ = world_.AddPlayer(Team::B, true);
        if (local_id_ == kInvalidPlayer || dummy_id_ == kInvalidPlayer) return false;
        SnapLookToSpawn();
        RememberPositions();

        CreateGraphics();
        CreateSounds();
        GetEnvironment().fog_density = 0.0f;
        SetMouseCaptured(true);
        return true;
    }

    void OnShutdown() override {
        world_.Shutdown();
        Renderer::SceneRenderer& renderer = GetRenderer();
        for (MeshHandle mesh : meshes_) renderer.DestroyMesh(mesh);
        renderer.DestroyTexture(grid_texture_);
    }

    void OnUpdate(float32 dt) override {
        Input::InputSystem& input = GetInput();
        if (input.IsKeyPressed(Input::KeyCode::Escape)) {
            if (IsMouseCaptured()) SetMouseCaptured(false);
            else RequestQuit();
        }
        if (!IsMouseCaptured() && input.IsMouseButtonPressed(Input::MouseButton::Left)) {
            SetMouseCaptured(true);
            swallow_fire_ = true;   // the capturing click does not shoot
        }

        if (IsMouseCaptured()) {
            const Math::Vector2 delta = input.GetMouseDelta();
            look_yaw_ = std::fmod(look_yaw_ + delta.x * config_.player.mouse_sensitivity, 360.0f);
            look_pitch_ = std::clamp(look_pitch_ - delta.y * config_.player.mouse_sensitivity, -89.0f, 89.0f);

            // Latch short presses so they are not lost between ticks
            if (input.IsMouseButtonPressed(Input::MouseButton::Left) && !swallow_fire_) fire_latch_ = true;
            if (input.IsKeyPressed(Input::KeyCode::R)) reload_latch_ = true;
            if (input.IsKeyPressed(Input::KeyCode::Space)) jump_latch_ = true;
        }
        if (!input.IsMouseButtonDown(Input::MouseButton::Left)) swallow_fire_ = false;

        hit_marker_ = std::max(0.0f, hit_marker_ - dt);
        damage_flash_ = std::max(0.0f, damage_flash_ - dt);
        UpdateCamera();
    }

    void OnFixedUpdate(float32 /*dt*/) override {
        world_.SetInput(local_id_, BuildInput());
        RememberPositions();
        world_.Step();
        for (const WorldEvent& event : world_.TakeEvents()) HandleEvent(event);
    }

    void OnRender(Renderer::SceneRenderer& renderer) override {
        for (const RenderBox& box : level_boxes_) renderer.DrawMesh(box.mesh, box.transform, box.material);

        for (const auto& [id, player] : world_.GetPlayers()) {
            if (id == local_id_ || !player->alive) continue;
            const Vector3 feet = InterpolatedFeet(*player);
            Renderer::Material body;
            body.color = TeamColor(player->team);
            if (player->IsProtected()) {
                body.emissive = Vector3(0.6f, 0.8f, 1.0f);
                body.emissive_strength = 0.6f;
            }
            renderer.DrawMesh(body_mesh_, Matrix4::Translation(feet + Vector3(0, config_.player.height * 0.5f, 0)), body);
            Renderer::Material head;
            head.color = {0.95f, 0.85f, 0.7f, 1.0f};
            renderer.DrawMesh(head_mesh_, Matrix4::Translation(feet + Vector3(0, config_.player.height - 0.17f, 0)), head);

            Debug::DebugRenderer::Instance().DrawText(feet + Vector3(0, config_.player.height + 0.35f, 0),
                                                      fmt::format("{:.0f}", player->health), {1, 1, 1, 1});
        }
    }

    void OnUI(UI::UIContext& ui) override {
        const float32 w = ui.GetScreenWidth();
        const float32 h = ui.GetScreenHeight();
        const SimPlayer* me = world_.GetPlayer(local_id_);
        if (!me) return;

        if (damage_flash_ > 0.0f) ui.DrawRect({0, 0, w, h}, {0.8f, 0.0f, 0.0f, damage_flash_ * 0.6f});

        if (me->alive) {
            DrawCrosshair(ui, w * 0.5f, h * 0.5f);
        } else {
            ui.DrawRect({0, 0, w, h}, {0, 0, 0, 0.45f});
            const String text = fmt::format("Respawn in {:.1f} s", std::max(0.0f, me->respawn_timer));
            ui.DrawText(text, (w - ui.GetTextWidth(text, 56.0f)) * 0.5f, h * 0.4f, {1, 1, 1, 1}, 56.0f);
        }

        // Health and ammo
        ui.DrawRect({20, h - 76, 210, 56}, {0, 0, 0, 0.5f});
        ui.DrawText(fmt::format("{:.0f}", me->health), 34, h - 70, {1, 1, 1, 1}, 56.0f);
        if (me->IsProtected()) ui.DrawText("Schutz", 130, h - 52, {0.6f, 0.85f, 1.0f, 1.0f}, 28.0f);

        const WeaponState& weapon = me->ActiveWeapon();
        ui.DrawRect({w - 250, h - 76, 230, 56}, {0, 0, 0, 0.5f});
        if (weapon.IsReloading()) {
            const float32 progress = 1.0f - weapon.reload_timer / std::max(0.01f, weapon.def->reload_time);
            ui.DrawText("Nachladen", w - 236, h - 66, {1, 1, 1, 1}, 28.0f);
            ui.DrawRect({w - 236, h - 32, 200 * progress, 6}, {1, 1, 1, 0.9f});
        } else {
            ui.DrawText(fmt::format("{} / {}", weapon.ammo, weapon.def->magazine), w - 236, h - 70,
                        weapon.ammo == 0 ? Vector4(1, 0.3f, 0.3f, 1) : Vector4(1, 1, 1, 1), 56.0f);
        }
        ui.DrawText(weapon.def->name, w - 236, h - 98, {1, 1, 1, 0.8f});

        // Phase 2 check: time to kill the dummy
        ui.DrawRect({10, 10, 330, 74}, {0, 0, 0, 0.5f});
        ui.DrawText(fmt::format("{:.0f} FPS   Tick {}", GetFPS(), world_.GetTick()), 20, 18, {1, 1, 1, 1});
        if (last_kill_hits_ > 0) {
            ui.DrawText(fmt::format("Letzter Kill: {} Treffer in {:.2f} s", last_kill_hits_, last_kill_time_), 20, 44,
                        {0.8f, 1.0f, 0.8f, 1.0f});
        } else {
            ui.DrawText("Ziel: Puppe im Norden", 20, 44, {0.8f, 0.9f, 1.0f, 1.0f});
        }

        if (!IsMouseCaptured()) {
            const String hint = "Klicken zum Spielen  -  Esc beendet";
            ui.DrawText(hint, (w - ui.GetTextWidth(hint, 28.0f)) * 0.5f, h * 0.6f, {1, 1, 1, 1}, 28.0f);
        }
        const String help = "WASD laufen  Leertaste springen  LMB schiessen  R nachladen  Esc Maus frei";
        ui.DrawText(help, (w - ui.GetTextWidth(help)) * 0.5f, h - 22, {1, 1, 1, 0.6f});
    }

private:
    PlayerInput BuildInput() {
        Input::InputSystem& input = GetInput();
        PlayerInput in;
        in.tick = world_.GetTick() + 1;
        in.yaw = look_yaw_;
        in.pitch = look_pitch_;
        if (IsMouseCaptured()) {
            in.move_x = (input.IsKeyDown(Input::KeyCode::D) ? 1.0f : 0.0f) - (input.IsKeyDown(Input::KeyCode::A) ? 1.0f : 0.0f);
            in.move_y = (input.IsKeyDown(Input::KeyCode::W) ? 1.0f : 0.0f) - (input.IsKeyDown(Input::KeyCode::S) ? 1.0f : 0.0f);
            const bool fire_down = input.IsMouseButtonDown(Input::MouseButton::Left) && !swallow_fire_;
            if (fire_down || fire_latch_) in.buttons |= kButtonFire;
            if (input.IsKeyDown(Input::KeyCode::Space) || jump_latch_) in.buttons |= kButtonJump;
            if (reload_latch_) in.buttons |= kButtonReload;
        }
        fire_latch_ = reload_latch_ = jump_latch_ = false;
        return in;
    }

    void HandleEvent(const WorldEvent& event) {
        Audio::AudioSystem& audio = Audio::AudioSystem::Instance();
        Debug::DebugRenderer& debug = Debug::DebugRenderer::Instance();
        switch (event.type) {
            case WorldEventType::Shot: {
                const bool mine = event.source == local_id_;
                // Tracer starts a little below the eye so the shooter can see it
                const Vector3 from = mine ? event.from + Vector3(0, -0.15f, 0) : event.from;
                debug.DrawLine(from, event.to, {1.0f, 0.9f, 0.55f, 1.0f}, 0.05f);
                if (event.hit && event.target == kInvalidPlayer) debug.DrawHitPoint(event.to, event.normal, 1.0f);
                if (mine) audio.PlayOneShot2D(shot_sound_, 0.6f);
                else audio.PlayOneShot(shot_sound_, event.from, 0.8f);
                break;
            }
            case WorldEventType::Damage:
                if (event.source == local_id_) {
                    hit_marker_ = 0.18f;
                    hit_marker_headshot_ = event.headshot;
                    audio.PlayOneShot2D(event.headshot ? headshot_sound_ : hit_sound_, 0.8f);
                    if (event.target == dummy_id_) {
                        if (dummy_hits_ == 0) dummy_first_hit_tick_ = event.tick;
                        ++dummy_hits_;
                    }
                }
                if (event.target == local_id_) damage_flash_ = 0.3f;
                break;
            case WorldEventType::Kill:
                if (event.target == dummy_id_ && event.source == local_id_) {
                    last_kill_hits_ = dummy_hits_;
                    last_kill_time_ = static_cast<float32>(event.tick - dummy_first_hit_tick_) * world_.GetTickInterval();
                    DRX_LOG_INFO("Dummy down: {} hits in {:.2f} s", last_kill_hits_, last_kill_time_);
                    audio.PlayOneShot2D(kill_sound_, 0.8f);
                }
                if (event.target == dummy_id_) dummy_hits_ = 0;
                break;
            case WorldEventType::Respawn:
                if (event.target == local_id_) SnapLookToSpawn();
                break;
            case WorldEventType::EmptyClick:
                if (event.source == local_id_) audio.PlayOneShot2D(click_sound_, 0.6f);
                break;
            case WorldEventType::ReloadStart:
            case WorldEventType::ReloadDone:
                if (event.source == local_id_) audio.PlayOneShot2D(click_sound_, 0.4f, 0.7f);
                break;
        }
    }

    void SnapLookToSpawn() {
        look_yaw_ = world_.GetSpawn(Team::A).yaw;
        look_pitch_ = 0.0f;
        if (const SimPlayer* me = world_.GetPlayer(local_id_)) look_yaw_ = world_.GetSpawn(me->team).yaw;
    }

    void RememberPositions() {
        for (const auto& [id, player] : world_.GetPlayers()) previous_feet_[id] = player->GetFeet();
    }

    Vector3 InterpolatedFeet(const SimPlayer& player) const {
        const auto it = previous_feet_.find(player.id);
        const Vector3 current = player.GetFeet();
        if (it == previous_feet_.end()) return current;
        const Vector3 delta = current - it->second;
        if (delta.LengthSquared() > 4.0f) return current;   // teleport (respawn)
        return it->second + delta * GetInterpolationAlpha();
    }

    void UpdateCamera() {
        const SimPlayer* me = world_.GetPlayer(local_id_);
        if (!me) return;
        const Vector3 eye = InterpolatedFeet(*me) + Vector3(0, config_.player.eye_height, 0);
        GetCamera() = Renderer::CameraParams::LookAt(eye, eye + LookDirection(look_yaw_, look_pitch_),
                                                     config_.player.fov * kDegToRad, GetAspectRatio(), 0.03f, 300.0f);
    }

    void DrawCrosshair(UI::UIContext& ui, float32 cx, float32 cy) const {
        const Vector4 color{1, 1, 1, 0.9f};
        ui.DrawLine({cx - 10, cy}, {cx - 4, cy}, color, 2.0f);
        ui.DrawLine({cx + 4, cy}, {cx + 10, cy}, color, 2.0f);
        ui.DrawLine({cx, cy - 10}, {cx, cy - 4}, color, 2.0f);
        ui.DrawLine({cx, cy + 4}, {cx, cy + 10}, color, 2.0f);
        if (hit_marker_ <= 0.0f) return;
        const Vector4 marker = hit_marker_headshot_ ? Vector4(1.0f, 0.25f, 0.2f, 1.0f) : Vector4(1, 1, 1, 1);
        for (const float32 sx : {-1.0f, 1.0f}) {
            for (const float32 sy : {-1.0f, 1.0f}) {
                ui.DrawLine({cx + sx * 7, cy + sy * 7}, {cx + sx * 15, cy + sy * 15}, marker, 2.5f);
            }
        }
    }

    void CreateGraphics() {
        Renderer::SceneRenderer& renderer = GetRenderer();
        const auto grid = Renderer::MeshBuilder::GridTexture(256, 4, {0.82f, 0.82f, 0.84f, 1.0f},
                                                             {0.62f, 0.62f, 0.66f, 1.0f}, {0.5f, 0.5f, 0.54f, 1.0f});
        grid_texture_ = renderer.CreateTexture(256, 256, grid.data(), true, true, "Graybox");

        for (const LevelBox& box : level_.boxes) {
            const MeshHandle mesh = renderer.CreateMesh(Renderer::MeshBuilder::Box(box.size), "LevelBox");
            if (!mesh.IsValid()) continue;
            meshes_.push_back(mesh);
            RenderBox render;
            render.mesh = mesh;
            render.transform = Matrix4::Translation(box.center);
            render.material.albedo = grid_texture_;
            render.material.color = SurfaceColor(box.surface);
            render.material.cast_shadows = box.casts_shadow;
            level_boxes_.push_back(render);
        }

        const float32 r = config_.player.radius;
        body_mesh_ = renderer.CreateMesh(Renderer::MeshBuilder::Capsule(r, config_.player.height), "Body");
        head_mesh_ = renderer.CreateMesh(Renderer::MeshBuilder::Sphere(0.16f), "Head");
        meshes_.push_back(body_mesh_);
        meshes_.push_back(head_mesh_);
    }

    void CreateSounds() {
        Audio::AudioSystem& audio = Audio::AudioSystem::Instance();
        shot_sound_ = audio.CreateClip("shift:pistol", Audio::Synth::Gunshot(0.7f, 3));
        hit_sound_ = audio.CreateClip("shift:hit", Audio::Synth::Hit());
        headshot_sound_ = audio.CreateClip("shift:headshot", Audio::Synth::Headshot());
        click_sound_ = audio.CreateClip("shift:click", Audio::Synth::Click());
        Audio::Synth::ToneParams kill;
        kill.waveform = Audio::Synth::Waveform::Triangle;
        kill.start_frequency = 660.0f;
        kill.end_frequency = 990.0f;
        kill.duration = 0.25f;
        kill.volume = 0.6f;
        kill_sound_ = audio.CreateClip("shift:kill", Audio::Synth::Tone(kill));
    }

    ShiftConfig config_;
    ShiftWorld world_;
    Level level_;
    PlayerId local_id_ = kInvalidPlayer;
    PlayerId dummy_id_ = kInvalidPlayer;
    std::map<PlayerId, Vector3> previous_feet_;

    float32 look_yaw_ = 0.0f;
    float32 look_pitch_ = 0.0f;
    bool fire_latch_ = false;
    bool reload_latch_ = false;
    bool jump_latch_ = false;
    bool swallow_fire_ = false;

    float32 hit_marker_ = 0.0f;
    bool hit_marker_headshot_ = false;
    float32 damage_flash_ = 0.0f;
    uint32 dummy_hits_ = 0;
    uint32 dummy_first_hit_tick_ = 0;
    uint32 last_kill_hits_ = 0;
    float32 last_kill_time_ = 0.0f;

    Array<MeshHandle> meshes_;
    Array<RenderBox> level_boxes_;
    TextureHandle grid_texture_;
    MeshHandle body_mesh_;
    MeshHandle head_mesh_;
    SharedPtr<Audio::AudioClip> shot_sound_, hit_sound_, headshot_sound_, click_sound_, kill_sound_;
};

} // namespace
} // namespace Shift

int main(int argc, char** argv) {
    DRX::Runtime::ApplicationConfig config;
    config.title = "SHIFT";
    config.config_path = SHIFT_CONFIG_PATH;
    Shift::ShiftApp app(std::move(config));
    return app.Run(argc, argv);
}

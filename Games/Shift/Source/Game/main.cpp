/**
 * SHIFT - Game client
 *
 * Modes (menu at start, or command line):
 *   1 / --offline            against a computer opponent
 *   2 / --host               host a duel: you play on the server, a friend joins
 *   3 / --connect [ip:port]  join a host (default: net.server_address from shift.json)
 *
 * Controls: mouse look, WASD move, Space jump, LMB fire, R reload, hold E to pick up the core,
 *           Esc releases the mouse (Esc again quits), click to capture, F11 fullscreen, F12 screenshot
 */

#include "../Core/ShiftBot.h"
#include "../Core/ShiftConfig.h"
#include "../Core/ShiftLevel.h"
#include "../Core/ShiftNet.h"
#include "../Core/ShiftWorld.h"

#include "Audio/Public/SoundSynth.h"
#include "Core/Public/Log.h"
#include "Core/Public/Platform.h"
#include "Debug/Public/DebugSystem.h"
#include "Renderer/Public/MeshBuilder.h"
#include "Renderer/Public/ModelLoader.h"
#include "Renderer/Public/SkinnedModel.h"
#include "Runtime/Public/Application.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>

using namespace DRX;
using Math::Matrix4;
using Math::Vector3;
using Math::Vector4;

namespace Shift {
namespace {

constexpr float32 kPi = 3.14159265358979323846f;
constexpr float32 kDegToRad = kPi / 180.0f;

enum class Mode { Menu, Offline, Host, Client };

struct LaunchOptions {
    Mode mode = Mode::Menu;
    String address;      // client: "host[:port]"
    String asset_dir;    // folder with Models/ etc.
};

// Weapon model in its own space: centred, barrel along -Z, top +Y, real length in metres
struct ViewModel {
    Renderer::Model model;
    Matrix4 local;      // orientation and centring
    float32 length = 0.0f;
};

Vector4 SurfaceColor(Surface surface) {
    switch (surface) {
        case Surface::Floor: return {0.55f, 0.56f, 0.58f, 1.0f};
        case Surface::Wall: return {0.72f, 0.72f, 0.74f, 1.0f};
        case Surface::Ceiling: return {0.6f, 0.6f, 0.62f, 1.0f};
        case Surface::Cover: return {0.85f, 0.55f, 0.25f, 1.0f};
        case Surface::Step: return {0.6f, 0.75f, 0.6f, 1.0f};
        case Surface::SpawnA: return {0.2f, 0.45f, 1.0f, 1.0f};
        case Surface::SpawnB: return {1.0f, 0.3f, 0.25f, 1.0f};
        case Surface::Moving: return {0.55f, 0.6f, 0.75f, 1.0f};
    }
    return {1, 1, 1, 1};
}

Vector4 TeamColor(Team team) {
    return team == Team::A ? Vector4(0.25f, 0.5f, 1.0f, 1.0f) : Vector4(0.95f, 0.3f, 0.25f, 1.0f);
}

const char* TeamName(Team team) {
    return team == Team::A ? "Team Blau" : "Team Rot";
}

struct RenderBox {
    MeshHandle mesh;
    Matrix4 transform;
    Renderer::Material material;
};

// What the presentation needs of a player, from the authoritative world or the network
struct PlayerView {
    PlayerId id = kInvalidPlayer;
    Team team = Team::A;
    Vector3 feet;
    Vector3 velocity;
    float32 yaw = 0.0f;
    bool grounded = true;
    bool alive = false;
    bool is_protected = false;
    float32 health = 0.0f;
    String weapon;      // id of the weapon in the hands
};

// Animated third person character: one rig, named clips, cross-fade between them
struct CharacterRig {
    Renderer::SkinnedModelData model;
    std::map<String, Renderer::AnimationClipData> clips;
    std::map<String, Renderer::ClipBinding> bindings;
    float32 scale = 1.0f;
    float32 foot_offset = 0.0f;
    float32 yaw_offset = 180.0f;
    float32 blend_time = 0.2f;
    int32 right_hand = -1;
    int32 left_hand = -1;
    bool loaded = false;
};

struct CharacterInstance {
    Renderer::SkinnedMesh mesh;
    String clip;
    String previous_clip;
    float32 time = 0.0f;
    float32 previous_time = 0.0f;
    float32 blend = 1.0f;
    Renderer::SkeletonPose pose_a, pose_b, pose;
    Array<Matrix4> globals;
};

class ShiftApp final : public Runtime::Application {
public:
    ShiftApp(Runtime::ApplicationConfig config, LaunchOptions launch)
        : Application(std::move(config)), launch_(std::move(launch)) {}

protected:
    bool OnInitialize() override {
        config_.Load(GetConfig());
        SetFixedTimestep(config_.GetTickInterval());
        CreateSounds();
        GetEnvironment().fog_density = 0.0f;

        switch (launch_.mode) {
            case Mode::Offline: return StartOffline();
            case Mode::Host: return StartHost();
            case Mode::Client: return StartClient(launch_.address);
            case Mode::Menu: break;
        }
        SetMouseCaptured(false);
        return true;
    }

    void OnShutdown() override {
        if (mode_ == Mode::Client) client_.Disconnect(Platform::GetTimeSeconds());
        server_.Stop();
        world_.Shutdown();
        DestroyGraphics();
    }

    void OnUpdate(float32 dt) override {
        Input::InputSystem& input = GetInput();
        if (mode_ == Mode::Menu) {
            UpdateMenu();
            return;
        }
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
            const float32 sensitivity = config_.player.mouse_sensitivity / zoom_;
            look_yaw_ = std::fmod(look_yaw_ + delta.x * sensitivity, 360.0f);
            look_pitch_ = std::clamp(look_pitch_ - delta.y * sensitivity, -89.0f, 89.0f);

            // Latch short presses so they are not lost between ticks
            if (input.IsMouseButtonPressed(Input::MouseButton::Left) && !swallow_fire_) fire_latch_ = true;
            if (input.IsKeyPressed(Input::KeyCode::R)) reload_latch_ = true;
            if (input.IsKeyPressed(Input::KeyCode::Space)) jump_latch_ = true;
            if (input.IsKeyPressed(Input::KeyCode::Num1)) slot_latch_ = kButtonSlotPrimary;
            if (input.IsKeyPressed(Input::KeyCode::Num2)) slot_latch_ = kButtonSlotPistol;
            if (input.IsKeyPressed(Input::KeyCode::B)) loadout_ = static_cast<uint8>((loadout_ + 1) % kLoadoutCount);
        }
        if (!input.IsMouseButtonDown(Input::MouseButton::Left)) swallow_fire_ = false;

        hit_marker_ = std::max(0.0f, hit_marker_ - dt);
        recoil_ = std::max(0.0f, recoil_ - dt * 9.0f);
        bob_phase_ += dt * view_speed_ * 1.8f;
        damage_flash_ = std::max(0.0f, damage_flash_ - dt);
        announcement_time_ = std::max(0.0f, announcement_time_ - dt);
        UpdateCamera();
    }

    void OnFixedUpdate(float32 /*dt*/) override {
        const float64 now = Platform::GetTimeSeconds();
        switch (mode_) {
            case Mode::Menu:
                return;
            case Mode::Offline:
            case Mode::Host: {
                if (mode_ == Mode::Host) server_.Receive(now);
                world_.SetInput(local_id_, BuildInput());
                bots_.Update(world_);
                RememberPositions();
                world_.Step();
                const Array<WorldEvent> events = world_.TakeEvents();
                if (mode_ == Mode::Host) server_.Send(events, now);
                for (const WorldEvent& event : events) HandleEvent(event);
                return;
            }
            case Mode::Client: {
                client_.Receive(now);
                if (client_.IsReady() && !graphics_ready_) {
                    local_id_ = client_.GetLocalId();
                    level_ = client_.GetLevel();
                    CreateGraphics();
                    SnapLookToSpawn();
                    SetMouseCaptured(true);
                }
                RememberPositions();
                client_.Tick(BuildInput(), now);
                client_.Flush(now);
                for (const WorldEvent& event : client_.TakeEvents()) HandleEvent(event);
                return;
            }
        }
    }

    void OnRender(Renderer::SceneRenderer& renderer) override {
        if (!graphics_ready_) return;
        for (const RenderBox& box : level_boxes_) renderer.DrawMesh(box.mesh, box.transform, box.material);
        DrawSegments(renderer);
        DrawExitsAndCore(renderer);

        DrawViewModel(renderer);

        const float64 now = GetTime();
        const float32 frame_dt = static_cast<float32>(std::clamp(now - last_render_time_, 0.0, 0.1));
        last_render_time_ = now;
        for (const PlayerView& player : CollectPlayers()) {
            if (player.id == local_id_) continue;
            if (rig_.loaded) {
                DrawCharacter(renderer, player, frame_dt);
                if (player.alive) {
                    Debug::DebugRenderer::Instance().DrawText(player.feet + Vector3(0, config_.player.height + 0.35f, 0),
                                                              fmt::format("{:.0f}", player.health), {1, 1, 1, 1});
                }
                continue;
            }
            if (!player.alive) continue;
            Renderer::Material body;
            body.color = TeamColor(player.team);
            if (player.is_protected) {
                body.emissive = Vector3(0.6f, 0.8f, 1.0f);
                body.emissive_strength = 0.6f;
            }
            renderer.DrawMesh(body_mesh_, Matrix4::Translation(player.feet + Vector3(0, config_.player.height * 0.5f, 0)), body);
            Renderer::Material head;
            head.color = {0.95f, 0.85f, 0.7f, 1.0f};
            renderer.DrawMesh(head_mesh_, Matrix4::Translation(player.feet + Vector3(0, config_.player.height - 0.17f, 0)), head);
            Debug::DebugRenderer::Instance().DrawText(player.feet + Vector3(0, config_.player.height + 0.35f, 0),
                                                      fmt::format("{:.0f}", player.health), {1, 1, 1, 1});
        }
    }

    void OnUI(UI::UIContext& ui) override {
        const float32 w = ui.GetScreenWidth();
        const float32 h = ui.GetScreenHeight();
        if (mode_ == Mode::Menu) {
            DrawMenu(ui, w, h);
            return;
        }
        if (mode_ == Mode::Client && !client_.IsReady()) {
            DrawConnecting(ui, w, h);
            return;
        }
        const SimPlayer* me = SimWorld().GetPlayer(local_id_);
        if (!me) return;

        if (damage_flash_ > 0.0f) ui.DrawRect({0, 0, w, h}, {0.8f, 0.0f, 0.0f, damage_flash_ * 0.6f});
        if (me->alive) {
            DrawCrosshair(ui, w * 0.5f, h * 0.5f);
            DrawCoreMarkers(ui, w, h);
        } else {
            ui.DrawRect({0, 0, w, h}, {0, 0, 0, 0.45f});
            if (crushed_) CenterText(ui, "Zerquetscht", h * 0.3f, {1.0f, 0.35f, 0.3f, 1.0f}, 56.0f);
            CenterText(ui, fmt::format("Respawn in {:.1f} s", std::max(0.0f, me->respawn_timer)), h * 0.4f, {1, 1, 1, 1}, 56.0f);
        }
        DrawTopBar(ui, w);
        DrawRoundMessages(ui, w, h);
        DrawPickup(ui, w, h, *me);

        // Health and ammo
        ui.DrawRect({20, h - 76, 210, 56}, {0, 0, 0, 0.5f});
        ui.DrawText(fmt::format("{:.0f}", me->health), 34, h - 70, {1, 1, 1, 1}, 56.0f);
        if (me->IsProtected()) ui.DrawText("Schutz", 130, h - 52, {0.6f, 0.85f, 1.0f, 1.0f}, 28.0f);

        const WeaponState& weapon = SimWorld().IsCarrier(local_id_) ? me->pistol : me->ActiveWeapon();
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
        if (const WeaponDef* next = SimWorld().GetLoadoutWeapon(loadout_)) {
            const bool pending = me->primary_index != loadout_;
            ui.DrawText(fmt::format("Hauptwaffe: {}{}  (B)", next->name, pending ? " ab Respawn" : ""), w - 360, h - 124,
                        pending ? Vector4(1.0f, 0.85f, 0.4f, 1.0f) : Vector4(1, 1, 1, 0.6f));
        }

        DrawStatus(ui);
        if (!IsMouseCaptured()) CenterText(ui, "Klicken zum Spielen  -  Esc beendet", h * 0.62f, {1, 1, 1, 1}, 28.0f);
        const String help = "WASD laufen  Leertaste springen  LMB schiessen  RMB zielen  R nachladen  1/2 Waffe  B Hauptwaffe  E Kern  Esc Maus frei";
        ui.DrawText(help, (w - ui.GetTextWidth(help)) * 0.5f, h - 22, {1, 1, 1, 0.6f});
    }

private:
    // ------------------------------------------------------------------ modes

    ShiftWorld& SimWorld() { return mode_ == Mode::Client ? client_.GetWorld() : world_; }
    const ShiftWorld& SimWorld() const { return mode_ == Mode::Client ? client_.GetWorld() : world_; }

    bool StartWorld(bool with_bot) {
        if (!world_.Initialize(config_, true)) return false;
        level_ = BuildLevel(config_.round.map);
        level_.Apply(world_);
        local_id_ = world_.AddPlayer(Team::A);
        if (local_id_ == kInvalidPlayer) return false;
        bots_.Clear();
        if (with_bot) {
            bots_.Build(world_, level_);
            if (bots_.AddBot(world_, Team::B) == kInvalidPlayer) return false;
        }
        world_.StartMatch(NewSeed());
        world_.TakeEvents();
        CreateGraphics();
        SnapLookToSpawn();
        RememberPositions();
        SetMouseCaptured(true);
        return true;
    }

    bool StartOffline() {
        mode_ = Mode::Offline;
        return StartWorld(true);
    }

    bool StartHost() {
        mode_ = Mode::Host;
        if (!StartWorld(false)) return false;
        server_.on_player_change = [this](PlayerId, bool joined) {
            // A new duel starts whenever somebody joins
            if (joined) world_.StartMatch(NewSeed());
        };
        if (!server_.Start(world_, config_.round.map, static_cast<uint16>(config_.net.port), Conditioner())) {
            status_error_ = fmt::format("Port {} ist belegt", config_.net.port);
        }
        return true;
    }

    bool StartClient(const String& address_text) {
        mode_ = Mode::Client;
        String text = address_text.empty() ? config_.net.server_address : address_text;
        if (text.find(':') == String::npos) text += fmt::format(":{}", config_.net.port);
        connect_target_ = text;
        Network::Address address;
        if (!Network::Address::Parse(text, address)) {
            status_error_ = fmt::format("Adresse '{}' ungueltig", text);
            return true;
        }
        client_.Connect(config_, address, Platform::GetTimeSeconds(), BuildLevel, Conditioner());
        return true;
    }

    Network::ConditionerSettings Conditioner() const {
        Network::ConditionerSettings c;
        c.latency_ms = config_.net.simulated_latency_ms;
        c.jitter_ms = config_.net.simulated_jitter_ms;
        c.loss = config_.net.simulated_loss;
        return c;
    }

    static uint32 NewSeed() {
        return HashU32(static_cast<uint32>(Platform::GetTimeMilliseconds()), 0x5348u, 0x4946u) | 1u;
    }

    void UpdateMenu() {
        Input::InputSystem& input = GetInput();
        bool ok = true;
        if (input.IsKeyPressed(Input::KeyCode::Num1)) ok = StartOffline();
        else if (input.IsKeyPressed(Input::KeyCode::Num2)) ok = StartHost();
        else if (input.IsKeyPressed(Input::KeyCode::Num3)) ok = StartClient("");
        else if (input.IsKeyPressed(Input::KeyCode::M)) {
            static const char* const kMaps[] = {"arena", "fabrik", "testroom"};
            size_t next = 0;
            for (size_t i = 0; i < 3; ++i) {
                if (config_.round.map == kMaps[i]) next = (i + 1) % 3;
            }
            config_.round.map = kMaps[next];
        } else if (input.IsKeyPressed(Input::KeyCode::Escape)) RequestQuit();
        if (!ok) {
            DRX_LOG_ERROR("SHIFT: could not start the game");
            RequestQuit();
        }
    }

    // ------------------------------------------------------------------ input and players

    PlayerInput BuildInput() {
        Input::InputSystem& input = GetInput();
        PlayerInput in;
        in.tick = SimWorld().GetTick() + 1;
        in.yaw = look_yaw_;
        in.pitch = look_pitch_;
        if (IsMouseCaptured()) {
            in.move_x = (input.IsKeyDown(Input::KeyCode::D) ? 1.0f : 0.0f) - (input.IsKeyDown(Input::KeyCode::A) ? 1.0f : 0.0f);
            in.move_y = (input.IsKeyDown(Input::KeyCode::W) ? 1.0f : 0.0f) - (input.IsKeyDown(Input::KeyCode::S) ? 1.0f : 0.0f);
            const bool fire_down = input.IsMouseButtonDown(Input::MouseButton::Left) && !swallow_fire_;
            if (fire_down || fire_latch_) in.buttons |= kButtonFire;
            if (input.IsKeyDown(Input::KeyCode::Space) || jump_latch_) in.buttons |= kButtonJump;
            if (reload_latch_) in.buttons |= kButtonReload;
            if (input.IsKeyDown(Input::KeyCode::E)) in.buttons |= kButtonInteract;
            if (input.IsMouseButtonDown(Input::MouseButton::Right)) in.buttons |= kButtonAim;
            in.buttons |= slot_latch_;
        }
        in.loadout = loadout_;
        fire_latch_ = reload_latch_ = jump_latch_ = false;
        slot_latch_ = 0;
        return in;
    }

    Array<PlayerView> CollectPlayers() const {
        Array<PlayerView> views;
        for (const auto& [id, player] : SimWorld().GetPlayers()) {
            PlayerView v;
            v.id = id;
            v.team = player->team;
            v.feet = InterpolatedFeet(*player);
            v.velocity = player->controller->GetVelocity();
            v.yaw = player->yaw;
            v.grounded = player->IsGrounded();
            v.alive = player->alive;
            v.is_protected = player->IsProtected();
            v.health = player->health;
            const WeaponState& held = SimWorld().IsCarrier(id) ? player->pistol : player->ActiveWeapon();
            if (held.def) v.weapon = held.def->id;
            views.push_back(v);
        }
        if (mode_ == Mode::Client) {
            const float64 render_tick = client_.GetRenderTick(Platform::GetTimeSeconds());
            for (const PlayerId id : client_.GetRemotePlayerIds()) {
                RemotePlayerState state;
                if (!client_.GetRemotePlayer(id, render_tick, state)) continue;
                PlayerView v;
                v.id = id;
                v.team = state.team;
                v.feet = state.feet;
                v.velocity = state.velocity;
                v.yaw = state.yaw;
                v.grounded = state.grounded;
                v.alive = state.alive;
                v.is_protected = state.protection_timer > 0.0f;
                v.health = state.health;
                const WeaponDef* primary = SimWorld().GetLoadoutWeapon(state.primary_index);
                const bool carrier = SimWorld().IsCarrier(id);
                v.weapon = state.slot == 0 && primary && !carrier ? primary->id : String("pistol");
                views.push_back(v);
            }
        }
        return views;
    }

    bool FindPlayer(PlayerId id, PlayerView& out) const {
        for (const PlayerView& v : CollectPlayers()) {
            if (v.id == id) {
                out = v;
                return true;
            }
        }
        return false;
    }

    Team LocalTeam() const {
        const SimPlayer* me = SimWorld().GetPlayer(local_id_);
        return me ? me->team : Team::A;
    }

    // ------------------------------------------------------------------ events

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
                // One synthesized shot, pitched per weapon: rifle, shotgun, sniper, pistol
                static constexpr float32 kPitch[] = {1.25f, 0.7f, 0.55f, 1.0f};
                const float32 pitch = kPitch[std::min<uint8>(event.weapon, 3)];
                if (mine) recoil_ = 1.0f;
                if (mine) audio.PlayOneShot2D(shot_sound_, 0.6f, pitch);
                else audio.PlayOneShot(shot_sound_, event.from, 0.8f, pitch);
                break;
            }
            case WorldEventType::Damage:
                if (event.source == local_id_) {
                    hit_marker_ = 0.18f;
                    hit_marker_headshot_ = event.headshot;
                    audio.PlayOneShot2D(event.headshot ? headshot_sound_ : hit_sound_, 0.8f);
                }
                if (event.target == local_id_) damage_flash_ = 0.3f;
                break;
            case WorldEventType::Kill:
                if (event.target == local_id_) crushed_ = event.crushed;
                if (event.crushed) audio.PlayOneShot(crush_sound_, event.to, 1.0f);
                if (event.source == local_id_ && event.target != local_id_) audio.PlayOneShot2D(kill_sound_, 0.8f);
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
            case WorldEventType::ShiftWarning:
                audio.PlayOneShot2D(siren_sound_, 0.7f);
                break;
            case WorldEventType::ShiftStart:
                audio.PlayOneShot2D(rumble_sound_, 0.6f);
                break;
            case WorldEventType::CorePickup:
                audio.PlayOneShot2D(pickup_sound_, 0.8f);
                Announce(event.source == local_id_ ? String("Du hast den Kern!") : String("Kern aufgenommen"));
                break;
            case WorldEventType::CoreDrop:
                audio.PlayOneShot(drop_sound_, event.to, 1.0f);
                Announce("Kern fallen gelassen");
                break;
            case WorldEventType::CoreReturn:
                audio.PlayOneShot2D(drop_sound_, 0.6f);
                Announce("Kern zurueck in der Kernkammer");
                break;
            case WorldEventType::CoreMoved:
                break;
            case WorldEventType::RoundStart:
                crushed_ = false;
                SnapLookToSpawn();
                break;
            case WorldEventType::RoundEnd:
            case WorldEventType::MatchEnd:
                audio.PlayOneShot2D(event.hit || event.type == WorldEventType::MatchEnd ? fanfare_sound_ : drop_sound_, 0.9f);
                break;
        }
    }

    void Announce(const String& text) {
        announcement_ = text;
        announcement_time_ = 2.5f;
    }

    void SnapLookToSpawn() {
        look_yaw_ = SimWorld().GetSpawn(LocalTeam()).yaw;
        look_pitch_ = 0.0f;
    }

    void RememberPositions() {
        for (const auto& [id, player] : SimWorld().GetPlayers()) previous_feet_[id] = player->GetFeet();
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
        const SimPlayer* me = SimWorld().GetPlayer(local_id_);
        if (!me) return;
        const Vector3 eye = InterpolatedFeet(*me) + Vector3(0, config_.player.eye_height, 0);
        // Aiming (right mouse) zooms weapons that have a scope
        const WeaponState& weapon = SimWorld().IsCarrier(local_id_) ? me->pistol : me->ActiveWeapon();
        const bool aiming = me->alive && IsMouseCaptured() && GetInput().IsMouseButtonDown(Input::MouseButton::Right);
        const float32 zoom = aiming && weapon.def ? weapon.def->zoom : 1.0f;
        GetCamera() = Renderer::CameraParams::LookAt(eye, eye + LookDirection(look_yaw_, look_pitch_),
                                                     config_.player.fov / zoom * kDegToRad, GetAspectRatio(), 0.03f, 300.0f);
        zoom_ = zoom;
        view_eye_ = eye;
        aiming_ = aiming;
        const Vector3 v = me->controller->GetVelocity();
        view_speed_ = me->IsGrounded() ? std::sqrt(v.x * v.x + v.z * v.z) : 0.0f;
    }

    // ------------------------------------------------------------------ world drawing

    // Segments are drawn at the interpolated round time so they move smoothly between ticks
    void DrawSegments(Renderer::SceneRenderer& renderer) {
        const ShiftWorld& world = SimWorld();
        const float32 time = world.GetRoundTime() - world.GetTickInterval() * (1.0f - GetInterpolationAlpha());
        const GroupMask warned = world.GetUpcomingGroups();
        const auto& segments = world.GetSegments();
        const float32 pulse = 0.5f + 0.5f * std::sin(static_cast<float32>(GetTime()) * 8.0f);
        for (size_t i = 0; i < segments.size() && i < segment_meshes_.size(); ++i) {
            const MovingSegmentDef& def = segments[i].def;
            const SegmentPose pose = def.PoseAt(SegmentAmount(config_.shifts, world.GetPlan(), def, time));
            Renderer::Material material;
            material.albedo = grid_texture_;
            material.color = SurfaceColor(Surface::Moving);
            if (warned & GroupBit(def.group)) {
                // Pulsing red: this wall moves in the next shift
                material.color = {0.9f, 0.25f, 0.2f, 1.0f};
                material.emissive = {1.0f, 0.1f, 0.05f};
                material.emissive_strength = 0.6f + 1.4f * pulse;
            }
            renderer.DrawMesh(segment_meshes_[i], Matrix4::Translation(pose.center) * pose.Rotation().ToMatrix(), material);
        }
    }

    // Weapon in the hand: camera space offset, walk bob, recoil kick and a dip while reloading
    void DrawViewModel(Renderer::SceneRenderer& renderer) {
        const SimPlayer* me = SimWorld().GetPlayer(local_id_);
        if (!me || !me->alive) return;
        const WeaponState& weapon = SimWorld().IsCarrier(local_id_) ? me->pistol : me->ActiveWeapon();
        if (!weapon.def) return;
        const auto it = view_models_.find(weapon.def->id);
        if (it == view_models_.end()) return;
        if (aiming_ && weapon.def->zoom > 1.5f) return;   // scope view

        const WeaponDef& def = *weapon.def;
        float32 right = aiming_ ? 0.0f : def.view_right;
        float32 down = aiming_ ? def.view_down * 0.6f : def.view_down;
        float32 forward = def.view_forward;
        float32 tilt = 0.0f;
        if (weapon.IsReloading()) {
            const float32 progress = 1.0f - weapon.reload_timer / std::max(0.01f, def.reload_time);
            const float32 dip = std::sin(progress * kPi);
            down += 0.18f * dip;
            tilt = 25.0f * dip;
        }
        const float32 bob = std::min(1.0f, view_speed_ / 5.5f);
        right += 0.012f * bob * std::sin(bob_phase_);
        down += 0.010f * bob * std::abs(std::cos(bob_phase_));
        forward -= 0.05f * recoil_;
        down -= 0.012f * recoil_;

        const Math::Quaternion view = Math::Quaternion::FromAxisAngle(Vector3::Up, -look_yaw_ * kDegToRad) *
                                      Math::Quaternion::FromAxisAngle(Vector3::Right, (look_pitch_ + 3.0f * recoil_) * kDegToRad);
        const Matrix4 transform = Matrix4::Translation(view_eye_) * view.ToMatrix() *
                                  Matrix4::Translation(Vector3(right, -down, -forward)) *
                                  Math::Quaternion::FromAxisAngle(Vector3::Forward, tilt * kDegToRad).ToMatrix() *
                                  it->second.local;
        Renderer::DrawModel(renderer, it->second.model, transform);
    }

    // ------------------------------------------------------------------ characters

    void LoadCharacterRig() {
        const Config& config = GetConfig();
        const String model = config.GetString("character.model", "");
        if (model.empty()) return;
        const auto asset = [this](const String& file) { return (std::filesystem::path(launch_.asset_dir) / file).string(); };
        if (!Renderer::LoadSkinnedModelData(asset(model), rig_.model)) return;

        const float32 height = rig_.model.bounds.max.y - rig_.model.bounds.min.y;
        rig_.scale = height > 0.0f ? config.GetFloat("character.height", config_.player.height) / height : 1.0f;
        rig_.foot_offset = -rig_.model.bounds.min.y;
        rig_.yaw_offset = config.GetFloat("character.yaw_offset", 180.0f);
        rig_.blend_time = std::max(0.01f, config.GetFloat("character.blend_time", 0.2f));
        for (const char* name : {"idle", "run", "back", "left", "right", "jump", "death"}) {
            const String file = config.GetString(String("character.clips.") + name, "");
            if (file.empty()) continue;
            Array<Renderer::AnimationClipData> clips;
            if (!Renderer::LoadAnimationClips(asset(file), clips) || clips.empty()) continue;
            rig_.bindings[name] = Renderer::BindClip(rig_.model, clips[0]);
            rig_.clips[name] = std::move(clips[0]);
        }
        rig_.right_hand = rig_.model.FindNode("righthand");
        rig_.left_hand = rig_.model.FindNode("lefthand");
        rig_.loaded = rig_.clips.count("idle") > 0;
        DRX_LOG_INFO("SHIFT: character {} with {} clips ({})", model, rig_.clips.size(), rig_.loaded ? "ok" : "no idle clip");
    }

    // Clip for what the player is doing: dead, in the air, standing, or moving in a direction
    String ChooseClip(const PlayerView& player) const {
        const auto has = [this](const char* name) { return rig_.clips.count(name) > 0; };
        if (!player.alive) return has("death") ? "death" : "idle";
        if (!player.grounded && has("jump")) return "jump";
        const Vector3 v(player.velocity.x, 0.0f, player.velocity.z);
        if (v.LengthSquared() < 0.36f) return "idle";
        const float32 forward = Vector3::Dot(v, FlatForward(player.yaw));
        const float32 right = Vector3::Dot(v, FlatRight(player.yaw));
        const char* clip = std::abs(forward) >= std::abs(right) ? (forward >= 0.0f ? "run" : "back")
                                                                  : (right >= 0.0f ? "right" : "left");
        return has(clip) ? clip : "idle";
    }

    void DrawCharacter(Renderer::SceneRenderer& renderer, const PlayerView& player, float32 dt) {
        CharacterInstance& character = characters_[player.id];
        if (!character.mesh.IsValid() && !character.mesh.Create(renderer, rig_.model, fmt::format("player{}", player.id))) {
            return;
        }
        const String clip = ChooseClip(player);
        if (clip != character.clip) {
            character.previous_clip = character.clip;
            character.previous_time = character.time;
            character.clip = clip;
            character.time = 0.0f;
            character.blend = character.previous_clip.empty() ? 1.0f : 0.0f;
        }
        character.time += dt;
        character.previous_time += dt;
        character.blend = std::min(1.0f, character.blend + dt / rig_.blend_time);

        const auto sample = [this](const String& name, float32 time, Renderer::SkeletonPose& pose) {
            Renderer::BindPose(rig_.model, pose);
            Renderer::SampleOptions options;
            options.loop = name != "death" && name != "jump";
            Renderer::SampleClip(rig_.model, rig_.clips.at(name), rig_.bindings.at(name), time, pose, options);
        };
        sample(character.clip, character.time, character.pose_b);
        if (character.blend < 1.0f && rig_.clips.count(character.previous_clip)) {
            sample(character.previous_clip, character.previous_time, character.pose_a);
            Renderer::BlendPoses(character.pose_a, character.pose_b, character.blend, character.pose);
        } else {
            character.pose = character.pose_b;
        }
        Renderer::ComputeGlobals(rig_.model, character.pose, character.globals);
        character.mesh.Update(renderer, rig_.model, character.globals);

        const Matrix4 transform = Matrix4::Translation(player.feet) *
                                  Math::Quaternion::FromAxisAngle(Vector3::Up, (rig_.yaw_offset - player.yaw) * kDegToRad).ToMatrix() *
                                  Matrix4::Scale(rig_.scale) * Matrix4::Translation(Vector3(0.0f, rig_.foot_offset, 0.0f));
        const Vector4 tint = player.team == Team::A ? Vector4(0.8f, 0.9f, 1.15f, 1.0f) : Vector4(1.15f, 0.85f, 0.8f, 1.0f);
        character.mesh.Draw(renderer, transform, tint);
        if (player.alive) DrawHeldWeapon(renderer, player, character, transform);
    }

    // Weapon in the character's hands: grip in the right hand, barrel towards the left hand
    void DrawHeldWeapon(Renderer::SceneRenderer& renderer, const PlayerView& player, const CharacterInstance& character,
                        const Matrix4& transform) {
        if (rig_.right_hand < 0 || rig_.left_hand < 0) return;
        const auto it = view_models_.find(player.weapon);
        if (it == view_models_.end()) return;
        const Vector3 right_hand = transform.TransformPoint(
            character.globals[static_cast<size_t>(rig_.right_hand)].TransformPoint(Vector3::Zero));
        const Vector3 left_hand = transform.TransformPoint(
            character.globals[static_cast<size_t>(rig_.left_hand)].TransformPoint(Vector3::Zero));
        Vector3 forward = left_hand - right_hand;
        // Hands close together (pistol): point where the player looks
        if (forward.LengthSquared() < 0.01f) forward = FlatForward(player.yaw);
        forward = forward.Normalized();
        Vector3 side = Vector3::Cross(forward, Vector3::Up);
        if (side.LengthSquared() < 1.0e-4f) side = Vector3::Right;
        side = side.Normalized();
        const Vector3 up = Vector3::Cross(side, forward);
        const Vector3 grip = right_hand + forward * (it->second.length * 0.18f);

        Matrix4 basis = Matrix4::Identity();
        basis[0][0] = side.x;      basis[0][1] = side.y;      basis[0][2] = side.z;
        basis[1][0] = up.x;        basis[1][1] = up.y;        basis[1][2] = up.z;
        basis[2][0] = -forward.x;  basis[2][1] = -forward.y;  basis[2][2] = -forward.z;
        basis[3][0] = grip.x;      basis[3][1] = grip.y;      basis[3][2] = grip.z;
        Renderer::DrawModel(renderer, it->second.model, basis * it->second.local);
    }

    Vector3 CoreRenderPosition() const {
        const CoreState& core = SimWorld().GetCore();
        if (core.location == CoreLocation::Carried) {
            PlayerView carrier;
            if (FindPlayer(core.carrier, carrier)) return carrier.feet + Vector3(0, config_.player.height + 0.45f, 0);
        }
        const float32 bob = 0.08f * std::sin(static_cast<float32>(GetTime()) * 2.5f);
        return core.position + Vector3(0, 0.8f + bob, 0);
    }

    void DrawExitsAndCore(Renderer::SceneRenderer& renderer) {
        const ShiftWorld& world = SimWorld();
        if (!world.HasCore()) return;
        for (const Team team : {Team::A, Team::B}) {
            const ZoneBox& exit = world.GetExit(team);
            Renderer::Material plate;
            plate.color = TeamColor(team);
            plate.emissive = Vector3(TeamColor(team).x, TeamColor(team).y, TeamColor(team).z);
            plate.emissive_strength = 0.8f;
            plate.cast_shadows = false;
            renderer.DrawMesh(exit_mesh_, Matrix4::Translation(Vector3(exit.center.x, 0.015f, exit.center.z)), plate);
            Debug::DebugRenderer::Instance().DrawText(Vector3(exit.center.x, 2.2f, exit.center.z),
                                                      team == Team::A ? "AUSGANG BLAU" : "AUSGANG ROT", TeamColor(team));
        }
        if (world.IsCarrier(local_id_)) return;   // the carrier does not see the core in his face
        Renderer::Material glow;
        glow.color = {1.0f, 0.9f, 0.4f, 1.0f};
        glow.emissive = {1.0f, 0.85f, 0.3f};
        glow.emissive_strength = 2.5f;
        glow.cast_shadows = false;
        renderer.DrawMesh(core_mesh_, Matrix4::Translation(CoreRenderPosition()), glow);
    }

    // ------------------------------------------------------------------ HUD

    void CenterText(UI::UIContext& ui, const String& text, float32 y, const Vector4& color, float32 size) const {
        ui.DrawText(text, (ui.GetScreenWidth() - ui.GetTextWidth(text, size)) * 0.5f, y, color, size);
    }

    void DrawMenu(UI::UIContext& ui, float32 w, float32 h) const {
        ui.DrawRect({0, 0, w, h}, {0.05f, 0.06f, 0.08f, 1.0f});
        CenterText(ui, "SHIFT", h * 0.18f, {1, 1, 1, 1}, 96.0f);
        const float32 x = w * 0.5f - 260.0f;
        float32 y = h * 0.4f;
        const auto line = [&](const String& text, const Vector4& color, float32 size) {
            ui.DrawText(text, x, y, color, size);
            y += size + 14.0f;
        };
        line("1   Gegen Bot spielen", {1, 1, 1, 1}, 32.0f);
        line(fmt::format("2   Duell hosten (Port {})", config_.net.port), {1, 1, 1, 1}, 32.0f);
        line(fmt::format("3   Beitreten ({})", config_.net.server_address), {1, 1, 1, 1}, 32.0f);
        line(fmt::format("M   Karte: {}", config_.round.map), {1.0f, 0.85f, 0.4f, 1.0f}, 32.0f);
        line("Esc  Beenden", {1, 1, 1, 0.7f}, 32.0f);
        y += 20.0f;
        line("Die Adresse zum Beitreten steht in shift.json unter \"net\" -> \"server_address\".", {0.8f, 0.85f, 1.0f, 0.8f}, 22.0f);
        line("Der Host muss den UDP-Port in seiner Firewall freigeben.", {0.8f, 0.85f, 1.0f, 0.8f}, 22.0f);
    }

    void DrawConnecting(UI::UIContext& ui, float32 w, float32 h) const {
        ui.DrawRect({0, 0, w, h}, {0.05f, 0.06f, 0.08f, 1.0f});
        if (!status_error_.empty()) {
            CenterText(ui, status_error_, h * 0.4f, {1.0f, 0.4f, 0.35f, 1.0f}, 40.0f);
        } else if (client_.HasFailed()) {
            CenterText(ui, fmt::format("Keine Verbindung zu {}: {}", connect_target_, client_.GetFailReason()), h * 0.4f,
                       {1.0f, 0.4f, 0.35f, 1.0f}, 36.0f);
        } else {
            CenterText(ui, fmt::format("Verbinde mit {} ...", connect_target_), h * 0.4f, {1, 1, 1, 1}, 40.0f);
        }
        CenterText(ui, "Esc beendet", h * 0.55f, {1, 1, 1, 0.7f}, 28.0f);
    }

    void DrawTopBar(UI::UIContext& ui, float32 w) const {
        const ShiftWorld& world = SimWorld();
        const MatchState& match = world.GetMatch();
        const ShiftPhase phase = world.GetShiftPhase();

        // Shift timer with the upcoming pattern
        String text;
        Vector4 color{1, 1, 1, 0.9f};
        const ShiftPattern pattern = world.GetPlan().GetPattern(phase.next);
        if (phase.moving) {
            text = fmt::format("VERSCHIEBUNG: {}", PatternName(pattern));
            color = {1.0f, 0.3f, 0.25f, 1.0f};
        } else if (phase.warning) {
            text = fmt::format("{}  in {:.0f} s", PatternName(pattern), std::ceil(phase.time_to_next));
            color = {1.0f, 0.3f, 0.25f, 1.0f};
        } else {
            text = fmt::format("Verschiebung in {:.0f} s", std::ceil(phase.time_to_next));
        }
        ui.DrawRect({w * 0.5f - 190, 12, 380, 40}, {0, 0, 0, 0.5f});
        ui.DrawText(text, (w - ui.GetTextWidth(text, 28.0f)) * 0.5f, 18, color, 28.0f);

        if (!world.HasCore()) return;
        // Score and round clock
        const float32 left = std::max(0.0f, config_.round.round_time - world.GetRoundTime());
        const String score = fmt::format("{}  :  {}", match.score[0], match.score[1]);
        const String info = fmt::format("Runde {}   {:d}:{:02d}", match.round, static_cast<int>(left) / 60, static_cast<int>(left) % 60);
        ui.DrawRect({w * 0.5f - 190, 56, 380, 64}, {0, 0, 0, 0.5f});
        ui.DrawText(fmt::format("{}", match.score[0]), w * 0.5f - 150, 58, TeamColor(Team::A), 40.0f);
        ui.DrawText(fmt::format("{}", match.score[1]), w * 0.5f + 130, 58, TeamColor(Team::B), 40.0f);
        ui.DrawText(info, (w - ui.GetTextWidth(info, 28.0f)) * 0.5f, 64, {1, 1, 1, 0.9f}, 28.0f);
        (void)score;

        // Core status
        const CoreState& core = world.GetCore();
        String status;
        Vector4 status_color{1.0f, 0.9f, 0.5f, 1.0f};
        switch (core.location) {
            case CoreLocation::Home: status = "Kern: in der Kernkammer"; break;
            case CoreLocation::Carried: {
                PlayerView carrier;
                const bool known = FindPlayer(core.carrier, carrier);
                status = core.carrier == local_id_ ? String("Kern: DU traegst ihn")
                                                   : fmt::format("Kern: {} traegt ihn", known ? TeamName(carrier.team) : "?");
                if (known) status_color = TeamColor(carrier.team);
                break;
            }
            case CoreLocation::Dropped:
                status = fmt::format("Kern liegt  (zurueck in {:.0f} s)",
                                     std::ceil(std::max(0.0f, config_.round.core_return_time - core.idle_time)));
                break;
        }
        ui.DrawText(status, (w - ui.GetTextWidth(status, 24.0f)) * 0.5f, 126, status_color, 24.0f);
    }

    void DrawRoundMessages(UI::UIContext& ui, float32 /*w*/, float32 h) const {
        const ShiftWorld& world = SimWorld();
        if (!world.HasCore()) return;
        const MatchState& match = world.GetMatch();
        const float32 seconds = static_cast<float32>(static_cast<int64>(match.phase_end_tick) - static_cast<int64>(world.GetTick())) *
                                world.GetTickInterval();
        switch (match.phase) {
            case RoundPhase::Freeze:
                if (world.IsFrozen()) {
                    CenterText(ui, fmt::format("Runde {}", match.round), h * 0.26f, {1, 1, 1, 1}, 56.0f);
                    CenterText(ui, fmt::format("Start in {:.0f}", std::ceil(std::max(0.0f, seconds))), h * 0.34f, {1, 1, 1, 0.9f}, 40.0f);
                    CenterText(ui, fmt::format("Du bist {} - bring den Kern in deinen Ausgang", TeamName(LocalTeam())), h * 0.41f,
                               TeamColor(LocalTeam()), 28.0f);
                }
                break;
            case RoundPhase::Live:
                break;
            case RoundPhase::RoundOver:
                if (match.round_has_winner) {
                    CenterText(ui, fmt::format("{} gewinnt die Runde", TeamName(match.round_winner)), h * 0.3f,
                               TeamColor(match.round_winner), 56.0f);
                } else {
                    CenterText(ui, "Niemand hatte den Kern - Runde wird wiederholt", h * 0.3f, {1, 1, 1, 1}, 40.0f);
                }
                break;
            case RoundPhase::MatchOver:
                CenterText(ui, fmt::format("{} gewinnt das Match!", TeamName(match.match_winner)), h * 0.28f,
                           TeamColor(match.match_winner), 64.0f);
                CenterText(ui, fmt::format("{} : {}", match.score[0], match.score[1]), h * 0.37f, {1, 1, 1, 1}, 48.0f);
                CenterText(ui, fmt::format("Neues Match in {:.0f} s", std::ceil(std::max(0.0f, seconds))), h * 0.45f,
                           {1, 1, 1, 0.8f}, 28.0f);
                break;
        }
        if (announcement_time_ > 0.0f) {
            CenterText(ui, announcement_, h * 0.2f, {1.0f, 0.9f, 0.5f, std::min(1.0f, announcement_time_)}, 32.0f);
        }
    }

    void DrawPickup(UI::UIContext& ui, float32 w, float32 h, const SimPlayer& me) const {
        if (me.pickup_progress <= 0.0f) return;
        const float32 progress = std::clamp(me.pickup_progress / config_.player.core_pickup_time, 0.0f, 1.0f);
        ui.DrawRect({w * 0.5f - 100, h * 0.58f, 200, 10}, {0, 0, 0, 0.6f});
        ui.DrawRect({w * 0.5f - 100, h * 0.58f, 200 * progress, 10}, {1.0f, 0.85f, 0.3f, 1.0f});
        CenterText(ui, "Kern aufnehmen ...", h * 0.58f - 30, {1.0f, 0.9f, 0.5f, 1.0f}, 24.0f);
    }

    // Rough direction to a point, drawn on a ring around the crosshair (no exact position)
    void DrawDirectionMarker(UI::UIContext& ui, float32 w, float32 h, const Vector3& target, const Vector4& color,
                             const String& label) const {
        const SimPlayer* me = SimWorld().GetPlayer(local_id_);
        if (!me) return;
        const Vector3 d = target - me->GetFeet();
        const float32 bearing = std::atan2(d.x, -d.z) / kDegToRad;   // same convention as yaw
        // Quantised to 45 degrees: a rough direction only
        float32 relative = std::fmod(bearing - look_yaw_ + 720.0f, 360.0f);
        relative = std::round(relative / 45.0f) * 45.0f;
        const float32 a = relative * kDegToRad;
        const float32 radius = std::min(w, h) * 0.22f;
        const Math::Vector2 p{w * 0.5f + std::sin(a) * radius, h * 0.5f - std::cos(a) * radius};
        ui.DrawCircle(p, 10.0f, color);
        ui.DrawText(label, p.x - ui.GetTextWidth(label, 20.0f) * 0.5f, p.y + 12.0f, color, 20.0f);
    }

    void DrawCoreMarkers(UI::UIContext& ui, float32 w, float32 h) const {
        const ShiftWorld& world = SimWorld();
        if (!world.HasCore()) return;
        const CoreState& core = world.GetCore();
        if (world.IsCarrier(local_id_)) {
            DrawDirectionMarker(ui, w, h, world.GetExit(LocalTeam()).center, TeamColor(LocalTeam()), "AUSGANG");
            return;
        }
        if (core.location == CoreLocation::Carried) {
            PlayerView carrier;
            if (FindPlayer(core.carrier, carrier) && carrier.team != LocalTeam()) {
                DrawDirectionMarker(ui, w, h, carrier.feet, {1.0f, 0.85f, 0.3f, 1.0f}, "KERN");
            }
        }
    }

    void DrawStatus(UI::UIContext& ui) const {
        String line;
        switch (mode_) {
            case Mode::Offline: line = "Offline"; break;
            case Mode::Host:
                line = status_error_.empty()
                           ? fmt::format("Host  Port {}  {} verbunden  Ping {:.0f} ms", config_.net.port, server_.GetClientCount(),
                                         server_.GetStats().rtt_ms)
                           : status_error_;
                break;
            case Mode::Client:
                line = fmt::format("Client  Ping {:.0f} ms  Korrekturen {}", client_.GetStats().rtt_ms, client_.GetStats().corrections);
                break;
            case Mode::Menu: break;
        }
        ui.DrawRect({10, 10, 380, 56}, {0, 0, 0, 0.5f});
        ui.DrawText(fmt::format("{:.0f} FPS   Tick {}", GetFPS(), SimWorld().GetTick()), 20, 16, {1, 1, 1, 1});
        ui.DrawText(line, 20, 40, {0.8f, 0.9f, 1.0f, 1.0f});
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

    // ------------------------------------------------------------------ resources

    void CreateGraphics() {
        if (graphics_ready_) return;
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
        for (const MovingSegmentDef& segment : level_.segments) {
            const MeshHandle mesh = renderer.CreateMesh(Renderer::MeshBuilder::Box(segment.size), "Segment");
            meshes_.push_back(mesh);
            segment_meshes_.push_back(mesh);
        }

        const float32 r = config_.player.radius;
        body_mesh_ = renderer.CreateMesh(Renderer::MeshBuilder::Capsule(r, config_.player.height), "Body");
        head_mesh_ = renderer.CreateMesh(Renderer::MeshBuilder::Sphere(0.16f), "Head");
        core_mesh_ = renderer.CreateMesh(Renderer::MeshBuilder::Sphere(0.3f), "Core");
        exit_mesh_ = renderer.CreateMesh(Renderer::MeshBuilder::Box({3.0f, 0.02f, 3.0f}), "Exit");
        for (const MeshHandle mesh : {body_mesh_, head_mesh_, core_mesh_, exit_mesh_}) meshes_.push_back(mesh);

        for (const WeaponDef* def : {&config_.weapons.rifle, &config_.weapons.shotgun, &config_.weapons.sniper,
                                     &config_.weapons.pistol}) {
            if (def->model.empty()) continue;
            const String path = (std::filesystem::path(launch_.asset_dir) / def->model).string();
            Renderer::ModelData data;
            Renderer::ModelLoadOptions options;
            options.scale = def->model_length > 0.0f ? 1.0f : def->model_scale;
            ViewModel view;
            if (!Renderer::LoadModelData(path, data, options)) {
                DRX_LOG_WARNING("SHIFT: weapon model '{}' could not be loaded", path);
                continue;
            }
            Vector3 size = data.bounds.max - data.bounds.min;
            if (def->model_length > 0.0f) {
                // Any source unit: scale so the longest side is the weapon's real length
                const float32 longest = std::max({size.x, size.y, size.z});
                const float32 factor = longest > 0.0f ? def->model_length / longest : 1.0f;
                for (Renderer::ModelPart& part : data.parts) {
                    for (Renderer::Vertex& v : part.mesh.vertices) v.position = v.position * factor;
                }
                data.bounds.min = data.bounds.min * factor;
                data.bounds.max = data.bounds.max * factor;
                size = size * factor;
            }
            if (!Renderer::UploadModel(renderer, data, view.model)) continue;
            const Math::Quaternion orient = Math::Quaternion::FromAxisAngle(Vector3::Up, def->model_yaw * kDegToRad) *
                                            Math::Quaternion::FromAxisAngle(Vector3::Right, def->model_pitch * kDegToRad) *
                                            Math::Quaternion::FromAxisAngle(Vector3::Forward, def->model_roll * kDegToRad);
            view.local = orient.ToMatrix() * Matrix4::Translation(-data.bounds.GetCenter());
            view.length = std::max({size.x, size.y, size.z});
            DRX_LOG_INFO("SHIFT: weapon model {} ({:.2f} x {:.2f} x {:.2f} m)", def->model, size.x, size.y, size.z);
            view_models_[def->id] = std::move(view);
        }
        LoadCharacterRig();
        graphics_ready_ = true;
    }

    void DestroyGraphics() {
        Renderer::SceneRenderer& renderer = GetRenderer();
        for (MeshHandle mesh : meshes_) renderer.DestroyMesh(mesh);
        meshes_.clear();
        for (auto& [def, view] : view_models_) Renderer::ReleaseModel(renderer, view.model);
        view_models_.clear();
        for (auto& [id, character] : characters_) character.mesh.Release(renderer);
        characters_.clear();
        if (grid_texture_.IsValid()) renderer.DestroyTexture(grid_texture_);
        graphics_ready_ = false;
    }

    void CreateSounds() {
        Audio::AudioSystem& audio = Audio::AudioSystem::Instance();
        shot_sound_ = audio.CreateClip("shift:pistol", Audio::Synth::Gunshot(0.7f, 3));
        hit_sound_ = audio.CreateClip("shift:hit", Audio::Synth::Hit());
        headshot_sound_ = audio.CreateClip("shift:headshot", Audio::Synth::Headshot());
        click_sound_ = audio.CreateClip("shift:click", Audio::Synth::Click());
        Audio::Synth::ToneParams tone;
        tone.waveform = Audio::Synth::Waveform::Triangle;
        tone.start_frequency = 660.0f;
        tone.end_frequency = 990.0f;
        tone.duration = 0.25f;
        tone.volume = 0.6f;
        kill_sound_ = audio.CreateClip("shift:kill", Audio::Synth::Tone(tone));
        tone.start_frequency = 440.0f;
        tone.end_frequency = 1320.0f;
        tone.duration = 0.4f;
        pickup_sound_ = audio.CreateClip("shift:pickup", Audio::Synth::Tone(tone));
        tone.start_frequency = 700.0f;
        tone.end_frequency = 220.0f;
        drop_sound_ = audio.CreateClip("shift:drop", Audio::Synth::Tone(tone));
        tone.waveform = Audio::Synth::Waveform::Square;
        tone.start_frequency = 523.0f;
        tone.end_frequency = 1046.0f;
        tone.duration = 0.8f;
        tone.volume = 0.4f;
        fanfare_sound_ = audio.CreateClip("shift:fanfare", Audio::Synth::Tone(tone));
        siren_sound_ = audio.CreateClip("shift:siren", Audio::Synth::Alarm(config_.shifts.warning_time));
        Audio::Synth::ToneParams rumble;
        rumble.waveform = Audio::Synth::Waveform::Noise;
        rumble.duration = config_.shifts.move_duration;
        rumble.volume = 0.8f;
        rumble.lowpass = 180.0f;
        rumble.envelope = {0.3f, 0.0f, 1.0f, 0.5f};
        rumble_sound_ = audio.CreateClip("shift:rumble", Audio::Synth::Tone(rumble));
        Audio::Synth::ToneParams crush;
        crush.waveform = Audio::Synth::Waveform::Noise;
        crush.duration = 0.5f;
        crush.lowpass = 600.0f;
        crush.envelope = {0.002f, 0.1f, 0.3f, 0.35f};
        crush_sound_ = audio.CreateClip("shift:crush", Audio::Synth::Tone(crush));
    }

    LaunchOptions launch_;
    Mode mode_ = Mode::Menu;
    ShiftConfig config_;
    ShiftWorld world_;     // offline and host (authority)
    ShiftServer server_;
    ShiftClient client_;
    Level level_;
    PlayerId local_id_ = kInvalidPlayer;
    BotDirector bots_;
    std::map<PlayerId, Vector3> previous_feet_;
    String status_error_;
    String connect_target_;

    float32 zoom_ = 1.0f;
    CharacterRig rig_;
    std::map<PlayerId, CharacterInstance> characters_;
    float64 last_render_time_ = 0.0;
    std::map<String, ViewModel> view_models_;   // by weapon id
    Vector3 view_eye_;
    bool aiming_ = false;
    float32 view_speed_ = 0.0f;
    float32 bob_phase_ = 0.0f;
    float32 recoil_ = 0.0f;
    float32 look_yaw_ = 0.0f;
    float32 look_pitch_ = 0.0f;
    bool fire_latch_ = false;
    bool reload_latch_ = false;
    bool jump_latch_ = false;
    bool swallow_fire_ = false;
    uint16 slot_latch_ = 0;
    uint8 loadout_ = 0;   // rifle

    float32 hit_marker_ = 0.0f;
    bool hit_marker_headshot_ = false;
    float32 damage_flash_ = 0.0f;
    bool crushed_ = false;
    String announcement_;
    float32 announcement_time_ = 0.0f;

    bool graphics_ready_ = false;
    Array<MeshHandle> meshes_;
    Array<RenderBox> level_boxes_;
    Array<MeshHandle> segment_meshes_;
    TextureHandle grid_texture_;
    MeshHandle body_mesh_, head_mesh_, core_mesh_, exit_mesh_;
    SharedPtr<Audio::AudioClip> shot_sound_, hit_sound_, headshot_sound_, click_sound_, kill_sound_;
    SharedPtr<Audio::AudioClip> pickup_sound_, drop_sound_, fanfare_sound_;
    SharedPtr<Audio::AudioClip> siren_sound_, rumble_sound_, crush_sound_;
};

LaunchOptions ParseLaunch(int argc, char** argv) {
    LaunchOptions launch;
    for (int i = 1; i < argc; ++i) {
        const String arg = argv[i];
        if (arg == "--offline") launch.mode = Mode::Offline;
        else if (arg == "--host") launch.mode = Mode::Host;
        else if (arg == "--connect") {
            launch.mode = Mode::Client;
            if (i + 1 < argc && argv[i + 1][0] != '-') launch.address = argv[++i];
        }
    }
    return launch;
}

} // namespace
} // namespace Shift

int main(int argc, char** argv) {
    DRX::Runtime::ApplicationConfig config;
    config.title = "SHIFT";
    // Portable builds ship shift.json next to the executable; development builds use the source copy
    const std::filesystem::path exe_dir = std::filesystem::path(DRX::Platform::GetExecutablePath()).parent_path();
    const std::filesystem::path portable = exe_dir / "shift.json";
    std::error_code ec;
    DRX::Logger::Instance().SetOutputFile((exe_dir / "shift.log").string());   // for bug reports
    config.config_path = std::filesystem::exists(portable, ec) ? portable.string() : String(SHIFT_CONFIG_PATH);
    Shift::LaunchOptions launch = Shift::ParseLaunch(argc, argv);
    // Portable builds ship Assets/Models next to the executable; development builds use the source folder
    launch.asset_dir = std::filesystem::exists(exe_dir / "Assets" / "Models", ec) ? (exe_dir / "Assets").string()
                                                                                  : String(SHIFT_ASSET_DIR);
    Shift::ShiftApp app(std::move(config), std::move(launch));
    return app.Run(argc, argv);
}

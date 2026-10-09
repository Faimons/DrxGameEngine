#pragma once

/**
 * SHIFT - Authoritative simulation
 * Runs at the configured tick rate. The same code runs on the server (authority) and on
 * clients (prediction of the local player). Presentation reads players and drains events.
 */

#include "ShiftConfig.h"
#include "ShiftSchedule.h"
#include "ShiftTypes.h"
#include "ShiftWeapons.h"

#include "Physics/Public/PhysicsWorld.h"

#include <map>

namespace Shift {

enum class WeaponSlot : uint8 { Primary = 0, Pistol = 1 };

struct SimPlayer {
    PlayerId id = kInvalidPlayer;
    Team team = Team::A;
    bool is_dummy = false;   // target dummy: never receives input

    SharedPtr<Physics::CharacterController> controller;
    float32 yaw = 0.0f;
    float32 pitch = 0.0f;

    float32 health = 0.0f;
    bool alive = true;
    float32 respawn_timer = 0.0f;
    float32 protection_timer = 0.0f;

    WeaponState primary;
    WeaponState pistol;
    WeaponSlot active_slot = WeaponSlot::Pistol;
    PlayerInput input;

    WeaponState& ActiveWeapon() { return active_slot == WeaponSlot::Primary && primary.def ? primary : pistol; }
    const WeaponState& ActiveWeapon() const {
        return active_slot == WeaponSlot::Primary && primary.def ? primary : pistol;
    }
    bool IsProtected() const { return protection_timer > 0.0f; }
    Math::Vector3 GetFeet() const { return controller ? controller->GetPosition() : Math::Vector3(); }
    bool IsGrounded() const { return controller && controller->IsGrounded(); }
};

enum class WorldEventType : uint8 {
    Shot,         // one pellet trace: from, to, hit
    Damage,       // target took damage
    Kill,
    Respawn,
    EmptyClick,
    ReloadStart,
    ReloadDone,
    ShiftWarning,   // siren: walls of the next shift glow red
    ShiftStart,     // walls start moving
};

struct WorldEvent {
    WorldEventType type = WorldEventType::Shot;
    uint32 tick = 0;
    PlayerId source = kInvalidPlayer;
    PlayerId target = kInvalidPlayer;
    Math::Vector3 from;
    Math::Vector3 to;
    Math::Vector3 normal;
    float32 amount = 0.0f;
    bool headshot = false;
    bool hit = false;
    bool crushed = false;
};

struct SimSegment {
    MovingSegmentDef def;
    SharedPtr<Physics::RigidBody> body;
    float32 open_amount = 1.0f;
};

struct SpawnPoint {
    Math::Vector3 position;
    float32 yaw = 0.0f;
};

class ShiftWorld {
public:
    ShiftWorld() = default;
    ~ShiftWorld();
    ShiftWorld(const ShiftWorld&) = delete;
    ShiftWorld& operator=(const ShiftWorld&) = delete;

    bool Initialize(const ShiftConfig& config);
    void Shutdown();

    // Level geometry (static boxes; size = full extents)
    void AddStaticBox(const Math::Vector3& center, const Math::Vector3& size);
    // Kinematic wall segment that moves on the shift schedule; returns its index
    size_t AddSegment(const MovingSegmentDef& segment);
    const Array<SimSegment>& GetSegments() const { return segments_; }

    // Round clock (drives the shift schedule)
    void StartRound() { round_start_tick_ = tick_; UpdateSegments(); }
    float32 GetRoundTime() const { return static_cast<float32>(tick_ - round_start_tick_) * dt_; }
    ShiftPhase GetShiftPhase() const { return EvaluateShiftPhase(config_.shifts, GetRoundTime()); }

    void SetSpawn(Team team, const SpawnPoint& spawn) { spawns_[static_cast<size_t>(team)] = spawn; }
    const SpawnPoint& GetSpawn(Team team) const { return spawns_[static_cast<size_t>(team)]; }

    PlayerId AddPlayer(Team team, bool is_dummy = false, const WeaponDef* primary = nullptr);
    void RemovePlayer(PlayerId id);
    SimPlayer* GetPlayer(PlayerId id);
    const SimPlayer* GetPlayer(PlayerId id) const;
    const std::map<PlayerId, UniquePtr<SimPlayer>>& GetPlayers() const { return players_; }

    // Input for the next Step (ignored for dummies and dead players)
    void SetInput(PlayerId id, const PlayerInput& input);

    // Advances the simulation by exactly one tick
    void Step();
    uint32 GetTick() const { return tick_; }
    float32 GetTickInterval() const { return dt_; }

    // Applies damage with all rules (protection, death); returns true when the target died
    bool ApplyDamage(PlayerId target, PlayerId source, float32 amount, bool headshot);
    void Kill(PlayerId target, PlayerId source, bool crushed = false);

    Array<WorldEvent> TakeEvents();
    Physics::PhysicsWorld& GetPhysics() { return physics_; }
    const ShiftConfig& GetConfig() const { return config_; }

private:
    void StepPlayer(SimPlayer& player);
    void UpdateSegments();
    void CheckCrushed(SimPlayer& player);
    void Move(SimPlayer& player);
    void Fire(SimPlayer& player, const WeaponDef& weapon, uint32 shot_index);
    void Respawn(SimPlayer& player);
    SimPlayer* FindByUserData(void* user_data);
    void Emit(WorldEvent event);

    ShiftConfig config_;
    Physics::PhysicsWorld physics_;
    std::map<PlayerId, UniquePtr<SimPlayer>> players_;
    Array<SharedPtr<Physics::RigidBody>> statics_;
    Array<SimSegment> segments_;
    uint32 round_start_tick_ = 0;
    SpawnPoint spawns_[2];
    Array<WorldEvent> events_;
    PlayerId next_id_ = 1;
    uint32 tick_ = 0;
    float32 dt_ = 1.0f / 30.0f;
};

} // namespace Shift

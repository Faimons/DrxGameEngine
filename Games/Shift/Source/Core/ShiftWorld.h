#pragma once

/**
 * SHIFT - Simulation
 * Runs at the configured tick rate. The same code runs on the server (authority: damage, deaths,
 * core, rounds) and on clients (prediction of the local player only). Presentation reads the
 * state and drains events.
 */

#include "ShiftConfig.h"
#include "ShiftSchedule.h"
#include "ShiftTypes.h"
#include "ShiftWeapons.h"

#include "Network/Public/TickHistory.h"
#include "Physics/Public/PhysicsWorld.h"

#include <map>

namespace Shift {

enum class WeaponSlot : uint8 { Primary = 0, Pistol = 1 };

struct PositionRecord {
    Math::Vector3 feet;
    bool alive = false;
};

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
    float32 pickup_progress = 0.0f;   // seconds the interact key was held at the core
    uint8 loadout = kNoLoadout;       // wished primary weapon
    uint8 primary_index = kNoLoadout; // equipped primary weapon

    WeaponState primary;
    WeaponState pistol;
    WeaponSlot active_slot = WeaponSlot::Pistol;
    PlayerInput input;

    // Positions of the last ticks for lag compensation
    Network::TickHistory<PositionRecord, 64> history;

    WeaponState& ActiveWeapon() { return active_slot == WeaponSlot::Primary && primary.def ? primary : pistol; }
    const WeaponState& ActiveWeapon() const {
        return active_slot == WeaponSlot::Primary && primary.def ? primary : pistol;
    }
    bool IsProtected() const { return protection_timer > 0.0f; }
    Math::Vector3 GetFeet() const { return controller ? controller->GetPosition() : Math::Vector3(); }
    bool IsGrounded() const { return controller && controller->IsGrounded(); }
};

enum class WorldEventType : uint8 {
    Shot,           // one pellet trace: from, to, hit
    Damage,         // target took damage
    Kill,
    Respawn,
    EmptyClick,
    ReloadStart,
    ReloadDone,
    ShiftWarning,   // siren: walls of the next shift glow red
    ShiftStart,     // walls start moving
    CorePickup,     // source picked up the core
    CoreDrop,       // core dropped at `to`
    CoreReturn,     // core went back to the core chamber
    CoreMoved,      // core pushed out of a closing gap
    RoundStart,
    RoundEnd,       // team = winner, hit = false when the round is repeated
    MatchEnd,       // team = winner
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
    Team team = Team::A;
    bool headshot = false;
    bool hit = false;
    bool crushed = false;
    uint8 weapon = kNoLoadout;   // Shot: loadout index of the weapon, kNoLoadout = pistol
};

struct SimSegment {
    MovingSegmentDef def;
    SharedPtr<Physics::RigidBody> body;
    float32 amount = 0.0f;   // 0 = state 0, 1 = state 1
    SegmentPose pose;
};

struct SpawnPoint {
    Math::Vector3 position;
    float32 yaw = 0.0f;
};

struct ZoneBox {
    Math::Vector3 center;
    Math::Vector3 size;   // full extents

    bool Contains(const Math::Vector3& point) const;
};

enum class RoundPhase : uint8 {
    Freeze,      // round start: nothing moves
    Live,
    RoundOver,   // result screen
    MatchOver
};

enum class CoreLocation : uint8 { Home, Carried, Dropped };

struct CoreState {
    CoreLocation location = CoreLocation::Home;
    Math::Vector3 position;
    PlayerId carrier = kInvalidPlayer;
    float32 idle_time = 0.0f;          // seconds lying dropped
    bool has_last_team = false;        // somebody carried it this round
    Team last_team = Team::A;
};

struct MatchState {
    RoundPhase phase = RoundPhase::Freeze;
    int32 round = 1;
    int32 score[2] = {0, 0};
    uint32 phase_end_tick = 0;         // Freeze, RoundOver and MatchOver end at this tick
    bool round_has_winner = false;
    Team round_winner = Team::A;
    Team match_winner = Team::A;
    uint32 match_seed = 1;
};

class ShiftWorld {
public:
    ShiftWorld() = default;
    ~ShiftWorld();
    ShiftWorld(const ShiftWorld&) = delete;
    ShiftWorld& operator=(const ShiftWorld&) = delete;

    // `authority` = server or offline game; clients only predict their own movement and weapon
    bool Initialize(const ShiftConfig& config, bool authority = true);
    void Shutdown();
    bool IsAuthority() const { return authority_; }

    // Level (static boxes; size = full extents)
    void AddStaticBox(const Math::Vector3& center, const Math::Vector3& size);
    size_t AddSegment(const MovingSegmentDef& segment);
    const Array<SimSegment>& GetSegments() const { return segments_; }
    // Puts every segment into its end state (bit set = state 1) regardless of the round clock (tools)
    void SetSegmentState(GroupMask state);
    void SetShiftMode(ShiftMode mode) { shift_mode_ = mode; }
    void SetSpawn(Team team, const SpawnPoint& spawn) { spawns_[static_cast<size_t>(team)] = spawn; }
    const SpawnPoint& GetSpawn(Team team) const { return spawns_[static_cast<size_t>(team)]; }
    void SetExit(Team team, const ZoneBox& zone) { exits_[static_cast<size_t>(team)] = zone; }
    const ZoneBox& GetExit(Team team) const { return exits_[static_cast<size_t>(team)]; }
    void SetCoreHome(const Math::Vector3& position);
    const Math::Vector3& GetCoreHome() const { return core_home_; }
    bool HasCore() const { return has_core_; }

    // Match flow (authority): a match is best of `rounds_to_win * 2 - 1` rounds
    void StartMatch(uint32 seed);
    void StartRound();
    const MatchState& GetMatch() const { return match_; }
    const CoreState& GetCore() const { return core_; }
    bool IsFrozen() const;   // players cannot move or shoot

    // Round clock and wall plan
    uint32 GetRoundStartTick() const { return round_start_tick_; }
    uint32 GetRoundSeed() const { return plan_.GetSeed(); }
    float32 GetRoundTime() const { return static_cast<float32>(tick_ - round_start_tick_) * dt_; }
    ShiftPhase GetShiftPhase() const { return EvaluateShiftPhase(config_.shifts, GetRoundTime()); }
    const ShiftPlan& GetPlan() const { return plan_; }
    // Groups that move in the current or next shift (red warning)
    GroupMask GetUpcomingGroups() const;

    // Client side: adopt the server's clocks and match state
    void SetTick(uint32 tick) { tick_ = tick; }
    void SetRoundClock(uint32 round_start_tick, uint32 round_seed);
    void SetMatchState(const MatchState& match) { match_ = match; }
    void SetCoreState(const CoreState& core) { core_ = core; }

    PlayerId AddPlayer(Team team, bool is_dummy = false, const WeaponDef* primary = nullptr,
                       PlayerId id = kInvalidPlayer);
    void RemovePlayer(PlayerId id);
    SimPlayer* GetPlayer(PlayerId id);
    const SimPlayer* GetPlayer(PlayerId id) const;
    const std::map<PlayerId, UniquePtr<SimPlayer>>& GetPlayers() const { return players_; }
    bool IsCarrier(PlayerId id) const { return core_.location == CoreLocation::Carried && core_.carrier == id; }

    // Input for the next Step (ignored for dummies and dead players)
    void SetInput(PlayerId id, const PlayerInput& input);

    // Advances the simulation by exactly one tick
    void Step();
    // Client prediction: advances to `tick` simulating only this player (walls follow the plan)
    void StepSingle(PlayerId id, uint32 tick);
    uint32 GetTick() const { return tick_; }
    float32 GetTickInterval() const { return dt_; }

    // Applies damage with all rules (protection, death); returns true when the target died
    bool ApplyDamage(PlayerId target, PlayerId source, float32 amount, bool headshot);
    void Kill(PlayerId target, PlayerId source, bool crushed = false);

    void SetEventsEnabled(bool enabled) { events_enabled_ = enabled; }
    Array<WorldEvent> TakeEvents();
    Physics::PhysicsWorld& GetPhysics() { return physics_; }
    const ShiftConfig& GetConfig() const { return config_; }
    // Primary weapon for a loadout index (nullptr for kNoLoadout or invalid)
    const WeaponDef* GetLoadoutWeapon(uint8 index) const;
    void EquipLoadout(SimPlayer& player, uint8 index);

private:
    void StepPlayer(SimPlayer& player);
    void UpdateSegments();
    void CheckCrushed(SimPlayer& player);
    void Move(SimPlayer& player);
    void Fire(SimPlayer& player, const WeaponDef& weapon, uint32 shot_index);
    void Respawn(SimPlayer& player);
    void ResetPlayer(SimPlayer& player);
    void UpdatePickup(SimPlayer& player);
    void UpdateCore();
    void UpdateMatch();
    void EndRound(bool has_winner, Team winner);
    void DropCore(const Math::Vector3& position);
    void ReturnCore();
    bool FreeCorePosition(Math::Vector3& position) const;
    Math::Vector3 RewoundFeet(const SimPlayer& target, float32 view_tick) const;
    void RecordHistory();
    void Emit(WorldEvent event);

    ShiftConfig config_;
    bool authority_ = true;
    Physics::PhysicsWorld physics_;
    std::map<PlayerId, UniquePtr<SimPlayer>> players_;
    Array<SharedPtr<Physics::RigidBody>> statics_;
    Array<SimSegment> segments_;
    GroupMask segment_groups_ = 0;
    ShiftMode shift_mode_ = ShiftMode::Patterns;
    ShiftPlan plan_;
    uint32 round_start_tick_ = 0;
    SpawnPoint spawns_[2];
    ZoneBox exits_[2];
    Math::Vector3 core_home_;
    bool has_core_ = false;
    CoreState core_;
    MatchState match_;
    Array<WorldEvent> events_;
    bool events_enabled_ = true;
    PlayerId next_id_ = 1;
    uint32 tick_ = 0;
    float32 dt_ = 1.0f / 30.0f;
};

// Ray against a vertical capsule standing on `feet`; returns the hit distance or a negative value
float32 RayCapsule(const Math::Vector3& origin, const Math::Vector3& direction, const Math::Vector3& feet,
                   float32 radius, float32 height);

} // namespace Shift

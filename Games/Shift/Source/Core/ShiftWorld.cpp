/**
 * SHIFT - Simulation
 */

#include "ShiftWorld.h"

#include "Core/Public/Log.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace Shift {

namespace {

constexpr float32 kPi = 3.14159265358979323846f;
constexpr float32 kMaxPitch = 89.0f;
constexpr float32 kCoreRadius = 0.3f;
constexpr Physics::CollisionLayer kWorldMask = Physics::CollisionLayer::Static | Physics::CollisionLayer::Kinematic;

float32 Unit(uint32 h) {
    return static_cast<float32>(h & 0xFFFFFF) / static_cast<float32>(0x1000000);
}

Math::Vector3 SpreadDirection(const Math::Vector3& direction, float32 spread_degrees, uint32 seed) {
    if (spread_degrees <= 0.0f) return direction;
    const float32 angle = spread_degrees * kPi / 180.0f * std::sqrt(Unit(HashU32(seed, 1, 2)));
    const float32 around = 2.0f * kPi * Unit(HashU32(seed, 3, 4));
    const Math::Vector3 helper = std::abs(direction.y) < 0.99f ? Math::Vector3::Up : Math::Vector3::Right;
    const Math::Vector3 u = Math::Vector3::Cross(direction, helper).Normalized();
    const Math::Vector3 v = Math::Vector3::Cross(u, direction);
    return (direction * std::cos(angle) + (u * std::cos(around) + v * std::sin(around)) * std::sin(angle)).Normalized();
}

WorldEvent MakeEvent(WorldEventType type, PlayerId source, PlayerId target = kInvalidPlayer) {
    WorldEvent event;
    event.type = type;
    event.source = source;
    event.target = target;
    return event;
}

uint32 SecondsToTicks(float32 seconds, float32 dt) {
    return static_cast<uint32>(std::lround(std::max(0.0f, seconds) / dt));
}

} // namespace

float32 RayCapsule(const Math::Vector3& origin, const Math::Vector3& direction, const Math::Vector3& feet,
                   float32 radius, float32 height) {
    using Math::Vector3;
    const Vector3 a = feet + Vector3(0.0f, radius, 0.0f);
    const Vector3 b = feet + Vector3(0.0f, std::max(radius, height - radius), 0.0f);
    const Vector3 ba = b - a;
    const Vector3 oa = origin - a;
    const float32 baba = Vector3::Dot(ba, ba);
    const float32 bard = Vector3::Dot(ba, direction);
    const float32 baoa = Vector3::Dot(ba, oa);
    const float32 rdoa = Vector3::Dot(direction, oa);
    const float32 oaoa = Vector3::Dot(oa, oa);

    // Cylinder part
    const float32 qa = baba - bard * bard;
    if (qa > 1.0e-6f) {
        const float32 qb = baba * rdoa - baoa * bard;
        const float32 qc = baba * oaoa - baoa * baoa - radius * radius * baba;
        const float32 h = qb * qb - qa * qc;
        if (h < 0.0f) return -1.0f;
        const float32 t = (-qb - std::sqrt(h)) / qa;
        const float32 y = baoa + t * bard;
        if (y > 0.0f && y < baba) return t >= 0.0f ? t : -1.0f;
    }
    // Caps (spheres at both ends)
    float32 best = -1.0f;
    for (const Vector3& center : {a, b}) {
        const Vector3 oc = origin - center;
        const float32 cb = Vector3::Dot(direction, oc);
        const float32 cc = Vector3::Dot(oc, oc) - radius * radius;
        const float32 h = cb * cb - cc;
        if (h < 0.0f) continue;
        const float32 t = -cb - std::sqrt(h);
        if (t >= 0.0f && (best < 0.0f || t < best)) best = t;
    }
    return best;
}

bool ZoneBox::Contains(const Math::Vector3& point) const {
    const Math::Vector3 d = point - center;
    return std::abs(d.x) <= size.x * 0.5f && std::abs(d.y) <= size.y * 0.5f && std::abs(d.z) <= size.z * 0.5f;
}

ShiftWorld::~ShiftWorld() {
    Shutdown();
}

bool ShiftWorld::Initialize(const ShiftConfig& config, bool authority) {
    Shutdown();
    config_ = config;
    authority_ = authority;
    dt_ = config.GetTickInterval();
    tick_ = 0;
    round_start_tick_ = 0;
    next_id_ = 1;
    match_ = {};
    match_.phase = RoundPhase::Live;
    core_ = {};
    has_core_ = false;
    segment_groups_ = 0;
    plan_ = {};
    return physics_.Initialize({0.0f, -config.player.gravity, 0.0f});
}

void ShiftWorld::Shutdown() {
    for (auto& [id, player] : players_) physics_.DestroyCharacterController(player->controller);
    players_.clear();
    statics_.clear();
    segments_.clear();
    events_.clear();
    physics_.Shutdown();
}

void ShiftWorld::AddStaticBox(const Math::Vector3& center, const Math::Vector3& size) {
    Physics::RigidBodyDesc desc;
    desc.type = Physics::RigidBodyType::Static;
    desc.shape.type = Physics::CollisionShapeType::Box;
    desc.shape.half_extents = size * 0.5f;
    desc.initial_transform.position = center;
    desc.layer = Physics::CollisionLayer::Static;
    if (auto body = physics_.CreateRigidBody(desc)) statics_.push_back(body);
}

size_t ShiftWorld::AddSegment(const MovingSegmentDef& def) {
    SimSegment segment;
    segment.def = def;
    segment.amount = SegmentAmount(config_.shifts, plan_, def, GetRoundTime());
    segment.pose = def.PoseAt(segment.amount);
    segment_groups_ |= GroupBit(def.group);
    plan_ = ShiftPlan(shift_mode_, plan_.GetSeed(), segment_groups_, config_.shifts.all_open_max_per);

    Physics::RigidBodyDesc desc;
    desc.type = Physics::RigidBodyType::Kinematic;
    desc.shape.type = Physics::CollisionShapeType::Box;
    desc.shape.half_extents = def.size * 0.5f;
    desc.initial_transform.position = segment.pose.center;
    desc.initial_transform.rotation = segment.pose.Rotation();
    desc.layer = Physics::CollisionLayer::Kinematic;
    segment.body = physics_.CreateRigidBody(desc);
    if (!segment.body) DRX_LOG_ERROR("Could not create wall segment '{}'", def.name);
    segments_.push_back(std::move(segment));
    return segments_.size() - 1;
}

void ShiftWorld::SetCoreHome(const Math::Vector3& position) {
    has_core_ = true;
    core_home_ = position;
    core_ = {};
    core_.position = position;
}

PlayerId ShiftWorld::AddPlayer(Team team, bool is_dummy, const WeaponDef* primary, PlayerId id) {
    if (id != kInvalidPlayer && players_.count(id)) {
        DRX_LOG_ERROR("Player {} already exists", id);
        return kInvalidPlayer;
    }
    auto player = MakeUnique<SimPlayer>();
    player->id = id != kInvalidPlayer ? id : next_id_;
    next_id_ = std::max(next_id_, player->id + 1);
    player->team = team;
    player->is_dummy = is_dummy;

    Physics::CharacterSettings settings;
    settings.radius = config_.player.radius;
    settings.height = config_.player.height;
    settings.gravity = config_.player.gravity;
    settings.user_data = player.get();
    // Pushed out until less than the player width remains, then crushed
    settings.crush_tolerance = 0.005f;
    player->controller = physics_.CreateCharacterController(settings, GetSpawn(team).position);
    if (!player->controller) {
        DRX_LOG_ERROR("Could not create a character for player {}", player->id);
        return kInvalidPlayer;
    }

    player->primary.Equip(primary);
    for (uint8 i = 0; i < kLoadoutCount; ++i) {
        if (primary && GetLoadoutWeapon(i) == primary) player->primary_index = i;
    }
    player->pistol.Equip(&config_.weapons.pistol);
    player->active_slot = primary ? WeaponSlot::Primary : WeaponSlot::Pistol;
    ResetPlayer(*player);

    const PlayerId new_id = player->id;
    players_.emplace(new_id, std::move(player));
    return new_id;
}

void ShiftWorld::RemovePlayer(PlayerId id) {
    const auto it = players_.find(id);
    if (it == players_.end()) return;
    if (IsCarrier(id)) DropCore(it->second->GetFeet());
    physics_.DestroyCharacterController(it->second->controller);
    players_.erase(it);
}

SimPlayer* ShiftWorld::GetPlayer(PlayerId id) {
    const auto it = players_.find(id);
    return it != players_.end() ? it->second.get() : nullptr;
}

const SimPlayer* ShiftWorld::GetPlayer(PlayerId id) const {
    const auto it = players_.find(id);
    return it != players_.end() ? it->second.get() : nullptr;
}

void ShiftWorld::SetInput(PlayerId id, const PlayerInput& input) {
    if (SimPlayer* player = GetPlayer(id); player && !player->is_dummy) player->input = input;
}

void ShiftWorld::Emit(WorldEvent event) {
    if (!events_enabled_) return;
    event.tick = tick_;
    events_.push_back(event);
}

Array<WorldEvent> ShiftWorld::TakeEvents() {
    Array<WorldEvent> events;
    events.swap(events_);
    return events;
}

// ============================================================================
// Match flow
// ============================================================================

bool ShiftWorld::IsFrozen() const {
    switch (match_.phase) {
        case RoundPhase::Freeze: return tick_ < match_.phase_end_tick;
        case RoundPhase::Live: return false;
        case RoundPhase::RoundOver:
        case RoundPhase::MatchOver: return true;
    }
    return false;
}

void ShiftWorld::StartMatch(uint32 seed) {
    match_ = {};
    match_.match_seed = seed;
    match_.round = 1;
    StartRound();
}

void ShiftWorld::StartRound() {
    round_start_tick_ = tick_;
    SetRoundClock(tick_, HashU32(match_.match_seed, static_cast<uint32>(match_.round), tick_));
    match_.round_has_winner = false;
    if (has_core_) {
        match_.phase = RoundPhase::Freeze;
        match_.phase_end_tick = tick_ + SecondsToTicks(config_.round.freeze_time, dt_);
        core_ = {};
        core_.position = core_home_;
    } else {
        match_.phase = RoundPhase::Live;
    }
    for (auto& [id, player] : players_) ResetPlayer(*player);
    WorldEvent event = MakeEvent(WorldEventType::RoundStart, kInvalidPlayer);
    event.amount = static_cast<float32>(match_.round);
    Emit(event);
}

void ShiftWorld::SetRoundClock(uint32 round_start_tick, uint32 round_seed) {
    round_start_tick_ = round_start_tick;
    plan_ = ShiftPlan(shift_mode_, round_seed, segment_groups_, config_.shifts.all_open_max_per);
    UpdateSegments();
}

GroupMask ShiftWorld::GetUpcomingGroups() const {
    const ShiftPhase phase = GetShiftPhase();
    if (!phase.moving && !phase.warning) return 0;
    return plan_.GetChangedGroups(phase.next);
}

void ShiftWorld::UpdateMatch() {
    switch (match_.phase) {
        case RoundPhase::Freeze:
            if (tick_ >= match_.phase_end_tick) match_.phase = RoundPhase::Live;
            break;
        case RoundPhase::Live:
            if (has_core_ && GetRoundTime() >= config_.round.round_time) {
                // Time limit: the team holding the core, else the team that held it last
                if (core_.location == CoreLocation::Carried) {
                    const SimPlayer* carrier = GetPlayer(core_.carrier);
                    EndRound(carrier != nullptr, carrier ? carrier->team : Team::A);
                } else {
                    EndRound(core_.has_last_team, core_.last_team);
                }
            }
            break;
        case RoundPhase::RoundOver:
            if (tick_ >= match_.phase_end_tick) {
                if (match_.round_has_winner) ++match_.round;
                StartRound();
            }
            break;
        case RoundPhase::MatchOver:
            if (tick_ >= match_.phase_end_tick) StartMatch(HashU32(match_.match_seed, tick_, 0x4D41u));
            break;
    }
}

void ShiftWorld::EndRound(bool has_winner, Team winner) {
    match_.round_has_winner = has_winner;
    match_.round_winner = winner;
    if (has_winner) ++match_.score[static_cast<size_t>(winner)];

    WorldEvent event = MakeEvent(WorldEventType::RoundEnd, kInvalidPlayer);
    event.team = winner;
    event.hit = has_winner;
    Emit(event);

    if (has_winner && match_.score[static_cast<size_t>(winner)] >= config_.round.rounds_to_win) {
        match_.phase = RoundPhase::MatchOver;
        match_.match_winner = winner;
        match_.phase_end_tick = tick_ + SecondsToTicks(config_.round.match_end_delay, dt_);
        WorldEvent end = MakeEvent(WorldEventType::MatchEnd, kInvalidPlayer);
        end.team = winner;
        Emit(end);
    } else {
        match_.phase = RoundPhase::RoundOver;
        match_.phase_end_tick = tick_ + SecondsToTicks(config_.round.round_end_delay, dt_);
    }
}

// ============================================================================
// Core
// ============================================================================

void ShiftWorld::UpdatePickup(SimPlayer& player) {
    const bool available = has_core_ && core_.location != CoreLocation::Carried && !player.is_dummy && !IsFrozen();
    const Math::Vector3 d = core_.position - player.GetFeet();
    const float32 range = config_.round.core_pickup_range;
    const bool in_reach = d.x * d.x + d.z * d.z <= range * range && std::abs(d.y) < 2.0f;
    if (!available || !in_reach || !player.input.Has(kButtonInteract)) {
        player.pickup_progress = 0.0f;
        return;
    }
    player.pickup_progress += dt_;
    if (player.pickup_progress + 1.0e-4f < config_.player.core_pickup_time) return;

    player.pickup_progress = 0.0f;
    core_.location = CoreLocation::Carried;
    core_.carrier = player.id;
    core_.idle_time = 0.0f;
    core_.has_last_team = true;
    core_.last_team = player.team;
    core_.position = player.GetFeet();
    for (auto& [id, other] : players_) other->pickup_progress = 0.0f;
    Emit(MakeEvent(WorldEventType::CorePickup, player.id));
}

void ShiftWorld::DropCore(const Math::Vector3& position) {
    core_.location = CoreLocation::Dropped;
    core_.carrier = kInvalidPlayer;
    core_.idle_time = 0.0f;
    // Falls to the floor below
    Physics::QueryFilter filter;
    filter.mask = kWorldMask;
    const Physics::RaycastResult ground =
        physics_.Raycast(position + Math::Vector3(0.0f, 0.5f, 0.0f), position - Math::Vector3(0.0f, 20.0f, 0.0f), filter);
    core_.position = ground.hit ? ground.point : position;
    if (!FreeCorePosition(core_.position)) {
        ReturnCore();
        return;
    }
    WorldEvent event = MakeEvent(WorldEventType::CoreDrop, kInvalidPlayer);
    event.to = core_.position;
    Emit(event);
}

void ShiftWorld::ReturnCore() {
    core_.location = CoreLocation::Home;
    core_.carrier = kInvalidPlayer;
    core_.idle_time = 0.0f;
    core_.position = core_home_;
    WorldEvent event = MakeEvent(WorldEventType::CoreReturn, kInvalidPlayer);
    event.to = core_home_;
    Emit(event);
}

bool ShiftWorld::FreeCorePosition(Math::Vector3& position) const {
    // A wall that moves over the core pushes it to the nearest free side
    for (const SimSegment& segment : segments_) {
        const Math::Vector3 half = segment.def.size * 0.5f;
        const Math::Quaternion rotation = segment.pose.Rotation();
        const Math::Vector3 local = rotation.Conjugate().Rotate(position + Math::Vector3(0, kCoreRadius, 0) - segment.pose.center);
        const float32 px = half.x + kCoreRadius - std::abs(local.x);
        const float32 pz = half.z + kCoreRadius - std::abs(local.z);
        if (px <= 0.0f || pz <= 0.0f || std::abs(local.y) > half.y + kCoreRadius) continue;

        // Candidate exits on both sides of both axes, nearest first
        struct Candidate {
            float32 distance;
            Math::Vector3 local_offset;
        };
        Candidate candidates[4] = {
            {px, {(local.x >= 0.0f ? 1.0f : -1.0f) * (px + 0.1f), 0, 0}},
            {pz, {0, 0, (local.z >= 0.0f ? 1.0f : -1.0f) * (pz + 0.1f)}},
            {2.0f * half.x + 2.0f * kCoreRadius - px, {(local.x >= 0.0f ? -1.0f : 1.0f) * (2.0f * (half.x + kCoreRadius) - px + 0.1f), 0, 0}},
            {2.0f * half.z + 2.0f * kCoreRadius - pz, {0, 0, (local.z >= 0.0f ? -1.0f : 1.0f) * (2.0f * (half.z + kCoreRadius) - pz + 0.1f)}},
        };
        std::sort(std::begin(candidates), std::end(candidates),
                  [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
        for (const Candidate& candidate : candidates) {
            const Math::Vector3 moved = position + rotation.Rotate(candidate.local_offset);
            Physics::QueryFilter filter;
            filter.mask = kWorldMask;
            if (physics_.OverlapSphere(moved + Math::Vector3(0, kCoreRadius + 0.05f, 0), kCoreRadius, filter).empty()) {
                position = moved;
                return true;
            }
        }
        return false;
    }
    return true;
}

void ShiftWorld::UpdateCore() {
    if (!has_core_) return;
    switch (core_.location) {
        case CoreLocation::Carried: {
            const SimPlayer* carrier = GetPlayer(core_.carrier);
            if (!carrier || !carrier->alive) {
                DropCore(carrier ? carrier->GetFeet() : core_.position);
                break;
            }
            core_.position = carrier->GetFeet();
            if (match_.phase == RoundPhase::Live && GetExit(carrier->team).Contains(core_.position + Math::Vector3(0, 0.5f, 0))) {
                EndRound(true, carrier->team);
            }
            break;
        }
        case CoreLocation::Dropped:
            core_.idle_time += dt_;
            if (core_.idle_time + 1.0e-4f >= config_.round.core_return_time) {
                ReturnCore();
                break;
            }
            [[fallthrough]];
        case CoreLocation::Home: {
            Math::Vector3 position = core_.position;
            if (!FreeCorePosition(position)) {
                ReturnCore();
            } else if ((position - core_.position).LengthSquared() > 1.0e-6f) {
                core_.position = position;
                if (core_.location == CoreLocation::Home) core_.location = CoreLocation::Dropped;
                WorldEvent event = MakeEvent(WorldEventType::CoreMoved, kInvalidPlayer);
                event.to = position;
                Emit(event);
            }
            break;
        }
    }
}

// ============================================================================
// Simulation step
// ============================================================================

void ShiftWorld::UpdateSegments() {
    const float32 round_time = GetRoundTime();
    for (SimSegment& segment : segments_) {
        segment.amount = SegmentAmount(config_.shifts, plan_, segment.def, round_time);
        segment.pose = segment.def.PoseAt(segment.amount);
        if (!segment.body) continue;
        Math::Transform transform;
        transform.position = segment.pose.center;
        transform.rotation = segment.pose.Rotation();
        segment.body->SetTransform(transform);
    }
}

void ShiftWorld::SetSegmentState(GroupMask state) {
    for (SimSegment& segment : segments_) {
        segment.amount = (state & GroupBit(segment.def.group)) ? 1.0f : 0.0f;
        segment.pose = segment.def.PoseAt(segment.amount);
        if (!segment.body) continue;
        Math::Transform transform;
        transform.position = segment.pose.center;
        transform.rotation = segment.pose.Rotation();
        segment.body->SetTransform(transform);
    }
}

void ShiftWorld::RecordHistory() {
    for (auto& [id, player] : players_) player->history.Store(tick_, {player->GetFeet(), player->alive});
}

void ShiftWorld::Step() {
    const ShiftPhase before = GetShiftPhase();
    ++tick_;
    const ShiftPhase after = GetShiftPhase();
    const bool round_running = match_.phase == RoundPhase::Freeze || match_.phase == RoundPhase::Live;
    if (round_running && after.warning && !before.warning) Emit(MakeEvent(WorldEventType::ShiftWarning, kInvalidPlayer));
    if (round_running && after.moving && !before.moving) Emit(MakeEvent(WorldEventType::ShiftStart, kInvalidPlayer));

    // Walls move first; characters are then pushed out (or crushed) by their own move
    UpdateSegments();
    // Fixed order (by id) keeps the simulation deterministic
    for (auto& [id, player] : players_) StepPlayer(*player);
    physics_.Step(dt_);
    if (authority_) {
        UpdateCore();
        UpdateMatch();
    }
    RecordHistory();
}

void ShiftWorld::StepSingle(PlayerId id, uint32 tick) {
    tick_ = tick;
    UpdateSegments();
    if (SimPlayer* player = GetPlayer(id)) StepPlayer(*player);
    physics_.Step(dt_);
}

void ShiftWorld::CheckCrushed(SimPlayer& player) {
    if (authority_ && player.alive && player.controller->IsCrushed()) Kill(player.id, kInvalidPlayer, true);
}

void ShiftWorld::StepPlayer(SimPlayer& player) {
    if (!player.alive) {
        player.respawn_timer -= dt_;
        if (player.respawn_timer <= 1.0e-4f) Respawn(player);
        return;
    }
    player.protection_timer = std::max(0.0f, player.protection_timer - dt_);

    if (player.is_dummy) {
        player.controller->Move(Math::Vector3::Zero, 0.0f, dt_);
        CheckCrushed(player);
        return;
    }

    const PlayerInput& input = player.input;
    player.yaw = std::fmod(input.yaw, 360.0f);
    player.pitch = std::clamp(input.pitch, -kMaxPitch, kMaxPitch);
    if (input.loadout < kLoadoutCount) player.loadout = input.loadout;
    // During the round start freeze a new choice is equipped at once, otherwise at the next spawn
    if (IsFrozen() && player.loadout != kNoLoadout && player.loadout != player.primary_index) {
        EquipLoadout(player, player.loadout);
        player.active_slot = WeaponSlot::Primary;
    }
    if (input.Has(kButtonSlotPrimary) && player.primary.def) player.active_slot = WeaponSlot::Primary;
    if (input.Has(kButtonSlotPistol)) player.active_slot = WeaponSlot::Pistol;

    Move(player);
    CheckCrushed(player);
    if (!player.alive) return;
    if (authority_) UpdatePickup(player);
    if (IsFrozen()) return;

    // The carrier may only use the pistol
    WeaponState& weapon = IsCarrier(player.id) ? player.pistol : player.ActiveWeapon();
    const WeaponTickResult result =
        UpdateWeapon(weapon, input.Has(kButtonFire), input.Has(kButtonReload), dt_);
    if (result.empty_click) Emit(MakeEvent(WorldEventType::EmptyClick, player.id));
    if (result.reload_started) Emit(MakeEvent(WorldEventType::ReloadStart, player.id));
    if (result.reload_finished) Emit(MakeEvent(WorldEventType::ReloadDone, player.id));
    if (result.shots > 0) {
        player.protection_timer = 0.0f;   // shooting ends spawn protection
        for (uint32 shot = 0; shot < result.shots; ++shot) Fire(player, *weapon.def, shot);
    }
}

void ShiftWorld::Move(SimPlayer& player) {
    const PlayerInput& input = player.input;
    const PlayerSettings& s = config_.player;
    const bool frozen = IsFrozen();

    Math::Vector3 direction = FlatForward(player.yaw) * input.move_y + FlatRight(player.yaw) * input.move_x;
    if (direction.LengthSquared() > 1.0f) direction = direction.Normalized();
    const float32 speed = IsCarrier(player.id) ? s.carrier_speed : s.walk_speed;
    const Math::Vector3 wish = frozen ? Math::Vector3::Zero : direction * speed;

    Math::Vector3 velocity = wish;
    const bool grounded = player.controller->IsGrounded();
    if (!grounded) {
        const Math::Vector3 v = player.controller->GetVelocity();
        const Math::Vector3 current(v.x, 0.0f, v.z);
        velocity = current + (wish - current) * std::clamp(s.air_control * 10.0f * dt_, 0.0f, 1.0f);
    }
    const bool jump = !frozen && input.Has(kButtonJump) && grounded;
    player.controller->Move(velocity, jump ? std::sqrt(2.0f * s.gravity * s.jump_height) : 0.0f, dt_);
}

Math::Vector3 ShiftWorld::RewoundFeet(const SimPlayer& target, float32 view_tick) const {
    if (!authority_ || view_tick <= 0.0f) return target.GetFeet();
    const float32 max_ticks = config_.net.lag_compensation_max_ms * 0.001f / dt_;
    const float32 now = static_cast<float32>(tick_);
    const float32 rewind = std::clamp(view_tick, now - max_ticks, now);
    const PositionRecord* from = nullptr;
    const PositionRecord* to = nullptr;
    float32 alpha = 0.0f;
    if (!target.history.FindBracket(rewind, from, to, alpha) || !from->alive || !to->alive) return target.GetFeet();
    return from->feet + (to->feet - from->feet) * alpha;
}

void ShiftWorld::Fire(SimPlayer& player, const WeaponDef& weapon, uint32 shot_index) {
    const Math::Vector3 eye = player.GetFeet() + Math::Vector3(0.0f, config_.player.eye_height, 0.0f);
    const Math::Vector3 aim = LookDirection(player.yaw, player.pitch);

    const Math::Vector3 v = player.controller->GetVelocity();
    const bool moving = v.x * v.x + v.z * v.z > 0.25f || !player.IsGrounded();
    const float32 spread = weapon.spread + (moving ? weapon.moving_spread : 0.0f);

    // Geometry by ray; players by capsule at the position the shooter saw (lag compensation)
    Physics::QueryFilter filter;
    filter.mask = kWorldMask;

    for (uint32 pellet = 0; pellet < weapon.pellets; ++pellet) {
        const uint32 seed = HashU32(tick_, player.id, shot_index * 64 + pellet);
        const Math::Vector3 direction = SpreadDirection(aim, spread, seed);
        const Physics::RaycastResult ray = physics_.Raycast(eye, eye + direction * weapon.range, filter);
        float32 distance = ray.hit ? ray.distance : weapon.range;

        SimPlayer* target = nullptr;
        Math::Vector3 target_feet;
        for (auto& [id, other] : players_) {
            if (id == player.id || !other->alive) continue;
            const Math::Vector3 feet = RewoundFeet(*other, player.input.view_tick);
            const float32 t = RayCapsule(eye, direction, feet, config_.player.radius, config_.player.height);
            if (t >= 0.0f && t < distance) {
                distance = t;
                target = other.get();
                target_feet = feet;
            }
        }

        WorldEvent shot = MakeEvent(WorldEventType::Shot, player.id);
        shot.weapon = &weapon == player.pistol.def ? kNoLoadout : player.primary_index;
        shot.from = eye;
        shot.to = eye + direction * distance;
        shot.normal = target ? -direction : ray.normal;
        shot.hit = target != nullptr || ray.hit;
        if (target) {
            shot.target = target->id;
            shot.headshot = shot.to.y - target_feet.y >= config_.player.head_height;
            shot.amount = weapon.damage * DamageFalloff(weapon, distance) *
                          (shot.headshot ? weapon.headshot_multiplier : 1.0f);
        }
        Emit(shot);
        if (target && authority_) ApplyDamage(target->id, player.id, shot.amount, shot.headshot);
    }
}

bool ShiftWorld::ApplyDamage(PlayerId target_id, PlayerId source, float32 amount, bool headshot) {
    SimPlayer* target = GetPlayer(target_id);
    if (!authority_ || !target || !target->alive || amount <= 0.0f || target->IsProtected()) return false;

    target->health = std::max(0.0f, target->health - amount);
    WorldEvent damage = MakeEvent(WorldEventType::Damage, source, target_id);
    damage.amount = amount;
    damage.headshot = headshot;
    damage.to = target->GetFeet();
    Emit(damage);

    if (target->health > 0.0f) return false;
    Kill(target_id, source);
    return true;
}

void ShiftWorld::Kill(PlayerId target_id, PlayerId source, bool crushed) {
    SimPlayer* target = GetPlayer(target_id);
    if (!authority_ || !target || !target->alive) return;
    target->health = 0.0f;
    target->alive = false;
    target->respawn_timer = config_.player.respawn_time;
    target->pickup_progress = 0.0f;
    target->controller->SetEnabled(false);

    WorldEvent kill = MakeEvent(WorldEventType::Kill, source, target_id);
    kill.to = target->GetFeet();
    kill.crushed = crushed;
    Emit(kill);

    if (IsCarrier(target_id)) DropCore(target->GetFeet());
}

void ShiftWorld::ResetPlayer(SimPlayer& player) {
    Respawn(player);
    player.protection_timer = 0.0f;   // round start is not protected
}

const WeaponDef* ShiftWorld::GetLoadoutWeapon(uint8 index) const {
    switch (index) {
        case 0: return &config_.weapons.rifle;
        case 1: return &config_.weapons.shotgun;
        case 2: return &config_.weapons.sniper;
        default: return nullptr;
    }
}

void ShiftWorld::EquipLoadout(SimPlayer& player, uint8 index) {
    const WeaponDef* weapon = GetLoadoutWeapon(index);
    if (!weapon) return;
    player.primary.Equip(weapon);
    player.primary_index = index;
}

void ShiftWorld::Respawn(SimPlayer& player) {
    const SpawnPoint& spawn = GetSpawn(player.team);
    player.alive = true;
    player.health = config_.player.max_health;
    player.respawn_timer = 0.0f;
    player.protection_timer = config_.player.spawn_protection;
    player.pickup_progress = 0.0f;
    player.yaw = spawn.yaw;
    player.pitch = 0.0f;
    player.input = {};
    player.input.yaw = spawn.yaw;
    if (player.loadout != kNoLoadout) EquipLoadout(player, player.loadout);
    else if (player.primary.def) player.primary.Equip(player.primary.def);
    player.pistol.Equip(player.pistol.def);
    player.active_slot = player.primary.def ? WeaponSlot::Primary : WeaponSlot::Pistol;

    Physics::CharacterState state;
    state.position = spawn.position;
    player.controller->SetEnabled(true);
    player.controller->SetState(state);

    WorldEvent event = MakeEvent(WorldEventType::Respawn, kInvalidPlayer, player.id);
    event.to = spawn.position;
    Emit(event);
}

} // namespace Shift

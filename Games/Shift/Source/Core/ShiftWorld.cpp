/**
 * SHIFT - Authoritative simulation
 */

#include "ShiftWorld.h"

#include "Core/Public/Log.h"

#include <algorithm>
#include <cmath>

namespace Shift {

namespace {

constexpr float32 kPi = 3.14159265358979323846f;
constexpr float32 kMaxPitch = 89.0f;

// Deterministic per-shot random numbers (same tick, player and pellet -> same spread)
uint32 Hash(uint32 a, uint32 b, uint32 c) {
    uint32 h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

float32 Unit(uint32 h) {
    return static_cast<float32>(h & 0xFFFFFF) / static_cast<float32>(0x1000000);
}

Math::Vector3 SpreadDirection(const Math::Vector3& direction, float32 spread_degrees, uint32 seed) {
    if (spread_degrees <= 0.0f) return direction;
    const float32 angle = spread_degrees * kPi / 180.0f * std::sqrt(Unit(Hash(seed, 1, 2)));
    const float32 around = 2.0f * kPi * Unit(Hash(seed, 3, 4));
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

} // namespace

ShiftWorld::~ShiftWorld() {
    Shutdown();
}

bool ShiftWorld::Initialize(const ShiftConfig& config) {
    Shutdown();
    config_ = config;
    dt_ = config.GetTickInterval();
    tick_ = 0;
    next_id_ = 1;
    return physics_.Initialize({0.0f, -config.player.gravity, 0.0f});
}

void ShiftWorld::Shutdown() {
    for (auto& [id, player] : players_) physics_.DestroyCharacterController(player->controller);
    players_.clear();
    statics_.clear();
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

PlayerId ShiftWorld::AddPlayer(Team team, bool is_dummy, const WeaponDef* primary) {
    auto player = MakeUnique<SimPlayer>();
    player->id = next_id_++;
    player->team = team;
    player->is_dummy = is_dummy;

    Physics::CharacterSettings settings;
    settings.radius = config_.player.radius;
    settings.height = config_.player.height;
    settings.gravity = config_.player.gravity;
    settings.user_data = player.get();
    // Pushed out until less than the player width remains, then crushed
    settings.crush_tolerance = 0.01f;
    player->controller = physics_.CreateCharacterController(settings, GetSpawn(team).position);
    if (!player->controller) {
        DRX_LOG_ERROR("Could not create a character for player {}", player->id);
        return kInvalidPlayer;
    }

    player->primary.Equip(primary);
    player->pistol.Equip(&config_.weapons.pistol);
    player->active_slot = primary ? WeaponSlot::Primary : WeaponSlot::Pistol;
    Respawn(*player);
    player->protection_timer = 0.0f;   // first spawn of a match is not protected

    const PlayerId id = player->id;
    players_.emplace(id, std::move(player));
    return id;
}

void ShiftWorld::RemovePlayer(PlayerId id) {
    const auto it = players_.find(id);
    if (it == players_.end()) return;
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

SimPlayer* ShiftWorld::FindByUserData(void* user_data) {
    for (auto& [id, player] : players_) {
        if (player.get() == user_data) return player.get();
    }
    return nullptr;
}

void ShiftWorld::SetInput(PlayerId id, const PlayerInput& input) {
    if (SimPlayer* player = GetPlayer(id); player && !player->is_dummy) player->input = input;
}

void ShiftWorld::Emit(WorldEvent event) {
    event.tick = tick_;
    events_.push_back(event);
}

Array<WorldEvent> ShiftWorld::TakeEvents() {
    Array<WorldEvent> events;
    events.swap(events_);
    return events;
}

void ShiftWorld::Step() {
    ++tick_;
    // Fixed order (by id) keeps the simulation deterministic
    for (auto& [id, player] : players_) StepPlayer(*player);
    physics_.Step(dt_);
}

void ShiftWorld::StepPlayer(SimPlayer& player) {
    if (!player.alive) {
        player.respawn_timer -= dt_;
        if (player.respawn_timer <= 0.0f) Respawn(player);
        return;
    }
    player.protection_timer = std::max(0.0f, player.protection_timer - dt_);

    if (player.is_dummy) {
        player.controller->Move(Math::Vector3::Zero, 0.0f, dt_);
        return;
    }

    const PlayerInput& input = player.input;
    player.yaw = std::fmod(input.yaw, 360.0f);
    player.pitch = std::clamp(input.pitch, -kMaxPitch, kMaxPitch);
    Move(player);

    WeaponState& weapon = player.ActiveWeapon();
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

    Math::Vector3 direction = FlatForward(player.yaw) * input.move_y + FlatRight(player.yaw) * input.move_x;
    if (direction.LengthSquared() > 1.0f) direction = direction.Normalized();
    const Math::Vector3 wish = direction * s.walk_speed;

    Math::Vector3 velocity = wish;
    const bool grounded = player.controller->IsGrounded();
    if (!grounded) {
        const Math::Vector3 v = player.controller->GetVelocity();
        const Math::Vector3 current(v.x, 0.0f, v.z);
        velocity = current + (wish - current) * std::clamp(s.air_control * 10.0f * dt_, 0.0f, 1.0f);
    }
    const float32 jump = input.Has(kButtonJump) && grounded ? std::sqrt(2.0f * s.gravity * s.jump_height) : 0.0f;
    player.controller->Move(velocity, jump, dt_);
}

void ShiftWorld::Fire(SimPlayer& player, const WeaponDef& weapon, uint32 shot_index) {
    const Math::Vector3 eye = player.GetFeet() + Math::Vector3(0.0f, config_.player.eye_height, 0.0f);
    const Math::Vector3 aim = LookDirection(player.yaw, player.pitch);

    const Math::Vector3 v = player.controller->GetVelocity();
    const bool moving = v.x * v.x + v.z * v.z > 0.25f || !player.IsGrounded();
    const float32 spread = weapon.spread + (moving ? weapon.moving_spread : 0.0f);

    Physics::QueryFilter filter;
    filter.ignore_character = player.controller.get();

    for (uint32 pellet = 0; pellet < weapon.pellets; ++pellet) {
        const uint32 seed = Hash(tick_, player.id, shot_index * 64 + pellet);
        const Math::Vector3 direction = SpreadDirection(aim, spread, seed);
        const Math::Vector3 end = eye + direction * weapon.range;
        const Physics::RaycastResult ray = physics_.Raycast(eye, end, filter);

        WorldEvent shot = MakeEvent(WorldEventType::Shot, player.id);
        shot.from = eye;
        shot.to = ray.hit ? ray.point : end;
        shot.normal = ray.normal;
        shot.hit = ray.hit;

        SimPlayer* target = ray.hit ? FindByUserData(ray.body_user_data) : nullptr;
        if (target && target->alive && target->id != player.id) {
            shot.target = target->id;
            shot.headshot = ray.point.y - target->GetFeet().y >= config_.player.head_height;
            shot.amount = weapon.damage * DamageFalloff(weapon, ray.distance) *
                          (shot.headshot ? weapon.headshot_multiplier : 1.0f);
        }
        Emit(shot);
        if (shot.target != kInvalidPlayer) ApplyDamage(shot.target, player.id, shot.amount, shot.headshot);
    }
}

bool ShiftWorld::ApplyDamage(PlayerId target_id, PlayerId source, float32 amount, bool headshot) {
    SimPlayer* target = GetPlayer(target_id);
    if (!target || !target->alive || amount <= 0.0f || target->IsProtected()) return false;

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

void ShiftWorld::Kill(PlayerId target_id, PlayerId source) {
    SimPlayer* target = GetPlayer(target_id);
    if (!target || !target->alive) return;
    target->health = 0.0f;
    target->alive = false;
    target->respawn_timer = config_.player.respawn_time;
    target->controller->SetEnabled(false);

    WorldEvent kill = MakeEvent(WorldEventType::Kill, source, target_id);
    kill.to = target->GetFeet();
    Emit(kill);
}

void ShiftWorld::Respawn(SimPlayer& player) {
    const SpawnPoint& spawn = GetSpawn(player.team);
    player.alive = true;
    player.health = config_.player.max_health;
    player.respawn_timer = 0.0f;
    player.protection_timer = config_.player.spawn_protection;
    player.yaw = spawn.yaw;
    player.pitch = 0.0f;
    player.input = {};
    player.input.yaw = spawn.yaw;
    if (player.primary.def) player.primary.Equip(player.primary.def);
    player.pistol.Equip(player.pistol.def);

    Physics::CharacterState state;
    state.position = spawn.position;
    player.controller->SetEnabled(true);
    player.controller->SetState(state);

    WorldEvent event = MakeEvent(WorldEventType::Respawn, kInvalidPlayer, player.id);
    event.to = spawn.position;
    Emit(event);
}

} // namespace Shift

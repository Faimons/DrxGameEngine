/**
 * DRX Engine - FPS Game Framework Implementation
 */

#include "Game/Public/FPSFramework.h"
#include "Core/Public/Log.h"

#include <algorithm>
#include <cmath>

namespace DRX::Game {

namespace {

constexpr float32 kPi = 3.14159265358979323846f;
constexpr float32 kDegToRad = kPi / 180.0f;

Math::Vector3 Horizontal(const Math::Vector3& v) {
    return Math::Vector3(v.x, 0.0f, v.z);
}

} // namespace

// ============================================================================
// HealthComponent
// ============================================================================

HealthComponent::HealthComponent(float32 max_health) : health_(max_health), max_health_(max_health) {}

float32 HealthComponent::TakeDamage(const DamageInfo& damage) {
    if (invulnerable || IsDead() || damage.amount <= 0.0f) return 0.0f;

    const float32 applied = std::min(health_, damage.amount * damage_multiplier);
    health_ -= applied;
    if (on_damage_) on_damage_(damage);
    if (IsDead() && on_death_) on_death_(damage);
    return applied;
}

void HealthComponent::Heal(float32 amount) {
    if (IsDead() || amount <= 0.0f) return;
    const float32 old_health = health_;
    health_ = std::min(health_ + amount, max_health_);
    if (on_heal_ && health_ > old_health) on_heal_(health_ - old_health);
}

void HealthComponent::SetHealth(float32 health) {
    health_ = std::clamp(health, 0.0f, max_health_);
}

void HealthComponent::SetMaxHealth(float32 max_health) {
    max_health_ = std::max(0.0f, max_health);
    health_ = std::min(health_, max_health_);
}

// ============================================================================
// Weapon
// ============================================================================

Weapon::Weapon(const WeaponConfig& config)
    : config_(config),
      ammo_in_magazine_(config.magazine_size),
      reserve_ammo_(config.max_ammo),
      rng_(config.seed) {}

void Weapon::Update(float32 delta_time) {
    cooldown_ = std::max(0.0f, cooldown_ - delta_time);

    switch (state_) {
        case WeaponState::Firing:
            if (!trigger_held_) {
                state_ = WeaponState::Idle;
            } else if (config_.automatic) {
                // High fire rates may fire several shots per frame
                while (trigger_held_ && cooldown_ <= 0.0f && ammo_in_magazine_ > 0) {
                    PerformFire();
                }
            }
            break;

        case WeaponState::Reloading:
            reload_timer_ -= delta_time;
            if (reload_timer_ <= 0.0f) {
                const int32 to_add = std::min(config_.magazine_size - ammo_in_magazine_, reserve_ammo_);
                ammo_in_magazine_ += to_add;
                reserve_ammo_ -= to_add;
                state_ = WeaponState::Idle;
            }
            break;

        case WeaponState::Equipping:
            equip_timer_ -= delta_time;
            if (equip_timer_ <= 0.0f) state_ = WeaponState::Idle;
            break;

        case WeaponState::Idle:
            break;
    }
}

bool Weapon::Fire() {
    const bool new_pull = trigger_released_;
    trigger_held_ = true;
    trigger_released_ = false;

    if (state_ == WeaponState::Firing) return true;   // automatic fire continues in Update
    if (state_ != WeaponState::Idle || !new_pull) return false;

    if (ammo_in_magazine_ <= 0) {
        if (on_empty_) on_empty_();
        Reload();
        return false;
    }
    if (cooldown_ > 0.0f) return false;

    state_ = WeaponState::Firing;
    PerformFire();
    return true;
}

void Weapon::StopFiring() {
    trigger_held_ = false;
    trigger_released_ = true;
    if (state_ == WeaponState::Firing) state_ = WeaponState::Idle;
}

void Weapon::Reload() {
    if (state_ != WeaponState::Idle && state_ != WeaponState::Firing) return;
    if (ammo_in_magazine_ >= config_.magazine_size || reserve_ammo_ <= 0) return;
    state_ = WeaponState::Reloading;
    reload_timer_ = config_.reload_time;
}

void Weapon::Equip() {
    state_ = WeaponState::Equipping;
    equip_timer_ = config_.equip_time;
    trigger_held_ = false;
    trigger_released_ = true;
}

void Weapon::AddAmmo(int32 amount) {
    reserve_ammo_ = std::clamp(reserve_ammo_ + amount, 0, config_.max_ammo);
}

Math::Vector3 Weapon::ApplySpread(const Math::Vector3& direction) {
    if (config_.spread <= 0.0f) return direction;

    // Uniform direction inside a cone of half-angle `spread`
    std::uniform_real_distribution<float32> unit(0.0f, 1.0f);
    const float32 angle = config_.spread * kDegToRad * std::sqrt(unit(rng_));
    const float32 around = 2.0f * kPi * unit(rng_);

    const Math::Vector3 helper = std::abs(direction.y) < 0.99f ? Math::Vector3::Up : Math::Vector3::Right;
    const Math::Vector3 u = Math::Vector3::Cross(direction, helper).Normalized();
    const Math::Vector3 v = Math::Vector3::Cross(u, direction);
    const Math::Vector3 offset = (u * std::cos(around) + v * std::sin(around)) * std::sin(angle);
    return (direction * std::cos(angle) + offset).Normalized();
}

void Weapon::PerformFire() {
    if (ammo_in_magazine_ <= 0 || !owner_) return;

    --ammo_in_magazine_;
    ++shot_count_;
    cooldown_ += 1.0f / std::max(0.01f, config_.fire_rate);

    const Math::Vector3 origin = owner_->GetCameraPosition();
    const Math::Vector3 direction = ApplySpread(owner_->GetAimDirection());

    DamageInfo damage;
    damage.amount = config_.damage;
    damage.type = DamageType::Bullet;
    damage.origin = origin;
    damage.direction = direction;
    damage.instigator = owner_;
    damage.weapon = this;

    GameWorld& world = GameWorld::Instance();
    if (config_.is_hitscan) {
        last_shot_ = world.TraceShot(origin, direction, config_.range, damage, config_.critical_multiplier, owner_);
    } else {
        world.SpawnProjectile(origin, direction, config_.projectile_speed, damage, owner_, config_.projectile_gravity);
        last_shot_ = {};
        last_shot_.fired = true;
        last_shot_.origin = origin;
        last_shot_.end = origin + direction * config_.range;
    }

    if (config_.recoil > 0.0f) {
        std::uniform_real_distribution<float32> jitter(-1.0f, 1.0f);
        owner_->AddRecoil(config_.recoil * (0.9f + 0.1f * jitter(rng_)), config_.recoil * 0.3f * jitter(rng_));
    }
    if (on_shot_) on_shot_(last_shot_);
}

// ============================================================================
// Player
// ============================================================================

Player::Player(const PlayerConfig& config)
    : config_(config), eye_height_(config.eye_height), health_(config.max_health) {
    weapons_.resize(static_cast<size_t>(WeaponSlot::MaxSlots));

    health_.SetOnDeath([this](const DamageInfo& damage) {
        if (GameMode* mode = GameWorld::Instance().GetGameMode()) {
            mode->OnPlayerDeath(this, damage);
            if (auto* killer = dynamic_cast<Player*>(damage.instigator); killer && killer != this) {
                mode->OnPlayerKill(killer, this);
            }
        }
    });
}

Player::~Player() = default;

void Player::AttachController(SharedPtr<Physics::CharacterController> controller) {
    if (controller) fallback_position_ = controller->GetPosition();
    controller_ = std::move(controller);
}

SharedPtr<Physics::CharacterController> Player::DetachController() {
    if (controller_) fallback_position_ = controller_->GetPosition();
    return std::move(controller_);
}

void Player::Update(float32 delta_time) {
    UpdateLook();
    if (current_weapon_) current_weapon_->Update(delta_time);

    if (config_.health_regen_rate > 0.0f && health_.IsAlive() && health_.GetHealth() < health_.GetMaxHealth()) {
        health_regen_timer_ += delta_time;
        if (health_regen_timer_ >= config_.health_regen_delay) {
            health_.Heal(config_.health_regen_rate * delta_time);
        }
    }
}

void Player::FixedUpdate(float32 fixed_delta_time) {
    if (health_.IsAlive()) UpdateMovement(fixed_delta_time);
}

void Player::UpdateLook() {
    yaw_ += look_input_.x * config_.look_sensitivity;
    pitch_ -= look_input_.y * config_.look_sensitivity * (config_.invert_y ? -1.0f : 1.0f);
    pitch_ = std::clamp(pitch_, -config_.max_pitch, config_.max_pitch);
    yaw_ = std::fmod(yaw_, 360.0f);
    look_input_ = Math::Vector2(0.0f, 0.0f);
}

void Player::UpdateMovement(float32 dt) {
    const bool grounded = IsGrounded();

    float32 speed = config_.walk_speed;
    eye_height_ = config_.eye_height;
    if (crouch_pressed_) {
        speed = config_.crouch_speed;
        eye_height_ = config_.crouch_eye_height;
    } else if (sprint_pressed_ && move_input_.y > 0.5f && grounded) {
        speed = config_.run_speed;
    }

    Math::Vector3 direction = GetForward() * move_input_.y + GetRight() * move_input_.x;
    if (direction.LengthSquared() > 1.0f) direction = direction.Normalized();
    const Math::Vector3 wish = direction * speed;

    if (!controller_) {
        fallback_position_ += wish * dt;
        movement_state_ = MovementState::Walking;
        return;
    }

    Math::Vector3 velocity = wish;
    if (!grounded) {
        // Limited steering in the air
        const Math::Vector3 current = Horizontal(controller_->GetVelocity());
        const float32 blend = std::clamp(config_.air_control * 10.0f * dt, 0.0f, 1.0f);
        velocity = current + (wish - current) * blend;
    }
    const float32 jump_speed = jump_pressed_ && grounded ? std::sqrt(2.0f * config_.gravity * config_.jump_height) : 0.0f;
    controller_->Move(velocity, jump_speed, dt);

    if (!controller_->IsGrounded()) {
        movement_state_ = controller_->GetVelocity().y > 0.0f ? MovementState::Jumping : MovementState::Falling;
    } else if (crouch_pressed_) {
        movement_state_ = MovementState::Crouching;
    } else {
        movement_state_ = speed > config_.walk_speed ? MovementState::Running : MovementState::Walking;
    }
}

void Player::SetFireInput(bool pressed) {
    if (!current_weapon_ || !health_.IsAlive()) return;
    if (pressed) current_weapon_->Fire();
    else current_weapon_->StopFiring();
}

void Player::SetReloadInput(bool pressed) {
    if (pressed && current_weapon_) current_weapon_->Reload();
}

void Player::SetWeaponSwitchInput(int32 slot) {
    if (slot >= 0 && slot < static_cast<int32>(WeaponSlot::MaxSlots)) {
        SwitchWeapon(static_cast<WeaponSlot>(slot));
    }
}

void Player::AddRecoil(float32 pitch_degrees, float32 yaw_degrees) {
    pitch_ = std::clamp(pitch_ + pitch_degrees, -config_.max_pitch, config_.max_pitch);
    yaw_ += yaw_degrees;
}

Math::Vector3 Player::GetVelocity() const {
    return controller_ ? controller_->GetVelocity() : Math::Vector3();
}

bool Player::IsGrounded() const {
    return controller_ ? controller_->IsGrounded() : true;
}

Math::Vector3 Player::GetPosition() const {
    return controller_ ? controller_->GetPosition() : fallback_position_;
}

void Player::SetPosition(const Math::Vector3& position) {
    fallback_position_ = position;
    if (controller_) {
        Physics::CharacterState state;
        state.position = position;
        controller_->SetState(state);
    }
}

void Player::SetLookAngles(float32 yaw, float32 pitch) {
    yaw_ = yaw;
    pitch_ = std::clamp(pitch, -config_.max_pitch, config_.max_pitch);
}

Math::Quaternion Player::GetRotation() const {
    return Math::Quaternion::FromAxisAngle(Math::Vector3::Up, -yaw_ * kDegToRad);
}

Math::Vector3 Player::GetForward() const {
    const float32 yaw = yaw_ * kDegToRad;
    return Math::Vector3(std::sin(yaw), 0.0f, -std::cos(yaw));
}

Math::Vector3 Player::GetRight() const {
    const float32 yaw = yaw_ * kDegToRad;
    return Math::Vector3(std::cos(yaw), 0.0f, std::sin(yaw));
}

Math::Vector3 Player::GetCameraPosition() const {
    return GetPosition() + Math::Vector3(0.0f, eye_height_, 0.0f);
}

Math::Vector3 Player::GetAimDirection() const {
    const float32 yaw = yaw_ * kDegToRad;
    const float32 pitch = pitch_ * kDegToRad;
    return Math::Vector3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw));
}

float32 Player::GetCurrentFOV(float32 default_fov) const {
    return aim_pressed_ && current_weapon_ ? current_weapon_->GetConfig().aim_fov : default_fov;
}

void Player::GiveWeapon(SharedPtr<Weapon> weapon) {
    if (!weapon) return;
    const auto slot = weapon->GetConfig().slot;
    weapon->SetOwner(this);
    weapons_[static_cast<size_t>(slot)] = std::move(weapon);
    if (!current_weapon_) SwitchWeapon(slot);
}

void Player::RemoveWeapon(WeaponSlot slot) {
    const auto index = static_cast<size_t>(slot);
    if (index >= weapons_.size() || !weapons_[index]) return;

    const bool was_current = weapons_[index].get() == current_weapon_;
    weapons_[index].reset();
    if (was_current) {
        current_weapon_ = nullptr;
        for (auto& weapon : weapons_) {
            if (weapon) {
                current_weapon_ = weapon.get();
                current_weapon_->Equip();
                break;
            }
        }
    }
}

Weapon* Player::GetWeapon(WeaponSlot slot) const {
    const auto index = static_cast<size_t>(slot);
    return index < weapons_.size() ? weapons_[index].get() : nullptr;
}

void Player::SwitchWeapon(WeaponSlot slot) {
    Weapon* weapon = GetWeapon(slot);
    if (!weapon || weapon == current_weapon_) return;
    if (current_weapon_) current_weapon_->StopFiring();
    current_weapon_ = weapon;
    current_weapon_->Equip();
}

void Player::TakeDamage(const DamageInfo& damage) {
    if (!health_.IsAlive()) return;
    health_regen_timer_ = 0.0f;

    DamageInfo modified = damage;
    if (armor_ > 0.0f && damage.type != DamageType::Crush) {
        const float32 absorbed = std::min(damage.amount * config_.armor_absorption, armor_);
        armor_ -= absorbed;
        modified.amount -= absorbed;
    }
    health_.TakeDamage(modified);
}

void Player::Respawn(const Math::Vector3& position, float32 yaw) {
    health_.Revive();
    armor_ = 0.0f;
    health_regen_timer_ = 0.0f;
    SetPosition(position);
    SetLookAngles(yaw, 0.0f);
    for (auto& weapon : weapons_) {
        if (weapon) weapon->StopFiring();
    }
}

// ============================================================================
// Enemy
// ============================================================================

Enemy::Enemy(const EnemyConfig& config) : config_(config), health_(config.max_health) {
    health_.SetOnDeath([this](const DamageInfo& damage) { Die(damage); });
}

Enemy::~Enemy() = default;

void Enemy::AttachController(SharedPtr<Physics::CharacterController> controller) {
    if (controller) fallback_position_ = controller->GetPosition();
    controller_ = std::move(controller);
}

SharedPtr<Physics::CharacterController> Enemy::DetachController() {
    if (controller_) fallback_position_ = controller_->GetPosition();
    return std::move(controller_);
}

Math::Vector3 Enemy::GetPosition() const {
    return controller_ ? controller_->GetPosition() : fallback_position_;
}

void Enemy::SetPosition(const Math::Vector3& position) {
    fallback_position_ = position;
    if (controller_) controller_->SetPosition(position);
}

void Enemy::Update(float32 delta_time) {
    if (ai_state_ == EnemyAIState::Dead) return;
    attack_timer_ = std::max(0.0f, attack_timer_ - delta_time);
    UpdateAI(delta_time);
}

void Enemy::FixedUpdate(float32 fixed_delta_time) {
    const Math::Vector3 velocity = ai_state_ == EnemyAIState::Dead ? Math::Vector3::Zero : wish_velocity_;
    if (controller_) controller_->Move(velocity, 0.0f, fixed_delta_time);
    else fallback_position_ += velocity * fixed_delta_time;
}

void Enemy::TakeDamage(const DamageInfo& damage) {
    if (ai_state_ == EnemyAIState::Dead) return;
    health_.TakeDamage(damage);
    if (ai_state_ == EnemyAIState::Dead) return;

    if (auto* attacker = dynamic_cast<Player*>(damage.instigator)) {
        target_ = attacker;
        last_known_target_pos_ = attacker->GetPosition();
        const bool should_flee = config_.flee_health_fraction > 0.0f &&
                                 health_.GetHealthPercent() < config_.flee_health_fraction;
        if (should_flee) ai_state_ = EnemyAIState::Flee;
        else if (ai_state_ != EnemyAIState::Attack) ai_state_ = EnemyAIState::Chase;
    }
}

void Enemy::UpdateAI(float32 delta_time) {
    switch (ai_state_) {
        case EnemyAIState::Idle: UpdateIdle(); break;
        case EnemyAIState::Patrol: UpdatePatrol(); break;
        case EnemyAIState::Chase: UpdateChase(); break;
        case EnemyAIState::Attack: UpdateAttack(); break;
        case EnemyAIState::Search: UpdateSearch(delta_time); break;
        case EnemyAIState::Flee: UpdateFlee(); break;
        case EnemyAIState::Dead: break;
    }
}

void Enemy::MoveTowards(const Math::Vector3& point, float32 speed) {
    const Math::Vector3 to = Horizontal(point - GetPosition());
    if (to.LengthSquared() < 1e-6f) {
        wish_velocity_ = Math::Vector3::Zero;
        return;
    }
    const Math::Vector3 direction = to.Normalized();
    wish_velocity_ = direction * speed;
    FaceDirection(direction);
}

void Enemy::FaceDirection(const Math::Vector3& direction) {
    const Math::Vector3 flat = Horizontal(direction);
    if (flat.LengthSquared() > 1e-6f) rotation_ = Math::Quaternion::LookRotation(flat.Normalized(), Math::Vector3::Up);
}

void Enemy::UpdateIdle() {
    wish_velocity_ = Math::Vector3::Zero;
    if (CanSeeTarget() || CanHearTarget()) ai_state_ = EnemyAIState::Chase;
    else if (!patrol_points_.empty()) ai_state_ = EnemyAIState::Patrol;
}

void Enemy::UpdatePatrol() {
    if (CanSeeTarget()) {
        ai_state_ = EnemyAIState::Chase;
        return;
    }
    if (patrol_points_.empty()) {
        ai_state_ = EnemyAIState::Idle;
        return;
    }
    const Math::Vector3& point = patrol_points_[current_patrol_index_];
    if (Horizontal(point - GetPosition()).Length() < 0.5f) {
        current_patrol_index_ = (current_patrol_index_ + 1) % static_cast<uint32>(patrol_points_.size());
    }
    MoveTowards(patrol_points_[current_patrol_index_], config_.walk_speed);
}

void Enemy::UpdateChase() {
    if (!target_ || !target_->IsAlive()) {
        target_ = target_ && target_->IsAlive() ? target_ : nullptr;
        ai_state_ = EnemyAIState::Search;
        search_timer_ = config_.search_duration;
        return;
    }

    const bool sees = CanSeeTarget();
    if (sees) last_known_target_pos_ = target_->GetPosition();

    const float32 distance = Horizontal(last_known_target_pos_ - GetPosition()).Length();
    if (sees && distance <= config_.attack_range) {
        ai_state_ = EnemyAIState::Attack;
        wish_velocity_ = Math::Vector3::Zero;
        return;
    }
    if (!sees && !CanHearTarget() && distance < 1.0f) {
        ai_state_ = EnemyAIState::Search;
        search_timer_ = config_.search_duration;
        return;
    }
    MoveTowards(last_known_target_pos_, config_.run_speed);
}

void Enemy::UpdateAttack() {
    wish_velocity_ = Math::Vector3::Zero;
    if (!target_ || !target_->IsAlive()) {
        target_ = nullptr;
        ai_state_ = EnemyAIState::Idle;
        return;
    }

    const Math::Vector3 to_target = target_->GetPosition() - GetPosition();
    if (Horizontal(to_target).Length() > config_.attack_range * 1.25f || !CanSeeTarget()) {
        ai_state_ = EnemyAIState::Chase;
        return;
    }
    FaceDirection(to_target);
    if (attack_timer_ <= 0.0f) {
        Attack();
        attack_timer_ = 1.0f / std::max(0.01f, config_.attack_rate);
    }
}

void Enemy::UpdateSearch(float32 delta_time) {
    search_timer_ -= delta_time;
    if (CanSeeTarget() || CanHearTarget()) {
        ai_state_ = EnemyAIState::Chase;
        return;
    }
    MoveTowards(last_known_target_pos_, config_.walk_speed);
    if (search_timer_ <= 0.0f) {
        ai_state_ = patrol_points_.empty() ? EnemyAIState::Idle : EnemyAIState::Patrol;
    }
}

void Enemy::UpdateFlee() {
    if (!target_) {
        ai_state_ = EnemyAIState::Idle;
        return;
    }
    const Math::Vector3 away = Horizontal(GetPosition() - target_->GetPosition());
    if (away.Length() > config_.sight_range) {
        ai_state_ = EnemyAIState::Idle;
        wish_velocity_ = Math::Vector3::Zero;
        return;
    }
    MoveTowards(GetPosition() + (away.LengthSquared() > 1e-6f ? away.Normalized() : Math::Vector3::Forward),
                config_.run_speed);
}

bool Enemy::CanSeeTarget() const {
    if (!target_ || !target_->IsAlive()) return false;

    const Math::Vector3 eye = GetPosition() + Math::Vector3(0.0f, config_.height * 0.9f, 0.0f);
    const Math::Vector3 target_eye = target_->GetCameraPosition();
    Math::Vector3 to_target = target_eye - eye;
    const float32 distance = to_target.Length();
    if (distance > config_.sight_range) return false;
    if (distance < 1e-4f) return true;

    to_target = to_target / distance;
    const Math::Vector3 forward = rotation_.Rotate(Math::Vector3::Forward);
    const float32 cos_half_angle = std::cos(config_.sight_angle * 0.5f * kDegToRad);
    if (Math::Vector3::Dot(Horizontal(forward).Normalized(), Horizontal(to_target).Normalized()) < cos_half_angle) {
        return false;
    }

    // Line of sight: the first thing hit must be the target
    Physics::QueryFilter filter;
    filter.ignore_character = controller_.get();
    const Physics::RaycastResult hit = GameWorld::Instance().GetPhysics().Raycast(eye, target_eye, filter);
    return !hit.hit || GameWorld::Instance().FindDamageable(hit.body_user_data) == target_;
}

bool Enemy::CanHearTarget() const {
    return target_ && target_->IsAlive() &&
           (target_->GetPosition() - GetPosition()).Length() <= config_.hearing_range;
}

void Enemy::Attack() {
    if (!target_) return;
    DamageInfo damage;
    damage.amount = config_.damage;
    damage.type = DamageType::Melee;
    damage.origin = GetPosition();
    damage.direction = (target_->GetPosition() - GetPosition()).Normalized();
    damage.hit_point = target_->GetPosition();
    damage.instigator = this;
    target_->TakeDamage(damage);
}

void Enemy::Die(const DamageInfo& damage) {
    ai_state_ = EnemyAIState::Dead;
    wish_velocity_ = Math::Vector3::Zero;
    if (GameMode* mode = GameWorld::Instance().GetGameMode()) {
        mode->AddScore(config_.score_value);
        mode->OnEnemyDeath(this, damage);
    }
}

// ============================================================================
// GameMode
// ============================================================================

void GameMode::Initialize() {
    match_active_ = false;
    match_time_ = 0.0f;
    score_ = 0;
}

void GameMode::Shutdown() {
    players_.clear();
    enemies_.clear();
}

void GameMode::Update(float32 delta_time) {
    if (!match_active_) return;
    match_time_ += delta_time;
    if (time_limit_ > 0.0f && match_time_ >= time_limit_) EndMatch();
}

void GameMode::StartMatch() {
    match_active_ = true;
    match_time_ = 0.0f;
    score_ = 0;
}

void GameMode::EndMatch() {
    match_active_ = false;
}

void GameMode::RestartMatch() {
    EndMatch();
    StartMatch();
}

void GameMode::OnPlayerSpawn(Player* player) {
    if (player && std::find(players_.begin(), players_.end(), player) == players_.end()) players_.push_back(player);
}

void GameMode::OnPlayerDeath(Player* /*player*/, const DamageInfo& /*damage*/) {}

void GameMode::OnPlayerKill(Player* /*killer*/, Player* /*victim*/) {}

void GameMode::OnEnemySpawn(Enemy* enemy) {
    if (enemy && std::find(enemies_.begin(), enemies_.end(), enemy) == enemies_.end()) enemies_.push_back(enemy);
}

void GameMode::OnEnemyDeath(Enemy* enemy, const DamageInfo& /*damage*/) {
    enemies_.erase(std::remove(enemies_.begin(), enemies_.end(), enemy), enemies_.end());
}

// ============================================================================
// GameWorld
// ============================================================================

GameWorld& GameWorld::Instance() {
    static GameWorld instance;
    return instance;
}

bool GameWorld::Initialize(const Math::Vector3& gravity) {
    return physics_.Initialize(gravity);
}

void GameWorld::Shutdown() {
    if (game_mode_) game_mode_->Shutdown();
    game_mode_.reset();
    for (auto& player : players_) physics_.DestroyCharacterController(player->DetachController());
    for (auto& enemy : enemies_) physics_.DestroyCharacterController(enemy->DetachController());
    players_.clear();
    enemies_.clear();
    damageables_.clear();
    spawn_points_.clear();
    projectiles_.clear();
    static_bodies_.clear();
    local_player_ = nullptr;
    physics_.Shutdown();
}

void GameWorld::Update(float32 delta_time) {
    if (game_mode_) game_mode_->Update(delta_time);
    for (auto& player : players_) player->Update(delta_time);
    for (auto& enemy : enemies_) enemy->Update(delta_time);
}

void GameWorld::FixedUpdate(float32 fixed_delta_time) {
    for (auto& player : players_) player->FixedUpdate(fixed_delta_time);
    for (auto& enemy : enemies_) enemy->FixedUpdate(fixed_delta_time);
    physics_.Step(fixed_delta_time);
    UpdateProjectiles(fixed_delta_time);
}

SharedPtr<Physics::RigidBody> GameWorld::AddStaticBox(const Math::Vector3& center, const Math::Vector3& size,
                                                      const Math::Quaternion& rotation) {
    Physics::RigidBodyDesc desc;
    desc.type = Physics::RigidBodyType::Static;
    desc.shape.type = Physics::CollisionShapeType::Box;
    desc.shape.half_extents = size * 0.5f;
    desc.initial_transform.position = center;
    desc.initial_transform.rotation = rotation;
    desc.layer = Physics::CollisionLayer::Static;
    auto body = physics_.CreateRigidBody(desc);
    if (body) static_bodies_.push_back(body);
    return body;
}

SharedPtr<Physics::CharacterController> GameWorld::CreateController(Damageable* owner, float32 radius, float32 height,
                                                                    float32 gravity, const Math::Vector3& feet) {
    if (!physics_.IsInitialized()) return nullptr;
    Physics::CharacterSettings settings;
    settings.radius = radius;
    settings.height = height;
    settings.gravity = gravity;
    settings.user_data = owner;   // resolved through FindDamageable
    return physics_.CreateCharacterController(settings, feet);
}

Player* GameWorld::SpawnPlayer(const Math::Vector3& position, const PlayerConfig& config, float32 yaw) {
    auto player = MakeShared<Player>(config);
    player->AttachController(CreateController(player.get(), config.radius, config.standing_height, config.gravity,
                                              position));
    player->SetPosition(position);
    player->SetLookAngles(yaw, 0.0f);

    damageables_.insert(player.get());
    players_.push_back(player);
    if (!local_player_) local_player_ = player.get();
    if (game_mode_) game_mode_->OnPlayerSpawn(player.get());
    return player.get();
}

void GameWorld::RemovePlayer(Player* player) {
    const auto it = std::find_if(players_.begin(), players_.end(),
                                 [player](const SharedPtr<Player>& p) { return p.get() == player; });
    if (it == players_.end()) return;

    physics_.DestroyCharacterController(player->DetachController());
    damageables_.erase(player);
    for (auto& enemy : enemies_) {
        if (enemy->GetTarget() == player) enemy->SetTarget(nullptr);
    }
    players_.erase(it);
    if (local_player_ == player) local_player_ = players_.empty() ? nullptr : players_.front().get();
}

Enemy* GameWorld::SpawnEnemy(const EnemyConfig& config, const Math::Vector3& position) {
    auto enemy = MakeShared<Enemy>(config);
    enemy->AttachController(CreateController(enemy.get(), config.radius, config.height, 20.0f, position));
    enemy->SetPosition(position);
    if (local_player_) enemy->SetTarget(local_player_);

    damageables_.insert(enemy.get());
    enemies_.push_back(enemy);
    if (game_mode_) game_mode_->OnEnemySpawn(enemy.get());
    return enemy.get();
}

void GameWorld::RemoveEnemy(Enemy* enemy) {
    const auto it = std::find_if(enemies_.begin(), enemies_.end(),
                                 [enemy](const SharedPtr<Enemy>& e) { return e.get() == enemy; });
    if (it == enemies_.end()) return;
    physics_.DestroyCharacterController(enemy->DetachController());
    damageables_.erase(enemy);
    enemies_.erase(it);
}

void GameWorld::AddSpawnPoint(const SpawnPoint& point) {
    spawn_points_.push_back(point);
}

bool GameWorld::GetRandomSpawnPoint(bool player_spawn, SpawnPoint& out) {
    Array<const SpawnPoint*> candidates;
    for (const auto& point : spawn_points_) {
        if (player_spawn ? point.is_player_spawn : point.is_enemy_spawn) candidates.push_back(&point);
    }
    if (candidates.empty()) return false;
    std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
    out = *candidates[pick(rng_)];
    return true;
}

void GameWorld::SetGameMode(SharedPtr<GameMode> mode) {
    if (game_mode_) game_mode_->Shutdown();
    game_mode_ = std::move(mode);
    if (!game_mode_) return;
    game_mode_->Initialize();
    for (auto& player : players_) game_mode_->OnPlayerSpawn(player.get());
    for (auto& enemy : enemies_) game_mode_->OnEnemySpawn(enemy.get());
}

Damageable* GameWorld::FindDamageable(void* user_data) const {
    auto* candidate = static_cast<Damageable*>(user_data);
    return candidate && damageables_.count(candidate) ? candidate : nullptr;
}

ShotResult GameWorld::TraceShot(const Math::Vector3& origin, const Math::Vector3& direction, float32 range,
                                DamageInfo damage, float32 critical_multiplier, Damageable* ignore) {
    ShotResult shot;
    shot.fired = true;
    shot.origin = origin;
    shot.end = origin + direction * range;

    for (const Physics::RaycastResult& hit : physics_.RaycastAll(origin, shot.end)) {
        Damageable* target = FindDamageable(hit.body_user_data);
        if (target && target == ignore) continue;

        shot.hit = true;
        shot.end = hit.point;
        shot.normal = hit.normal;
        if (target && target->IsAlive()) {
            shot.target = target;
            const float32 critical_height = target->GetCriticalHeight();
            shot.critical = critical_height > 0.0f && hit.point.y - target->GetPosition().y >= critical_height;
            damage.hit_point = hit.point;
            damage.is_critical = shot.critical;
            if (shot.critical) damage.amount *= critical_multiplier;
            target->TakeDamage(damage);
        }
        break;
    }
    return shot;
}

void GameWorld::SpawnProjectile(const Math::Vector3& position, const Math::Vector3& direction, float32 speed,
                                const DamageInfo& damage, Damageable* owner, float32 gravity, float32 lifetime) {
    Projectile projectile;
    projectile.position = position;
    projectile.velocity = direction.Normalized() * speed;
    projectile.damage = damage;
    projectile.owner = owner;
    projectile.gravity = gravity;
    projectile.lifetime = lifetime;
    projectiles_.push_back(projectile);
}

void GameWorld::UpdateProjectiles(float32 dt) {
    for (auto it = projectiles_.begin(); it != projectiles_.end();) {
        Projectile& p = *it;
        p.lifetime -= dt;
        p.velocity.y -= p.gravity * dt;
        const Math::Vector3 next = p.position + p.velocity * dt;

        bool consumed = p.lifetime <= 0.0f;
        for (const Physics::RaycastResult& hit : physics_.RaycastAll(p.position, next)) {
            Damageable* target = FindDamageable(hit.body_user_data);
            if (target && target == p.owner) continue;
            if (target && target->IsAlive()) {
                DamageInfo damage = p.damage;
                damage.hit_point = hit.point;
                damage.direction = p.velocity.Normalized();
                target->TakeDamage(damage);
            }
            if (on_projectile_impact_) on_projectile_impact_(p, hit);
            consumed = true;
            break;
        }

        if (consumed) {
            it = projectiles_.erase(it);
        } else {
            p.position = next;
            ++it;
        }
    }
}

} // namespace DRX::Game

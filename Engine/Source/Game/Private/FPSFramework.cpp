/**
 * DRX Engine - FPS Game Framework Implementation
 */

#include "Game/Public/FPSFramework.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace DRX::Game {

// ============================================================================
// HealthComponent
// ============================================================================

HealthComponent::HealthComponent(float32 max_health)
    : health_(max_health), max_health_(max_health) {}

void HealthComponent::TakeDamage(const DamageInfo& damage) {
    if (invulnerable || IsDead()) return;

    float32 actual_damage = damage.amount * damage_multiplier;

    // Apply critical hit multiplier
    if (damage.is_critical) {
        actual_damage *= 2.0f;
    }

    health_ = std::max(0.0f, health_ - actual_damage);

    if (on_damage_) {
        on_damage_(damage);
    }

    if (IsDead() && on_death_) {
        on_death_(damage);
    }
}

void HealthComponent::Heal(float32 amount) {
    if (IsDead()) return;

    float32 old_health = health_;
    health_ = std::min(health_ + amount, max_health_);

    if (on_heal_ && health_ > old_health) {
        on_heal_(health_ - old_health);
    }
}

void HealthComponent::SetHealth(float32 health) {
    health_ = std::clamp(health, 0.0f, max_health_);
}

void HealthComponent::SetMaxHealth(float32 max_health) {
    max_health_ = max_health;
    health_ = std::min(health_, max_health_);
}

// ============================================================================
// Weapon
// ============================================================================

Weapon::Weapon(const WeaponConfig& config)
    : config_(config),
      ammo_in_magazine_(config.magazine_size),
      reserve_ammo_(config.max_ammo) {}

void Weapon::Update(float32 delta_time) {
    switch (state_) {
        case WeaponState::Firing:
            fire_timer_ -= delta_time;
            if (fire_timer_ <= 0 && trigger_held_) {
                PerformFire();
            }
            if (!trigger_held_) {
                state_ = WeaponState::Idle;
            }
            break;

        case WeaponState::Reloading:
            reload_timer_ -= delta_time;
            if (reload_timer_ <= 0) {
                int32 ammo_needed = config_.magazine_size - ammo_in_magazine_;
                int32 ammo_to_add = std::min(ammo_needed, reserve_ammo_);
                ammo_in_magazine_ += ammo_to_add;
                reserve_ammo_ -= ammo_to_add;
                state_ = WeaponState::Idle;
            }
            break;

        case WeaponState::Equipping:
            equip_timer_ -= delta_time;
            if (equip_timer_ <= 0) {
                state_ = WeaponState::Idle;
            }
            break;

        case WeaponState::Unequipping:
            equip_timer_ -= delta_time;
            if (equip_timer_ <= 0) {
                state_ = WeaponState::Idle;
            }
            break;

        default:
            break;
    }
}

bool Weapon::Fire() {
    if (state_ != WeaponState::Idle && state_ != WeaponState::Firing) return false;

    trigger_held_ = true;

    if (ammo_in_magazine_ <= 0) {
        // Play empty click sound
        // Audio::AudioSystem::Instance().PlaySound(config_.empty_sound);
        return false;
    }

    if (state_ == WeaponState::Idle) {
        state_ = WeaponState::Firing;
        PerformFire();
    }

    return true;
}

void Weapon::StopFiring() {
    trigger_held_ = false;
}

void Weapon::Reload() {
    if (state_ != WeaponState::Idle) return;
    if (ammo_in_magazine_ >= config_.magazine_size) return;
    if (reserve_ammo_ <= 0) return;

    state_ = WeaponState::Reloading;
    reload_timer_ = config_.reload_time;

    // Play reload sound
    // Audio::AudioSystem::Instance().PlaySound(config_.reload_sound);
}

void Weapon::Equip() {
    state_ = WeaponState::Equipping;
    equip_timer_ = 0.5f;
}

void Weapon::Unequip() {
    state_ = WeaponState::Unequipping;
    equip_timer_ = 0.3f;
}

void Weapon::AddAmmo(int32 amount) {
    reserve_ammo_ = std::min(reserve_ammo_ + amount, config_.max_ammo);
}

void Weapon::PerformFire() {
    if (ammo_in_magazine_ <= 0) return;

    ammo_in_magazine_--;
    fire_timer_ = 1.0f / config_.fire_rate;

    // Play fire sound
    // Audio::AudioSystem::Instance().PlaySound(config_.fire_sound);

    // Apply recoil
    ApplyRecoil();

    // Perform raycast or spawn projectile
    if (owner_) {
        Math::Vector3 aim_origin = owner_->GetCameraPosition();
        Math::Vector3 aim_dir = owner_->GetAimDirection();

        // Apply spread
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float32> spread_dis(-1.0f, 1.0f);

        float32 spread_rad = config_.spread * 3.14159265359f / 180.0f;
        float32 spread_x = spread_dis(gen) * spread_rad;
        float32 spread_y = spread_dis(gen) * spread_rad;

        // Rotate aim direction by spread
        Math::Quaternion spread_rot = Math::Quaternion::FromEuler(
            Math::Vector3(spread_x, spread_y, 0)
        );
        aim_dir = spread_rot.Rotate(aim_dir);

        if (config_.is_hitscan) {
            // Hitscan weapon - raycast
            // Physics::RaycastHit hit;
            // if (Physics::PhysicsWorld::Instance().Raycast(aim_origin, aim_dir, config_.range, hit)) {
            //     // Deal damage to hit target
            //     DamageInfo damage;
            //     damage.amount = config_.damage;
            //     damage.type = DamageType::Bullet;
            //     damage.origin = aim_origin;
            //     damage.direction = aim_dir;
            //     damage.instigator = owner_;
            //     damage.weapon = this;
            //     damage.impulse = 500.0f;
            // }
        } else {
            // Projectile weapon
            GameWorld::Instance().SpawnProjectile(
                aim_origin,
                aim_dir,
                config_.projectile_speed,
                config_.damage,
                owner_
            );
        }
    }
}

void Weapon::ApplyRecoil() {
    if (!owner_) return;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float32> dis(-1.0f, 1.0f);

    float32 recoil_pitch = config_.recoil * (0.8f + dis(gen) * 0.2f);
    float32 recoil_yaw = config_.recoil * 0.3f * dis(gen);

    // Apply to player look
    Math::Vector2 recoil_input(recoil_yaw, -recoil_pitch);
    owner_->SetLookInput(recoil_input);
}

// ============================================================================
// Player
// ============================================================================

Player::Player(const PlayerConfig& config)
    : config_(config),
      current_height_(config.standing_height),
      health_(config.max_health) {
    weapons_.resize(static_cast<size_t>(WeaponSlot::MaxSlots));

    health_.SetOnDeath([this](const DamageInfo& damage) {
        // Handle player death
    });
}

Player::~Player() {
    weapons_.clear();
}

void Player::Update(float32 delta_time) {
    UpdateLook(delta_time);
    UpdateMovement(delta_time);
    UpdateWeapons(delta_time);

    // Health regeneration
    if (config_.health_regen_rate > 0 && health_.GetHealth() < health_.GetMaxHealth()) {
        health_regen_timer_ += delta_time;
        if (health_regen_timer_ >= config_.health_regen_delay) {
            health_.Heal(config_.health_regen_rate * delta_time);
        }
    }
}

void Player::FixedUpdate(float32 fixed_delta_time) {
    CheckGround();
    ApplyGravity(fixed_delta_time);

    // Update physics
    if (character_controller_) {
        // character_controller_->Move(velocity_ * fixed_delta_time);
    }
}

void Player::SetFireInput(bool pressed) {
    if (current_weapon_) {
        if (pressed) {
            current_weapon_->Fire();
        } else {
            current_weapon_->StopFiring();
        }
    }
}

void Player::SetReloadInput(bool pressed) {
    if (pressed && current_weapon_) {
        current_weapon_->Reload();
    }
}

void Player::SetWeaponSwitchInput(int32 slot) {
    if (slot >= 0 && slot < static_cast<int32>(WeaponSlot::MaxSlots)) {
        SwitchWeapon(static_cast<WeaponSlot>(slot));
    }
}

void Player::SetPosition(const Math::Vector3& position) {
    position_ = position;
}

Math::Quaternion Player::GetRotation() const {
    float32 rad = 3.14159265359f / 180.0f;
    return Math::Quaternion::FromAxisAngle(Math::Vector3::Up, yaw_ * rad);
}

Math::Vector3 Player::GetCameraPosition() const {
    return position_ + Math::Vector3(0, current_height_ + config_.camera_height_offset, 0);
}

Math::Vector3 Player::GetAimDirection() const {
    float32 rad = 3.14159265359f / 180.0f;
    float32 cos_pitch = cos(pitch_ * rad);
    float32 sin_pitch = sin(pitch_ * rad);
    float32 cos_yaw = cos(yaw_ * rad);
    float32 sin_yaw = sin(yaw_ * rad);

    return Math::Vector3(
        cos_pitch * sin_yaw,
        -sin_pitch,
        cos_pitch * cos_yaw
    );
}

float32 Player::GetCurrentFOV() const {
    if (aim_pressed_ && current_weapon_) {
        return current_weapon_->GetConfig().aim_fov;
    }
    return 90.0f;  // Default FOV
}

void Player::GiveWeapon(SharedPtr<Weapon> weapon) {
    if (!weapon) return;

    WeaponSlot slot = weapon->GetConfig().slot;
    weapons_[static_cast<size_t>(slot)] = weapon;
    weapon->SetOwner(this);

    if (!current_weapon_) {
        SwitchWeapon(slot);
    }
}

void Player::RemoveWeapon(WeaponSlot slot) {
    size_t index = static_cast<size_t>(slot);
    if (index < weapons_.size()) {
        if (weapons_[index].get() == current_weapon_) {
            current_weapon_ = nullptr;
            // Find another weapon to equip
            for (auto& w : weapons_) {
                if (w) {
                    current_weapon_ = w.get();
                    current_weapon_->Equip();
                    break;
                }
            }
        }
        weapons_[index].reset();
    }
}

Weapon* Player::GetWeapon(WeaponSlot slot) const {
    size_t index = static_cast<size_t>(slot);
    return index < weapons_.size() ? weapons_[index].get() : nullptr;
}

void Player::SwitchWeapon(WeaponSlot slot) {
    Weapon* new_weapon = GetWeapon(slot);
    if (!new_weapon || new_weapon == current_weapon_) return;

    if (current_weapon_) {
        current_weapon_->Unequip();
    }

    current_weapon_ = new_weapon;
    current_slot_ = slot;
    current_weapon_->Equip();
}

void Player::AddArmor(float32 amount) {
    armor_ = std::min(armor_ + amount, config_.max_armor);
}

void Player::TakeDamage(const DamageInfo& damage) {
    // Reset health regen timer
    health_regen_timer_ = 0;

    DamageInfo modified = damage;

    // Armor absorption
    if (armor_ > 0) {
        float32 armor_absorption = std::min(damage.amount * 0.5f, armor_);
        armor_ -= armor_absorption;
        modified.amount -= armor_absorption;
    }

    health_.TakeDamage(modified);
}

void Player::UpdateMovement(float32 delta_time) {
    // Determine movement state and speed
    float32 target_speed = config_.walk_speed;

    if (crouch_pressed_) {
        movement_state_ = MovementState::Crouching;
        target_speed = config_.crouch_speed;
        current_height_ = config_.crouch_height;
    } else if (sprint_pressed_ && move_input_.y > 0.5f && is_grounded_) {
        movement_state_ = MovementState::Running;
        target_speed = config_.run_speed;
        current_height_ = config_.standing_height;
    } else if (!is_grounded_) {
        movement_state_ = velocity_.y > 0 ? MovementState::Jumping : MovementState::Falling;
    } else {
        movement_state_ = MovementState::Walking;
        current_height_ = config_.standing_height;
    }

    // Calculate move direction
    float32 rad = 3.14159265359f / 180.0f;
    float32 cos_yaw = cos(yaw_ * rad);
    float32 sin_yaw = sin(yaw_ * rad);

    Math::Vector3 forward(sin_yaw, 0, cos_yaw);
    Math::Vector3 right(cos_yaw, 0, -sin_yaw);

    Math::Vector3 move_dir = forward * move_input_.y + right * move_input_.x;
    if (move_dir.LengthSquared() > 1.0f) {
        move_dir = move_dir.Normalized();
    }

    // Apply movement
    if (is_grounded_) {
        velocity_.x = move_dir.x * target_speed;
        velocity_.z = move_dir.z * target_speed;
    } else {
        // Air control
        velocity_.x += move_dir.x * target_speed * config_.air_control * delta_time;
        velocity_.z += move_dir.z * target_speed * config_.air_control * delta_time;

        // Clamp horizontal velocity
        float32 horizontal_speed = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
        if (horizontal_speed > target_speed) {
            velocity_.x = (velocity_.x / horizontal_speed) * target_speed;
            velocity_.z = (velocity_.z / horizontal_speed) * target_speed;
        }
    }

    // Jump
    if (jump_pressed_ && is_grounded_) {
        Jump();
    }

    // Apply velocity to position
    position_ = position_ + velocity_ * delta_time;
}

void Player::UpdateLook(float32 delta_time) {
    yaw_ += look_input_.x * config_.look_sensitivity;
    pitch_ += look_input_.y * config_.look_sensitivity * (config_.invert_y ? -1.0f : 1.0f);
    pitch_ = std::clamp(pitch_, -config_.max_pitch, config_.max_pitch);

    // Clear look input
    look_input_ = Math::Vector2(0, 0);
}

void Player::UpdateWeapons(float32 delta_time) {
    if (current_weapon_) {
        current_weapon_->Update(delta_time);
    }
}

void Player::CheckGround() {
    // Raycast down to check if grounded
    // For now, simple Y check
    is_grounded_ = position_.y <= 0.01f;
    if (is_grounded_ && position_.y < 0) {
        position_.y = 0;
        velocity_.y = 0;
    }
}

void Player::ApplyGravity(float32 delta_time) {
    if (!is_grounded_) {
        velocity_.y -= 9.81f * delta_time;
    }
}

void Player::Jump() {
    velocity_.y = config_.jump_force;
    is_grounded_ = false;
    movement_state_ = MovementState::Jumping;
}

// ============================================================================
// Enemy
// ============================================================================

Enemy::Enemy(const EnemyConfig& config)
    : config_(config), health_(config.max_health) {

    health_.SetOnDeath([this](const DamageInfo& damage) {
        Die(damage);
    });
}

Enemy::~Enemy() {}

void Enemy::Update(float32 delta_time) {
    if (ai_state_ == EnemyAIState::Dead) return;

    UpdateAI(delta_time);

    // Update attack timer
    if (attack_timer_ > 0) {
        attack_timer_ -= delta_time;
    }
}

void Enemy::TakeDamage(const DamageInfo& damage) {
    health_.TakeDamage(damage);

    // Alert to attacker
    if (damage.instigator) {
        target_ = static_cast<Player*>(damage.instigator);
        last_known_target_pos_ = target_->GetPosition();

        if (ai_state_ != EnemyAIState::Attack && ai_state_ != EnemyAIState::Dead) {
            ai_state_ = EnemyAIState::Chase;
        }
    }
}

void Enemy::UpdateAI(float32 delta_time) {
    switch (ai_state_) {
        case EnemyAIState::Idle:
            UpdateIdle(delta_time);
            break;
        case EnemyAIState::Patrol:
            UpdatePatrol(delta_time);
            break;
        case EnemyAIState::Chase:
            UpdateChase(delta_time);
            break;
        case EnemyAIState::Attack:
            UpdateAttack(delta_time);
            break;
        case EnemyAIState::Search:
            UpdateSearch(delta_time);
            break;
        case EnemyAIState::Flee:
            UpdateFlee(delta_time);
            break;
        default:
            break;
    }
}

void Enemy::UpdateIdle(float32 delta_time) {
    // Check for player
    if (CanSeeTarget() || CanHearTarget()) {
        ai_state_ = EnemyAIState::Chase;
        return;
    }

    // Random chance to start patrolling
    if (!patrol_points_.empty()) {
        ai_state_ = EnemyAIState::Patrol;
    }
}

void Enemy::UpdatePatrol(float32 delta_time) {
    // Check for player
    if (CanSeeTarget()) {
        ai_state_ = EnemyAIState::Chase;
        return;
    }

    if (patrol_points_.empty()) {
        ai_state_ = EnemyAIState::Idle;
        return;
    }

    // Move towards current patrol point
    Math::Vector3 target = patrol_points_[current_patrol_index_];
    Math::Vector3 dir = target - position_;
    float32 dist = dir.Length();

    if (dist < 1.0f) {
        // Reached patrol point
        current_patrol_index_ = (current_patrol_index_ + 1) % patrol_points_.size();
    } else {
        dir = dir.Normalized();
        velocity_ = dir * config_.walk_speed;
        position_ = position_ + velocity_ * delta_time;

        // Face movement direction
        rotation_ = Math::Quaternion::LookRotation(dir, Math::Vector3::Up);
    }
}

void Enemy::UpdateChase(float32 delta_time) {
    if (!target_) {
        ai_state_ = EnemyAIState::Search;
        return;
    }

    if (CanSeeTarget()) {
        last_known_target_pos_ = target_->GetPosition();
    }

    // Move towards target
    Math::Vector3 dir = last_known_target_pos_ - position_;
    float32 dist = dir.Length();

    if (dist <= config_.attack_range) {
        ai_state_ = EnemyAIState::Attack;
        return;
    }

    // Check if we lost the target
    if (!CanSeeTarget() && !CanHearTarget()) {
        if (dist < 2.0f) {
            ai_state_ = EnemyAIState::Search;
            search_timer_ = config_.search_duration;
            return;
        }
    }

    dir = dir.Normalized();
    velocity_ = dir * config_.run_speed;
    position_ = position_ + velocity_ * delta_time;
    rotation_ = Math::Quaternion::LookRotation(dir, Math::Vector3::Up);
}

void Enemy::UpdateAttack(float32 delta_time) {
    if (!target_ || target_->GetHealth().IsDead()) {
        ai_state_ = EnemyAIState::Idle;
        target_ = nullptr;
        return;
    }

    // Face target
    Math::Vector3 dir = target_->GetPosition() - position_;
    float32 dist = dir.Length();

    if (dist > config_.attack_range * 1.5f) {
        ai_state_ = EnemyAIState::Chase;
        return;
    }

    dir = dir.Normalized();
    rotation_ = Math::Quaternion::LookRotation(dir, Math::Vector3::Up);

    // Attack when ready
    if (attack_timer_ <= 0) {
        Attack();
        attack_timer_ = 1.0f / config_.attack_rate;
    }
}

void Enemy::UpdateSearch(float32 delta_time) {
    search_timer_ -= delta_time;

    if (CanSeeTarget() || CanHearTarget()) {
        ai_state_ = EnemyAIState::Chase;
        return;
    }

    if (search_timer_ <= 0) {
        ai_state_ = EnemyAIState::Patrol;
        target_ = nullptr;
    }

    // Look around randomly
    // Move to last known position
}

void Enemy::UpdateFlee(float32 delta_time) {
    if (!target_) {
        ai_state_ = EnemyAIState::Idle;
        return;
    }

    // Run away from target
    Math::Vector3 dir = position_ - target_->GetPosition();
    dir = dir.Normalized();

    velocity_ = dir * config_.run_speed;
    position_ = position_ + velocity_ * delta_time;
    rotation_ = Math::Quaternion::LookRotation(dir, Math::Vector3::Up);

    // Stop fleeing if far enough
    float32 dist = (position_ - target_->GetPosition()).Length();
    if (dist > config_.sight_range * 1.5f) {
        ai_state_ = EnemyAIState::Idle;
    }
}

bool Enemy::CanSeeTarget() const {
    if (!target_) return false;

    Math::Vector3 to_target = target_->GetPosition() - position_;
    float32 dist = to_target.Length();

    if (dist > config_.sight_range) return false;

    // Check angle
    to_target = to_target.Normalized();
    Math::Vector3 forward = rotation_.Rotate(Math::Vector3::Forward);
    float32 dot = Math::Vector3::Dot(forward, to_target);
    float32 angle = std::acos(dot) * 180.0f / 3.14159265359f;

    if (angle > config_.sight_angle * 0.5f) return false;

    // Would also raycast for line of sight check
    return true;
}

bool Enemy::CanHearTarget() const {
    if (!target_) return false;

    float32 dist = (target_->GetPosition() - position_).Length();
    return dist <= config_.hearing_range;
}

void Enemy::Attack() {
    if (!target_) return;

    DamageInfo damage;
    damage.amount = config_.damage;
    damage.type = DamageType::Melee;
    damage.origin = position_;
    damage.direction = (target_->GetPosition() - position_).Normalized();
    damage.instigator = nullptr;  // Would be this enemy
    damage.impulse = 200.0f;

    target_->TakeDamage(damage);
}

void Enemy::Die(const DamageInfo& damage) {
    ai_state_ = EnemyAIState::Dead;
    velocity_ = Math::Vector3::Zero;

    // Award score/XP
    GameMode* mode = GameWorld::Instance().GetGameMode();
    if (mode) {
        mode->AddScore(config_.score_value);
    }
}

// ============================================================================
// GameMode
// ============================================================================

GameMode::GameMode() {}

void GameMode::Initialize() {
    match_active_ = false;
    match_time_ = 0;
    score_ = 0;
}

void GameMode::Shutdown() {
    players_.clear();
    enemies_.clear();
}

void GameMode::Update(float32 delta_time) {
    if (!match_active_) return;

    match_time_ += delta_time;

    // Check time limit
    if (time_limit_ > 0 && match_time_ >= time_limit_) {
        EndMatch();
    }
}

void GameMode::StartMatch() {
    match_active_ = true;
    match_time_ = 0;
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
    if (player && std::find(players_.begin(), players_.end(), player) == players_.end()) {
        players_.push_back(player);
    }
}

void GameMode::OnPlayerDeath(Player* player, const DamageInfo& damage) {
    // Handle player death - respawn, game over, etc.
}

void GameMode::OnPlayerKill(Player* killer, Player* victim) {
    // Handle PvP kills
}

void GameMode::OnEnemySpawn(Enemy* enemy) {
    if (enemy && std::find(enemies_.begin(), enemies_.end(), enemy) == enemies_.end()) {
        enemies_.push_back(enemy);
    }
}

void GameMode::OnEnemyDeath(Enemy* enemy, const DamageInfo& damage) {
    enemies_.erase(
        std::remove(enemies_.begin(), enemies_.end(), enemy),
        enemies_.end()
    );
}

void GameMode::AddScore(int32 amount) {
    score_ += amount;
}

// ============================================================================
// GameWorld
// ============================================================================

GameWorld& GameWorld::Instance() {
    static GameWorld instance;
    return instance;
}

bool GameWorld::Initialize() {
    return true;
}

void GameWorld::Shutdown() {
    players_.clear();
    enemies_.clear();
    spawn_points_.clear();
    projectiles_.clear();
    game_mode_.reset();
    local_player_ = nullptr;
}

void GameWorld::Update(float32 delta_time) {
    // Update game mode
    if (game_mode_) {
        game_mode_->Update(delta_time);
    }

    // Update players
    for (auto& player : players_) {
        player->Update(delta_time);
    }

    // Update enemies
    for (auto& enemy : enemies_) {
        enemy->Update(delta_time);
    }

    // Update projectiles
    for (auto it = projectiles_.begin(); it != projectiles_.end();) {
        it->lifetime -= delta_time;
        if (it->lifetime <= 0) {
            it = projectiles_.erase(it);
            continue;
        }

        it->position = it->position + it->velocity * delta_time;

        // Check for collision
        // Physics raycast here

        ++it;
    }
}

void GameWorld::FixedUpdate(float32 fixed_delta_time) {
    for (auto& player : players_) {
        player->FixedUpdate(fixed_delta_time);
    }
}

Player* GameWorld::SpawnPlayer(const Math::Vector3& position) {
    auto player = std::make_shared<Player>();
    player->SetPosition(position);
    players_.push_back(player);

    if (!local_player_) {
        local_player_ = player.get();
    }

    if (game_mode_) {
        game_mode_->OnPlayerSpawn(player.get());
    }

    return player.get();
}

void GameWorld::RemovePlayer(Player* player) {
    players_.erase(
        std::remove_if(players_.begin(), players_.end(),
            [player](const SharedPtr<Player>& p) { return p.get() == player; }),
        players_.end()
    );

    if (local_player_ == player) {
        local_player_ = players_.empty() ? nullptr : players_[0].get();
    }
}

Enemy* GameWorld::SpawnEnemy(const EnemyConfig& config, const Math::Vector3& position) {
    auto enemy = std::make_shared<Enemy>(config);
    enemy->SetPosition(position);

    // Set target to local player
    if (local_player_) {
        enemy->SetTarget(local_player_);
    }

    enemies_.push_back(enemy);

    if (game_mode_) {
        game_mode_->OnEnemySpawn(enemy.get());
    }

    return enemy.get();
}

void GameWorld::RemoveEnemy(Enemy* enemy) {
    enemies_.erase(
        std::remove_if(enemies_.begin(), enemies_.end(),
            [enemy](const SharedPtr<Enemy>& e) { return e.get() == enemy; }),
        enemies_.end()
    );
}

void GameWorld::AddSpawnPoint(const SpawnPoint& point) {
    spawn_points_.push_back(point);
}

SpawnPoint GameWorld::GetRandomSpawnPoint(bool player_spawn) const {
    Array<const SpawnPoint*> valid_points;
    for (const auto& point : spawn_points_) {
        if ((player_spawn && point.is_player_spawn) ||
            (!player_spawn && point.is_enemy_spawn)) {
            valid_points.push_back(&point);
        }
    }

    if (valid_points.empty()) {
        return SpawnPoint{};
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dis(0, valid_points.size() - 1);

    return *valid_points[dis(gen)];
}

void GameWorld::SetGameMode(SharedPtr<GameMode> mode) {
    game_mode_ = mode;
    if (game_mode_) {
        game_mode_->Initialize();
    }
}

void GameWorld::SpawnProjectile(const Math::Vector3& position, const Math::Vector3& direction,
                                 float32 speed, float32 damage, void* owner) {
    ActiveProjectile proj;
    proj.position = position;
    proj.velocity = direction * speed;
    proj.damage = damage;
    proj.owner = owner;
    proj.lifetime = 5.0f;

    projectiles_.push_back(proj);
}

} // namespace DRX::Game

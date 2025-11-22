/**
 * DRX Engine - FPS Game Framework
 * Base classes for FPS game development
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Transform.h"
#include "Input/Public/InputSystem.h"
#include "Physics/Public/PhysicsWorld.h"
#include "Audio/Public/AudioSystem.h"

namespace DRX::Game {

// Forward declarations
class Player;
class Weapon;
class Enemy;
class Projectile;

/**
 * Damage types
 */
enum class DamageType : uint32 {
    Generic,
    Bullet,
    Explosion,
    Melee,
    Fire,
    Electric,
    Poison,
    Fall
};

/**
 * Damage info
 */
struct DamageInfo {
    float32 amount = 0;
    DamageType type = DamageType::Generic;
    Math::Vector3 origin;
    Math::Vector3 direction;
    void* instigator = nullptr;  // Who caused the damage
    void* weapon = nullptr;      // What weapon was used

    float32 impulse = 0;         // Knockback force
    bool is_critical = false;
};

/**
 * Health component
 */
class HealthComponent {
public:
    HealthComponent(float32 max_health = 100.0f);

    void TakeDamage(const DamageInfo& damage);
    void Heal(float32 amount);
    void SetHealth(float32 health);
    void SetMaxHealth(float32 max_health);

    float32 GetHealth() const { return health_; }
    float32 GetMaxHealth() const { return max_health_; }
    float32 GetHealthPercent() const { return health_ / max_health_; }
    bool IsAlive() const { return health_ > 0; }
    bool IsDead() const { return health_ <= 0; }

    // Callbacks
    using DamageCallback = std::function<void(const DamageInfo&)>;
    using DeathCallback = std::function<void(const DamageInfo&)>;
    using HealCallback = std::function<void(float32)>;

    void SetOnDamage(DamageCallback callback) { on_damage_ = callback; }
    void SetOnDeath(DeathCallback callback) { on_death_ = callback; }
    void SetOnHeal(HealCallback callback) { on_heal_ = callback; }

    // Modifiers
    float32 damage_multiplier = 1.0f;
    bool invulnerable = false;

private:
    float32 health_;
    float32 max_health_;

    DamageCallback on_damage_;
    DeathCallback on_death_;
    HealCallback on_heal_;
};

/**
 * Weapon slot
 */
enum class WeaponSlot : uint32 {
    Primary = 0,
    Secondary = 1,
    Melee = 2,
    Grenade = 3,
    Special = 4,
    MaxSlots = 5
};

/**
 * Weapon configuration
 */
struct WeaponConfig {
    String name;
    WeaponSlot slot = WeaponSlot::Primary;

    // Stats
    float32 damage = 25.0f;
    float32 fire_rate = 10.0f;  // Rounds per second
    float32 range = 100.0f;
    float32 spread = 1.0f;      // Degrees
    float32 recoil = 5.0f;

    // Ammo
    int32 magazine_size = 30;
    int32 max_ammo = 120;
    float32 reload_time = 2.0f;

    // Projectile (for non-hitscan weapons)
    bool is_hitscan = true;
    float32 projectile_speed = 100.0f;

    // Audio
    String fire_sound;
    String reload_sound;
    String empty_sound;

    // Visual
    String model_path;
    Math::Vector3 aim_offset;
    Math::Vector3 hip_offset;
    float32 aim_fov = 45.0f;
};

/**
 * Weapon state
 */
enum class WeaponState : uint32 {
    Idle,
    Firing,
    Reloading,
    Equipping,
    Unequipping
};

/**
 * Base Weapon class
 */
class Weapon {
public:
    Weapon(const WeaponConfig& config);
    virtual ~Weapon() = default;

    virtual void Update(float32 delta_time);

    // Actions
    virtual bool Fire();
    virtual void StopFiring();
    virtual void Reload();
    virtual void Equip();
    virtual void Unequip();

    // Ammo
    void AddAmmo(int32 amount);
    bool HasAmmo() const { return ammo_in_magazine_ > 0 || reserve_ammo_ > 0; }

    // State
    WeaponState GetState() const { return state_; }
    bool IsReady() const { return state_ == WeaponState::Idle; }
    bool IsReloading() const { return state_ == WeaponState::Reloading; }

    // Config
    const WeaponConfig& GetConfig() const { return config_; }

    // Ammo info
    int32 GetAmmoInMagazine() const { return ammo_in_magazine_; }
    int32 GetReserveAmmo() const { return reserve_ammo_; }

    // Owner
    void SetOwner(Player* owner) { owner_ = owner; }
    Player* GetOwner() const { return owner_; }

protected:
    virtual void PerformFire();
    virtual void ApplyRecoil();

    WeaponConfig config_;
    WeaponState state_ = WeaponState::Idle;

    int32 ammo_in_magazine_;
    int32 reserve_ammo_;

    float32 fire_timer_ = 0;
    float32 reload_timer_ = 0;
    float32 equip_timer_ = 0;

    bool trigger_held_ = false;
    Player* owner_ = nullptr;
};

/**
 * Player movement state
 */
enum class MovementState : uint32 {
    Walking,
    Running,
    Crouching,
    Sliding,
    Jumping,
    Falling
};

/**
 * Player configuration
 */
struct PlayerConfig {
    // Movement
    float32 walk_speed = 5.0f;
    float32 run_speed = 8.0f;
    float32 crouch_speed = 2.5f;
    float32 jump_force = 8.0f;
    float32 air_control = 0.3f;

    // Camera
    float32 standing_height = 1.8f;
    float32 crouch_height = 1.0f;
    float32 camera_height_offset = -0.1f;

    // Health
    float32 max_health = 100.0f;
    float32 max_armor = 100.0f;
    float32 health_regen_rate = 0.0f;
    float32 health_regen_delay = 5.0f;

    // Look
    float32 look_sensitivity = 1.0f;
    float32 max_pitch = 89.0f;
    bool invert_y = false;
};

/**
 * FPS Player class
 */
class Player {
public:
    Player(const PlayerConfig& config = {});
    virtual ~Player();

    virtual void Update(float32 delta_time);
    virtual void FixedUpdate(float32 fixed_delta_time);

    // Input
    void SetMoveInput(const Math::Vector2& input) { move_input_ = input; }
    void SetLookInput(const Math::Vector2& input) { look_input_ = input; }
    void SetJumpInput(bool pressed) { jump_pressed_ = pressed; }
    void SetCrouchInput(bool pressed) { crouch_pressed_ = pressed; }
    void SetSprintInput(bool pressed) { sprint_pressed_ = pressed; }
    void SetFireInput(bool pressed);
    void SetAimInput(bool pressed) { aim_pressed_ = pressed; }
    void SetReloadInput(bool pressed);
    void SetWeaponSwitchInput(int32 slot);

    // Movement
    MovementState GetMovementState() const { return movement_state_; }
    Math::Vector3 GetVelocity() const { return velocity_; }
    bool IsGrounded() const { return is_grounded_; }

    // Transform
    Math::Vector3 GetPosition() const { return position_; }
    void SetPosition(const Math::Vector3& position);
    Math::Quaternion GetRotation() const;
    float32 GetYaw() const { return yaw_; }
    float32 GetPitch() const { return pitch_; }

    // Camera
    Math::Vector3 GetCameraPosition() const;
    Math::Vector3 GetAimDirection() const;
    float32 GetCurrentFOV() const;

    // Weapons
    void GiveWeapon(SharedPtr<Weapon> weapon);
    void RemoveWeapon(WeaponSlot slot);
    Weapon* GetCurrentWeapon() const { return current_weapon_; }
    Weapon* GetWeapon(WeaponSlot slot) const;
    void SwitchWeapon(WeaponSlot slot);

    // Health
    HealthComponent& GetHealth() { return health_; }
    float32 GetArmor() const { return armor_; }
    void SetArmor(float32 armor) { armor_ = std::min(armor, config_.max_armor); }
    void AddArmor(float32 amount);

    // Damage
    void TakeDamage(const DamageInfo& damage);

    // Config
    const PlayerConfig& GetConfig() const { return config_; }

protected:
    virtual void UpdateMovement(float32 delta_time);
    virtual void UpdateLook(float32 delta_time);
    virtual void UpdateWeapons(float32 delta_time);
    virtual void CheckGround();
    virtual void ApplyGravity(float32 delta_time);
    virtual void Jump();

    PlayerConfig config_;

    // Transform
    Math::Vector3 position_;
    float32 yaw_ = 0;
    float32 pitch_ = 0;

    // Movement
    Math::Vector3 velocity_;
    MovementState movement_state_ = MovementState::Walking;
    bool is_grounded_ = false;
    float32 current_height_;

    // Input
    Math::Vector2 move_input_;
    Math::Vector2 look_input_;
    bool jump_pressed_ = false;
    bool crouch_pressed_ = false;
    bool sprint_pressed_ = false;
    bool aim_pressed_ = false;

    // Weapons
    Array<SharedPtr<Weapon>> weapons_;
    Weapon* current_weapon_ = nullptr;
    WeaponSlot current_slot_ = WeaponSlot::Primary;

    // Health
    HealthComponent health_;
    float32 armor_ = 0;
    float32 health_regen_timer_ = 0;

    // Physics
    Physics::CharacterController* character_controller_ = nullptr;
};

/**
 * Enemy AI state
 */
enum class EnemyAIState : uint32 {
    Idle,
    Patrol,
    Chase,
    Attack,
    Search,
    Flee,
    Dead
};

/**
 * Enemy configuration
 */
struct EnemyConfig {
    String name;

    // Stats
    float32 max_health = 100.0f;
    float32 damage = 20.0f;
    float32 attack_range = 2.0f;
    float32 attack_rate = 1.0f;

    // Movement
    float32 walk_speed = 3.0f;
    float32 run_speed = 6.0f;

    // AI
    float32 sight_range = 30.0f;
    float32 sight_angle = 90.0f;
    float32 hearing_range = 15.0f;
    float32 search_duration = 10.0f;

    // Rewards
    int32 score_value = 100;
    int32 xp_value = 50;
};

/**
 * Base Enemy class
 */
class Enemy {
public:
    Enemy(const EnemyConfig& config);
    virtual ~Enemy();

    virtual void Update(float32 delta_time);

    // AI
    EnemyAIState GetAIState() const { return ai_state_; }
    void SetTarget(Player* target) { target_ = target; }
    Player* GetTarget() const { return target_; }

    // Transform
    Math::Vector3 GetPosition() const { return position_; }
    void SetPosition(const Math::Vector3& position) { position_ = position; }
    Math::Quaternion GetRotation() const { return rotation_; }

    // Health
    HealthComponent& GetHealth() { return health_; }
    void TakeDamage(const DamageInfo& damage);

    // Config
    const EnemyConfig& GetConfig() const { return config_; }

protected:
    virtual void UpdateAI(float32 delta_time);
    virtual void UpdateIdle(float32 delta_time);
    virtual void UpdatePatrol(float32 delta_time);
    virtual void UpdateChase(float32 delta_time);
    virtual void UpdateAttack(float32 delta_time);
    virtual void UpdateSearch(float32 delta_time);
    virtual void UpdateFlee(float32 delta_time);

    virtual bool CanSeeTarget() const;
    virtual bool CanHearTarget() const;
    virtual void Attack();
    virtual void Die(const DamageInfo& damage);

    EnemyConfig config_;
    EnemyAIState ai_state_ = EnemyAIState::Idle;

    Math::Vector3 position_;
    Math::Quaternion rotation_;
    Math::Vector3 velocity_;

    Player* target_ = nullptr;
    Math::Vector3 last_known_target_pos_;

    HealthComponent health_;

    float32 attack_timer_ = 0;
    float32 search_timer_ = 0;

    // Patrol
    Array<Math::Vector3> patrol_points_;
    uint32 current_patrol_index_ = 0;
};

/**
 * Game Mode base class
 */
class GameMode {
public:
    GameMode();
    virtual ~GameMode() = default;

    virtual void Initialize();
    virtual void Shutdown();
    virtual void Update(float32 delta_time);

    // Match flow
    virtual void StartMatch();
    virtual void EndMatch();
    virtual void RestartMatch();

    // Player management
    virtual void OnPlayerSpawn(Player* player);
    virtual void OnPlayerDeath(Player* player, const DamageInfo& damage);
    virtual void OnPlayerKill(Player* killer, Player* victim);

    // Enemy management
    virtual void OnEnemySpawn(Enemy* enemy);
    virtual void OnEnemyDeath(Enemy* enemy, const DamageInfo& damage);

    // Scoring
    void AddScore(int32 amount);
    int32 GetScore() const { return score_; }

    // Time
    float32 GetMatchTime() const { return match_time_; }
    float32 GetTimeLimit() const { return time_limit_; }
    void SetTimeLimit(float32 limit) { time_limit_ = limit; }

    // State
    bool IsMatchActive() const { return match_active_; }

protected:
    bool match_active_ = false;
    float32 match_time_ = 0;
    float32 time_limit_ = 0;  // 0 = no limit
    int32 score_ = 0;

    Array<Player*> players_;
    Array<Enemy*> enemies_;
};

/**
 * Spawn point
 */
struct SpawnPoint {
    Math::Vector3 position;
    Math::Quaternion rotation;
    String tag;
    bool is_player_spawn = false;
    bool is_enemy_spawn = false;
};

/**
 * Game World - manages game entities
 */
class GameWorld {
public:
    static GameWorld& Instance();

    bool Initialize();
    void Shutdown();
    void Update(float32 delta_time);
    void FixedUpdate(float32 fixed_delta_time);

    // Player
    Player* SpawnPlayer(const Math::Vector3& position);
    void RemovePlayer(Player* player);
    Player* GetLocalPlayer() const { return local_player_; }

    // Enemies
    Enemy* SpawnEnemy(const EnemyConfig& config, const Math::Vector3& position);
    void RemoveEnemy(Enemy* enemy);
    const Array<SharedPtr<Enemy>>& GetEnemies() const { return enemies_; }

    // Spawn points
    void AddSpawnPoint(const SpawnPoint& point);
    SpawnPoint GetRandomSpawnPoint(bool player_spawn) const;

    // Game mode
    void SetGameMode(SharedPtr<GameMode> mode);
    GameMode* GetGameMode() const { return game_mode_.get(); }

    // Projectiles
    void SpawnProjectile(const Math::Vector3& position, const Math::Vector3& direction,
                         float32 speed, float32 damage, void* owner);

private:
    GameWorld() = default;

    Player* local_player_ = nullptr;
    Array<SharedPtr<Player>> players_;
    Array<SharedPtr<Enemy>> enemies_;
    Array<SpawnPoint> spawn_points_;
    SharedPtr<GameMode> game_mode_;

    // Projectile pool
    struct ActiveProjectile {
        Math::Vector3 position;
        Math::Vector3 velocity;
        float32 damage;
        void* owner;
        float32 lifetime;
    };
    Array<ActiveProjectile> projectiles_;
};

} // namespace DRX::Game

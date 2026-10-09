/**
 * DRX Engine - FPS Game Framework
 * Players, weapons, enemies and game modes on top of the physics character controller.
 *
 * Conventions: yaw/pitch in degrees, yaw 0 looks down -Z, positive pitch looks up.
 * Movement runs in FixedUpdate (deterministic), look/weapons/AI in Update.
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector2.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Transform.h"
#include "Physics/Public/PhysicsWorld.h"

#include <random>
#include <unordered_set>

namespace DRX::Game {

class Player;
class Weapon;
class Enemy;
class Damageable;

enum class DamageType : uint32 {
    Generic,
    Bullet,
    Explosion,
    Melee,
    Fire,
    Electric,
    Poison,
    Fall,
    Crush
};

struct DamageInfo {
    float32 amount = 0.0f;
    DamageType type = DamageType::Generic;
    Math::Vector3 origin;
    Math::Vector3 direction;
    Math::Vector3 hit_point;
    Damageable* instigator = nullptr;
    Weapon* weapon = nullptr;
    float32 impulse = 0.0f;
    bool is_critical = false;   // e.g. headshot; the multiplier is already in `amount`
};

class HealthComponent {
public:
    explicit HealthComponent(float32 max_health = 100.0f);

    // Returns the damage actually applied
    float32 TakeDamage(const DamageInfo& damage);
    void Heal(float32 amount);
    void SetHealth(float32 health);
    void SetMaxHealth(float32 max_health);
    void Revive() { health_ = max_health_; }

    float32 GetHealth() const { return health_; }
    float32 GetMaxHealth() const { return max_health_; }
    float32 GetHealthPercent() const { return max_health_ > 0.0f ? health_ / max_health_ : 0.0f; }
    bool IsAlive() const { return health_ > 0.0f; }
    bool IsDead() const { return health_ <= 0.0f; }

    using DamageCallback = Function<void(const DamageInfo&)>;
    using DeathCallback = Function<void(const DamageInfo&)>;
    using HealCallback = Function<void(float32)>;
    void SetOnDamage(DamageCallback callback) { on_damage_ = std::move(callback); }
    void SetOnDeath(DeathCallback callback) { on_death_ = std::move(callback); }
    void SetOnHeal(HealCallback callback) { on_heal_ = std::move(callback); }

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
 * Anything that can be hit and damaged (players, enemies, destructibles)
 */
class Damageable {
public:
    virtual ~Damageable() = default;
    virtual void TakeDamage(const DamageInfo& damage) = 0;
    virtual bool IsAlive() const = 0;
    // Height above the feet where critical hits start (0 = no critical zone)
    virtual float32 GetCriticalHeight() const { return 0.0f; }
    virtual Math::Vector3 GetPosition() const = 0;
};

enum class WeaponSlot : uint32 {
    Primary = 0,
    Secondary = 1,
    Melee = 2,
    Grenade = 3,
    Special = 4,
    MaxSlots = 5
};

struct WeaponConfig {
    String name;
    WeaponSlot slot = WeaponSlot::Primary;

    float32 damage = 25.0f;
    float32 critical_multiplier = 2.0f;   // headshots
    float32 fire_rate = 10.0f;            // shots per second
    bool automatic = true;                // false: one shot per trigger pull
    float32 range = 100.0f;
    float32 spread = 1.0f;                // cone half-angle in degrees
    float32 recoil = 1.0f;                // degrees of pitch per shot

    int32 magazine_size = 30;
    int32 max_ammo = 120;
    float32 reload_time = 2.0f;
    float32 equip_time = 0.4f;

    bool is_hitscan = true;
    float32 projectile_speed = 100.0f;
    float32 projectile_gravity = 0.0f;

    float32 aim_fov = 45.0f;
    uint32 seed = 1;                      // spread/recoil randomness (deterministic)
};

enum class WeaponState : uint32 {
    Idle,
    Firing,
    Reloading,
    Equipping
};

struct ShotResult {
    bool fired = false;
    bool hit = false;
    Math::Vector3 origin;
    Math::Vector3 end;           // hit point or end of range
    Math::Vector3 normal;
    Damageable* target = nullptr;
    bool critical = false;
};

class Weapon {
public:
    explicit Weapon(const WeaponConfig& config);
    virtual ~Weapon() = default;

    virtual void Update(float32 delta_time);

    // Trigger handling: Fire() each frame while held, StopFiring() on release
    virtual bool Fire();
    virtual void StopFiring();
    virtual void Reload();
    virtual void Equip();

    void AddAmmo(int32 amount);
    bool HasAmmo() const { return ammo_in_magazine_ > 0 || reserve_ammo_ > 0; }

    WeaponState GetState() const { return state_; }
    bool IsReady() const { return state_ == WeaponState::Idle; }
    bool IsReloading() const { return state_ == WeaponState::Reloading; }
    const WeaponConfig& GetConfig() const { return config_; }
    int32 GetAmmoInMagazine() const { return ammo_in_magazine_; }
    int32 GetReserveAmmo() const { return reserve_ammo_; }
    const ShotResult& GetLastShot() const { return last_shot_; }
    uint32 GetShotCount() const { return shot_count_; }

    void SetOwner(Player* owner) { owner_ = owner; }
    Player* GetOwner() const { return owner_; }

    // Called after every shot (sounds, muzzle flash, tracers)
    void SetOnShot(Function<void(const ShotResult&)> callback) { on_shot_ = std::move(callback); }
    // Called when the trigger is pulled with an empty magazine
    void SetOnEmpty(Function<void()> callback) { on_empty_ = std::move(callback); }

protected:
    virtual void PerformFire();
    Math::Vector3 ApplySpread(const Math::Vector3& direction);

    WeaponConfig config_;
    WeaponState state_ = WeaponState::Idle;
    int32 ammo_in_magazine_;
    int32 reserve_ammo_;
    float32 cooldown_ = 0.0f;
    float32 reload_timer_ = 0.0f;
    float32 equip_timer_ = 0.0f;
    bool trigger_held_ = false;
    bool trigger_released_ = true;
    uint32 shot_count_ = 0;
    ShotResult last_shot_;
    Player* owner_ = nullptr;
    std::mt19937 rng_;
    Function<void(const ShotResult&)> on_shot_;
    Function<void()> on_empty_;
};

enum class MovementState : uint32 {
    Walking,
    Running,
    Crouching,
    Jumping,
    Falling
};

struct PlayerConfig {
    float32 walk_speed = 5.5f;
    float32 run_speed = 8.0f;
    float32 crouch_speed = 2.5f;
    float32 jump_height = 1.0f;          // metres
    float32 gravity = 20.0f;
    float32 air_control = 0.3f;          // 0 = no steering in the air, 1 = full

    float32 radius = 0.35f;
    float32 standing_height = 1.8f;
    float32 eye_height = 1.65f;
    float32 crouch_eye_height = 1.0f;
    float32 critical_height = 1.5f;      // hits above this height (from the feet) are critical

    float32 max_health = 100.0f;
    float32 max_armor = 100.0f;
    float32 armor_absorption = 0.5f;     // fraction of damage taken by armor
    float32 health_regen_rate = 0.0f;
    float32 health_regen_delay = 5.0f;

    float32 look_sensitivity = 0.1f;     // degrees per input unit (mouse pixel)
    float32 max_pitch = 89.0f;
    bool invert_y = false;
};

class Player : public Damageable {
public:
    explicit Player(const PlayerConfig& config = {});
    ~Player() override;

    virtual void Update(float32 delta_time);
    virtual void FixedUpdate(float32 fixed_delta_time);

    // Input (move: x = right, y = forward; look: mouse delta, accumulated until the next Update)
    void SetMoveInput(const Math::Vector2& input) { move_input_ = input; }
    void AddLookInput(const Math::Vector2& input) { look_input_ += input; }
    void SetJumpInput(bool pressed) { jump_pressed_ = pressed; }
    void SetCrouchInput(bool pressed) { crouch_pressed_ = pressed; }
    void SetSprintInput(bool pressed) { sprint_pressed_ = pressed; }
    void SetFireInput(bool pressed);
    void SetAimInput(bool pressed) { aim_pressed_ = pressed; }
    void SetReloadInput(bool pressed);
    void SetWeaponSwitchInput(int32 slot);
    void AddRecoil(float32 pitch_degrees, float32 yaw_degrees);

    MovementState GetMovementState() const { return movement_state_; }
    Math::Vector3 GetVelocity() const;
    bool IsGrounded() const;

    Math::Vector3 GetPosition() const override;   // feet
    void SetPosition(const Math::Vector3& position);
    Math::Quaternion GetRotation() const;
    float32 GetYaw() const { return yaw_; }
    float32 GetPitch() const { return pitch_; }
    void SetLookAngles(float32 yaw, float32 pitch);

    Math::Vector3 GetCameraPosition() const;
    Math::Vector3 GetAimDirection() const;
    Math::Vector3 GetForward() const;   // horizontal
    Math::Vector3 GetRight() const;
    float32 GetCurrentFOV(float32 default_fov = 90.0f) const;

    void GiveWeapon(SharedPtr<Weapon> weapon);
    void RemoveWeapon(WeaponSlot slot);
    Weapon* GetCurrentWeapon() const { return current_weapon_; }
    Weapon* GetWeapon(WeaponSlot slot) const;
    void SwitchWeapon(WeaponSlot slot);

    HealthComponent& GetHealth() { return health_; }
    const HealthComponent& GetHealth() const { return health_; }
    float32 GetArmor() const { return armor_; }
    void SetArmor(float32 armor) { armor_ = std::clamp(armor, 0.0f, config_.max_armor); }
    void AddArmor(float32 amount) { SetArmor(armor_ + amount); }

    void TakeDamage(const DamageInfo& damage) override;
    bool IsAlive() const override { return health_.IsAlive(); }
    float32 GetCriticalHeight() const override { return config_.critical_height; }
    void Respawn(const Math::Vector3& position, float32 yaw);

    const PlayerConfig& GetConfig() const { return config_; }
    void AttachController(SharedPtr<Physics::CharacterController> controller);
    SharedPtr<Physics::CharacterController> DetachController();
    Physics::CharacterController* GetController() const { return controller_.get(); }

protected:
    virtual void UpdateLook();
    virtual void UpdateMovement(float32 dt);

    PlayerConfig config_;
    SharedPtr<Physics::CharacterController> controller_;
    Math::Vector3 fallback_position_;   // used without a controller
    float32 yaw_ = 0.0f;
    float32 pitch_ = 0.0f;
    MovementState movement_state_ = MovementState::Walking;
    float32 eye_height_;

    Math::Vector2 move_input_;
    Math::Vector2 look_input_;
    bool jump_pressed_ = false;
    bool crouch_pressed_ = false;
    bool sprint_pressed_ = false;
    bool aim_pressed_ = false;

    Array<SharedPtr<Weapon>> weapons_;
    Weapon* current_weapon_ = nullptr;

    HealthComponent health_;
    float32 armor_ = 0.0f;
    float32 health_regen_timer_ = 0.0f;
};

enum class EnemyAIState : uint32 {
    Idle,
    Patrol,
    Chase,
    Attack,
    Search,
    Flee,
    Dead
};

struct EnemyConfig {
    String name;
    float32 max_health = 100.0f;
    float32 damage = 20.0f;
    float32 attack_range = 2.0f;
    float32 attack_rate = 1.0f;
    float32 walk_speed = 3.0f;
    float32 run_speed = 6.0f;
    float32 radius = 0.35f;
    float32 height = 1.8f;
    float32 critical_height = 1.5f;
    float32 sight_range = 30.0f;
    float32 sight_angle = 90.0f;
    float32 hearing_range = 15.0f;
    float32 search_duration = 10.0f;
    float32 flee_health_fraction = 0.0f;   // flees below this fraction of health (0 = never)
    int32 score_value = 100;
};

class Enemy : public Damageable {
public:
    explicit Enemy(const EnemyConfig& config);
    ~Enemy() override;

    virtual void Update(float32 delta_time);
    virtual void FixedUpdate(float32 fixed_delta_time);

    EnemyAIState GetAIState() const { return ai_state_; }
    void SetTarget(Player* target) { target_ = target; }
    Player* GetTarget() const { return target_; }
    void SetPatrolPoints(Array<Math::Vector3> points) { patrol_points_ = std::move(points); }

    Math::Vector3 GetPosition() const override;
    void SetPosition(const Math::Vector3& position);
    Math::Quaternion GetRotation() const { return rotation_; }

    HealthComponent& GetHealth() { return health_; }
    void TakeDamage(const DamageInfo& damage) override;
    bool IsAlive() const override { return health_.IsAlive(); }
    float32 GetCriticalHeight() const override { return config_.critical_height; }

    const EnemyConfig& GetConfig() const { return config_; }
    void AttachController(SharedPtr<Physics::CharacterController> controller);
    SharedPtr<Physics::CharacterController> DetachController();
    Physics::CharacterController* GetController() const { return controller_.get(); }

protected:
    virtual void UpdateAI(float32 delta_time);
    virtual void UpdateIdle();
    virtual void UpdatePatrol();
    virtual void UpdateChase();
    virtual void UpdateAttack();
    virtual void UpdateSearch(float32 delta_time);
    virtual void UpdateFlee();
    virtual bool CanSeeTarget() const;
    virtual bool CanHearTarget() const;
    virtual void Attack();
    virtual void Die(const DamageInfo& damage);
    void MoveTowards(const Math::Vector3& point, float32 speed);
    void FaceDirection(const Math::Vector3& direction);

    EnemyConfig config_;
    EnemyAIState ai_state_ = EnemyAIState::Idle;
    SharedPtr<Physics::CharacterController> controller_;
    Math::Vector3 fallback_position_;
    Math::Quaternion rotation_;
    Math::Vector3 wish_velocity_;
    Player* target_ = nullptr;
    Math::Vector3 last_known_target_pos_;
    HealthComponent health_;
    float32 attack_timer_ = 0.0f;
    float32 search_timer_ = 0.0f;
    Array<Math::Vector3> patrol_points_;
    uint32 current_patrol_index_ = 0;
};

class GameMode {
public:
    GameMode() = default;
    virtual ~GameMode() = default;

    virtual void Initialize();
    virtual void Shutdown();
    virtual void Update(float32 delta_time);

    virtual void StartMatch();
    virtual void EndMatch();
    virtual void RestartMatch();

    virtual void OnPlayerSpawn(Player* player);
    virtual void OnPlayerDeath(Player* player, const DamageInfo& damage);
    virtual void OnPlayerKill(Player* killer, Player* victim);
    virtual void OnEnemySpawn(Enemy* enemy);
    virtual void OnEnemyDeath(Enemy* enemy, const DamageInfo& damage);

    void AddScore(int32 amount) { score_ += amount; }
    int32 GetScore() const { return score_; }
    float32 GetMatchTime() const { return match_time_; }
    float32 GetTimeLimit() const { return time_limit_; }
    void SetTimeLimit(float32 limit) { time_limit_ = limit; }
    bool IsMatchActive() const { return match_active_; }

protected:
    bool match_active_ = false;
    float32 match_time_ = 0.0f;
    float32 time_limit_ = 0.0f;   // 0 = no limit
    int32 score_ = 0;
    Array<Player*> players_;
    Array<Enemy*> enemies_;
};

struct SpawnPoint {
    Math::Vector3 position;
    float32 yaw = 0.0f;
    String tag;
    bool is_player_spawn = true;
    bool is_enemy_spawn = false;
};

/**
 * Game World - owns the physics world and all game entities
 */
class GameWorld {
public:
    static GameWorld& Instance();

    bool Initialize(const Math::Vector3& gravity = {0, -9.81f, 0});
    void Shutdown();
    bool IsInitialized() const { return physics_.IsInitialized(); }

    void Update(float32 delta_time);
    void FixedUpdate(float32 fixed_delta_time);

    Physics::PhysicsWorld& GetPhysics() { return physics_; }

    // Static level geometry (axis aligned box, size = full extents)
    SharedPtr<Physics::RigidBody> AddStaticBox(const Math::Vector3& center, const Math::Vector3& size,
                                               const Math::Quaternion& rotation = {});

    Player* SpawnPlayer(const Math::Vector3& position, const PlayerConfig& config = {}, float32 yaw = 0.0f);
    void RemovePlayer(Player* player);
    Player* GetLocalPlayer() const { return local_player_; }
    const Array<SharedPtr<Player>>& GetPlayers() const { return players_; }

    Enemy* SpawnEnemy(const EnemyConfig& config, const Math::Vector3& position);
    void RemoveEnemy(Enemy* enemy);
    const Array<SharedPtr<Enemy>>& GetEnemies() const { return enemies_; }

    void AddSpawnPoint(const SpawnPoint& point);
    const Array<SpawnPoint>& GetSpawnPoints() const { return spawn_points_; }
    // Returns false when no spawn point of the requested kind exists
    bool GetRandomSpawnPoint(bool player_spawn, SpawnPoint& out);
    void SetSeed(uint32 seed) { rng_.seed(seed); }

    void SetGameMode(SharedPtr<GameMode> mode);
    GameMode* GetGameMode() const { return game_mode_.get(); }

    /**
     * Hitscan: traces from origin along direction, damages the first Damageable hit
     * (critical above its critical height). `ignore` is never hit (the shooter).
     */
    ShotResult TraceShot(const Math::Vector3& origin, const Math::Vector3& direction, float32 range,
                         DamageInfo damage, float32 critical_multiplier, Damageable* ignore);

    void SpawnProjectile(const Math::Vector3& position, const Math::Vector3& direction, float32 speed,
                         const DamageInfo& damage, Damageable* owner, float32 gravity = 0.0f,
                         float32 lifetime = 5.0f);
    struct Projectile {
        Math::Vector3 position;
        Math::Vector3 velocity;
        DamageInfo damage;
        Damageable* owner = nullptr;
        float32 gravity = 0.0f;
        float32 lifetime = 0.0f;
    };
    const Array<Projectile>& GetProjectiles() const { return projectiles_; }
    void SetOnProjectileImpact(Function<void(const Projectile&, const Physics::RaycastResult&)> callback) {
        on_projectile_impact_ = std::move(callback);
    }

    // Damageables registered with the world (physics user data is checked against this set)
    Damageable* FindDamageable(void* user_data) const;

private:
    GameWorld() = default;
    SharedPtr<Physics::CharacterController> CreateController(Damageable* owner, float32 radius, float32 height,
                                                             float32 gravity, const Math::Vector3& feet);
    void UpdateProjectiles(float32 dt);

    Physics::PhysicsWorld physics_;
    Player* local_player_ = nullptr;
    Array<SharedPtr<Player>> players_;
    Array<SharedPtr<Enemy>> enemies_;
    Array<SpawnPoint> spawn_points_;
    Array<SharedPtr<Physics::RigidBody>> static_bodies_;
    SharedPtr<GameMode> game_mode_;
    Array<Projectile> projectiles_;
    std::unordered_set<Damageable*> damageables_;
    std::mt19937 rng_{1};
    Function<void(const Projectile&, const Physics::RaycastResult&)> on_projectile_impact_;
};

} // namespace DRX::Game

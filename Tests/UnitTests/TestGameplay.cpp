/**
 * DRX Engine - FPS framework tests (movement, weapons, damage, AI line of sight)
 */

#include <gtest/gtest.h>
#include "Game/Public/FPSFramework.h"

using namespace DRX;
using namespace DRX::Game;
using Math::Vector3;

namespace {

constexpr float32 kDt = 1.0f / 60.0f;

class GameplayTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(world.Initialize());
        world.AddStaticBox({0, -0.5f, 0}, {60, 1, 60});
    }
    void TearDown() override { world.Shutdown(); }

    void Run(float32 seconds) {
        const int steps = static_cast<int>(std::lround(seconds / kDt));
        for (int i = 0; i < steps; ++i) {
            world.FixedUpdate(kDt);
            world.Update(kDt);
        }
    }

    static WeaponConfig Rifle() {
        WeaponConfig config;
        config.damage = 20.0f;
        config.critical_multiplier = 2.0f;
        config.fire_rate = 10.0f;
        config.spread = 0.0f;
        config.recoil = 0.0f;
        config.magazine_size = 5;
        config.max_ammo = 10;
        config.reload_time = 1.0f;
        config.equip_time = 0.0f;
        return config;
    }

    GameWorld& world = GameWorld::Instance();
};

struct CountingMode : GameMode {
    int player_deaths = 0, kills = 0, enemy_deaths = 0;
    void OnPlayerDeath(Player*, const DamageInfo&) override { ++player_deaths; }
    void OnPlayerKill(Player*, Player*) override { ++kills; }
    void OnEnemyDeath(Enemy* enemy, const DamageInfo& damage) override {
        ++enemy_deaths;
        GameMode::OnEnemyDeath(enemy, damage);
    }
};

} // namespace

TEST_F(GameplayTest, PlayerMovesForwardAndCollides) {
    world.AddStaticBox({0, 1.5f, -6}, {10, 3, 1});   // wall face at z = -5.5
    Player* player = world.SpawnPlayer({0, 0, 0});
    ASSERT_NE(player->GetController(), nullptr);
    EXPECT_NEAR(player->GetForward().z, -1.0f, 1e-5f);   // yaw 0 looks down -Z

    player->SetMoveInput({0, 1});
    Run(2.0f);
    EXPECT_NEAR(player->GetPosition().z, -5.5f + player->GetConfig().radius, 0.05f);
    EXPECT_TRUE(player->IsGrounded());

    player->SetMoveInput({0, 0});
    player->SetJumpInput(true);
    float32 apex = 0.0f;
    for (int i = 0; i < 60; ++i) {
        world.FixedUpdate(kDt);
        player->SetJumpInput(false);
        apex = std::max(apex, player->GetPosition().y);
    }
    EXPECT_NEAR(apex, player->GetConfig().jump_height, 0.06f);
}

TEST_F(GameplayTest, LookInputTurnsAndClampsPitch) {
    Player* player = world.SpawnPlayer({0, 0, 0});
    player->AddLookInput({900.0f, 0.0f});   // 0.1 deg per unit -> 90 degrees right
    player->Update(kDt);
    EXPECT_NEAR(player->GetForward().x, 1.0f, 1e-4f);
    player->AddLookInput({0.0f, -5000.0f});   // mouse up -> look up, clamped
    player->Update(kDt);
    EXPECT_FLOAT_EQ(player->GetPitch(), player->GetConfig().max_pitch);
    EXPECT_GT(player->GetAimDirection().y, 0.99f);
}

TEST_F(GameplayTest, HitscanDamagesAndHeadshotsAreCritical) {
    auto mode = MakeShared<CountingMode>();
    world.SetGameMode(mode);

    Player* shooter = world.SpawnPlayer({0, 0, 0});
    shooter->GiveWeapon(MakeShared<Weapon>(Rifle()));
    EnemyConfig enemy_config;
    enemy_config.max_health = 100.0f;
    enemy_config.sight_range = 0.0f;   // passive target
    enemy_config.hearing_range = 0.0f;
    Enemy* enemy = world.SpawnEnemy(enemy_config, {0, 0, -10});
    Run(0.1f);
    world.Update(1.0f);   // finish equipping

    // Body shot: aim at chest height
    const Vector3 eye = shooter->GetCameraPosition();
    auto aim_at = [&](const Vector3& target) {
        const Vector3 d = (target - eye).Normalized();
        shooter->SetLookAngles(std::atan2(d.x, -d.z) * 57.2957795f, std::asin(d.y) * 57.2957795f);
    };
    aim_at({0, 1.0f, -10});
    shooter->SetFireInput(true);
    shooter->SetFireInput(false);
    Weapon* weapon = shooter->GetCurrentWeapon();
    ASSERT_TRUE(weapon->GetLastShot().hit);
    EXPECT_EQ(weapon->GetLastShot().target, enemy);
    EXPECT_FALSE(weapon->GetLastShot().critical);
    EXPECT_FLOAT_EQ(enemy->GetHealth().GetHealth(), 80.0f);

    // Head shot doubles damage
    world.Update(0.2f);
    aim_at({0, 1.7f, -10});
    shooter->SetFireInput(true);
    shooter->SetFireInput(false);
    EXPECT_TRUE(weapon->GetLastShot().critical);
    EXPECT_FLOAT_EQ(enemy->GetHealth().GetHealth(), 40.0f);

    // Kill
    world.Update(0.2f);
    aim_at({0, 1.7f, -10});
    shooter->SetFireInput(true);
    shooter->SetFireInput(false);
    EXPECT_FALSE(enemy->IsAlive());
    EXPECT_EQ(enemy->GetAIState(), EnemyAIState::Dead);
    EXPECT_EQ(mode->enemy_deaths, 1);
    EXPECT_EQ(mode->GetScore(), enemy_config.score_value);
}

TEST_F(GameplayTest, WallsBlockShotsAndShooterCannotHitItself) {
    Player* shooter = world.SpawnPlayer({0, 0, 0});
    shooter->GiveWeapon(MakeShared<Weapon>(Rifle()));
    Enemy* enemy = world.SpawnEnemy({}, {0, 0, -10});
    world.AddStaticBox({0, 1.5f, -5}, {4, 3, 0.5f});
    world.Update(1.0f);

    shooter->SetLookAngles(0.0f, -5.0f);
    shooter->SetFireInput(true);
    shooter->SetFireInput(false);
    const ShotResult& shot = shooter->GetCurrentWeapon()->GetLastShot();
    EXPECT_TRUE(shot.hit);
    EXPECT_EQ(shot.target, nullptr);
    EXPECT_NEAR(shot.end.z, -4.75f, 0.01f);
    EXPECT_FLOAT_EQ(enemy->GetHealth().GetHealth(), enemy->GetHealth().GetMaxHealth());
    EXPECT_FLOAT_EQ(shooter->GetHealth().GetHealth(), shooter->GetHealth().GetMaxHealth());
}

TEST_F(GameplayTest, AutomaticFireRateMagazineAndReload) {
    Player* shooter = world.SpawnPlayer({0, 0, 0});
    shooter->GiveWeapon(MakeShared<Weapon>(Rifle()));
    world.Update(1.0f);
    Weapon* weapon = shooter->GetCurrentWeapon();

    shooter->SetFireInput(true);
    for (int i = 0; i < 30; ++i) world.Update(kDt);   // 0.5 s at 10 shots/s, magazine of 5
    EXPECT_EQ(weapon->GetShotCount(), 5u);
    EXPECT_EQ(weapon->GetAmmoInMagazine(), 0);
    shooter->SetFireInput(false);

    bool empty_clicked = false;
    weapon->SetOnEmpty([&] { empty_clicked = true; });
    shooter->SetFireInput(true);   // empty: triggers reload
    shooter->SetFireInput(false);
    EXPECT_TRUE(empty_clicked);
    EXPECT_TRUE(weapon->IsReloading());
    world.Update(1.1f);
    EXPECT_EQ(weapon->GetAmmoInMagazine(), 5);
    EXPECT_EQ(weapon->GetReserveAmmo(), 5);

    // Semi-automatic: one shot per trigger pull
    WeaponConfig pistol = Rifle();
    pistol.automatic = false;
    pistol.slot = WeaponSlot::Secondary;
    shooter->GiveWeapon(MakeShared<Weapon>(pistol));
    shooter->SwitchWeapon(WeaponSlot::Secondary);
    world.Update(0.5f);
    Weapon* semi = shooter->GetCurrentWeapon();
    shooter->SetFireInput(true);
    for (int i = 0; i < 60; ++i) {
        shooter->SetFireInput(true);
        world.Update(kDt);
    }
    EXPECT_EQ(semi->GetShotCount(), 1u);
}

TEST_F(GameplayTest, ProjectilesHitTargets) {
    Player* shooter = world.SpawnPlayer({0, 0, 0});
    WeaponConfig launcher = Rifle();
    launcher.is_hitscan = false;
    launcher.projectile_speed = 40.0f;
    shooter->GiveWeapon(MakeShared<Weapon>(launcher));
    Enemy* enemy = world.SpawnEnemy({}, {0, 0, -12});
    world.Update(1.0f);

    int impacts = 0;
    world.SetOnProjectileImpact([&](const GameWorld::Projectile&, const Physics::RaycastResult&) { ++impacts; });
    shooter->SetLookAngles(0.0f, -2.0f);
    shooter->SetFireInput(true);
    shooter->SetFireInput(false);
    EXPECT_EQ(world.GetProjectiles().size(), 1u);
    Run(0.5f);
    EXPECT_TRUE(world.GetProjectiles().empty());
    EXPECT_EQ(impacts, 1);
    EXPECT_FLOAT_EQ(enemy->GetHealth().GetHealth(), enemy->GetConfig().max_health - launcher.damage);
    world.SetOnProjectileImpact(nullptr);
}

TEST_F(GameplayTest, EnemyNeedsLineOfSightToChase) {
    Player* player = world.SpawnPlayer({0, 0, 0});
    EnemyConfig config;
    config.hearing_range = 0.0f;
    config.sight_angle = 360.0f;
    Enemy* blocked = world.SpawnEnemy(config, {0, 0, -10});
    world.AddStaticBox({0, 1.5f, -5}, {6, 3, 0.5f});
    Enemy* visible = world.SpawnEnemy(config, {10, 0, 0});
    blocked->SetTarget(player);
    visible->SetTarget(player);

    Run(0.2f);
    EXPECT_EQ(blocked->GetAIState(), EnemyAIState::Idle);
    EXPECT_EQ(visible->GetAIState(), EnemyAIState::Chase);

    Run(2.0f);   // runs towards the player and attacks in melee range
    EXPECT_LT(player->GetHealth().GetHealth(), player->GetHealth().GetMaxHealth());
}

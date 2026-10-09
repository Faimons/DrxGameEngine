/**
 * SHIFT - Simulation tests (blueprint phase 0 to 2 acceptance criteria)
 */

#include <gtest/gtest.h>

#include "ShiftConfig.h"
#include "ShiftLevel.h"
#include "ShiftWeapons.h"
#include "ShiftWorld.h"

#include <cmath>

using namespace Shift;
using DRX::Math::Vector3;

namespace {

constexpr float32 kRadToDeg = 180.0f / 3.14159265358979323846f;

ShiftConfig LoadConfig() {
    DRX::Config file;
    EXPECT_TRUE(file.LoadFromFile(SHIFT_CONFIG_PATH));
    ShiftConfig config;
    config.Load(file);
    return config;
}

class ShiftWorldTest : public ::testing::Test {
protected:
    void SetUp() override {
        config = LoadConfig();
        ASSERT_TRUE(world.Initialize(config));
        BuildTestRoom().Apply(world);
        shooter = world.AddPlayer(Team::A);
        dummy = world.AddPlayer(Team::B, true);
        ASSERT_NE(shooter, kInvalidPlayer);
        ASSERT_NE(dummy, kInvalidPlayer);
        Run(5);   // settle on the floor
        world.TakeEvents();
    }

    // Look angles from the shooter's eye to a point at `height` above the dummy's feet
    PlayerInput AimAt(PlayerId target, float32 height) {
        const Vector3 eye = world.GetPlayer(shooter)->GetFeet() + Vector3(0, config.player.eye_height, 0);
        const Vector3 point = world.GetPlayer(target)->GetFeet() + Vector3(0, height, 0);
        const Vector3 d = (point - eye).Normalized();
        PlayerInput input;
        input.yaw = std::atan2(d.x, -d.z) * kRadToDeg;
        input.pitch = std::asin(d.y) * kRadToDeg;
        return input;
    }

    void Run(int ticks, const PlayerInput& input = {}) {
        for (int i = 0; i < ticks; ++i) {
            world.SetInput(shooter, input);
            world.Step();
        }
    }

    // Clicks as fast as possible (press, release, press, ...) until `target` dies or `max_ticks` pass
    int ClickUntilDead(PlayerId target, float32 aim_height, int max_ticks) {
        PlayerInput input = AimAt(target, aim_height);
        for (int i = 0; i < max_ticks; ++i) {
            input.buttons = (i % 2 == 0) ? kButtonFire : 0;
            world.SetInput(shooter, input);
            world.Step();
            if (!world.GetPlayer(target)->alive) return i;
        }
        return -1;
    }

    ShiftConfig config;
    ShiftWorld world;
    PlayerId shooter = kInvalidPlayer;
    PlayerId dummy = kInvalidPlayer;
};

} // namespace

// Phase 0: every value comes from shift.json and matches the blueprint start values
TEST(ShiftConfigTest, FileMatchesBlueprint) {
    const ShiftConfig c = LoadConfig();
    EXPECT_FLOAT_EQ(c.player.max_health, 100.0f);
    EXPECT_FLOAT_EQ(c.player.walk_speed, 5.5f);
    EXPECT_FLOAT_EQ(c.player.carrier_speed, 4.0f);
    EXPECT_FLOAT_EQ(c.player.jump_height, 1.0f);
    EXPECT_FLOAT_EQ(c.player.radius * 2.0f, 0.6f);
    EXPECT_FLOAT_EQ(c.player.respawn_time, 8.0f);
    EXPECT_FLOAT_EQ(c.player.spawn_protection, 2.0f);
    EXPECT_FLOAT_EQ(c.weapons.pistol.damage, 20.0f);
    EXPECT_FLOAT_EQ(c.weapons.pistol.fire_rate, 5.0f);
    EXPECT_EQ(c.weapons.pistol.magazine, 12);
    EXPECT_FLOAT_EQ(c.weapons.pistol.reload_time, 1.4f);
    EXPECT_FLOAT_EQ(c.weapons.rifle.damage * c.weapons.rifle.headshot_multiplier, 21.0f);
    EXPECT_EQ(c.weapons.shotgun.pellets, 8u);
    EXPECT_FLOAT_EQ(c.weapons.sniper.zoom, 3.0f);
    EXPECT_FLOAT_EQ(c.shifts.interval, 30.0f);
    EXPECT_FLOAT_EQ(c.shifts.crush_gap, 0.6f);
    EXPECT_EQ(c.net.tick_rate, 30);
    EXPECT_EQ(c.round.rounds_to_win, 2);
}

TEST(ShiftConfigTest, InvalidValuesAreClamped) {
    DRX::Config file;
    ASSERT_TRUE(file.LoadFromString(R"({"player": {"walk_speed": -3}, "net": {"tick_rate": 1000}})"));
    ShiftConfig c;
    c.Load(file);
    EXPECT_GT(c.player.walk_speed, 0.0f);
    EXPECT_EQ(c.net.tick_rate, 128);
}

TEST(ShiftWeaponTest, FireRateMagazineAndReload) {
    const ShiftConfig c = LoadConfig();
    const float32 dt = c.GetTickInterval();

    WeaponState rifle;
    rifle.Equip(&c.weapons.rifle);
    uint32 shots = 0;
    for (int i = 0; i < 30; ++i) shots += UpdateWeapon(rifle, true, false, dt).shots;   // 1 s held
    EXPECT_EQ(shots, 10u);

    WeaponState sniper;   // 0.8 shots/s = 37.5 ticks: the remainder carries over, no drift
    sniper.Equip(&c.weapons.sniper);
    Array<int> shot_ticks;
    for (int i = 0; i <= 150; ++i) {
        if (UpdateWeapon(sniper, i % 2 == 0, false, dt).shots > 0) shot_ticks.push_back(i);
    }
    EXPECT_EQ(shot_ticks, (Array<int>{0, 38, 75, 113, 150}));   // 0, 1.25, 2.5, 3.75, 5.0 s on the 30 Hz grid

    WeaponState pistol;
    pistol.Equip(&c.weapons.pistol);
    for (int i = 0; i < 200 && pistol.ammo > 0; ++i) UpdateWeapon(pistol, i % 2 == 0, false, dt);
    EXPECT_EQ(pistol.ammo, 0);
    UpdateWeapon(pistol, false, false, dt);
    const WeaponTickResult click = UpdateWeapon(pistol, true, false, dt);
    EXPECT_TRUE(click.empty_click);
    EXPECT_TRUE(pistol.IsReloading());
    int reload_ticks = 0;
    while (pistol.IsReloading()) {
        UpdateWeapon(pistol, false, false, dt);
        ++reload_ticks;
    }
    EXPECT_NEAR(reload_ticks * dt, 1.4f, dt);
    EXPECT_EQ(pistol.ammo, 12);
}

TEST(ShiftWeaponTest, ShotgunFalloff) {
    const ShiftConfig c = LoadConfig();
    EXPECT_FLOAT_EQ(DamageFalloff(c.weapons.shotgun, 5.0f), 1.0f);
    EXPECT_FLOAT_EQ(DamageFalloff(c.weapons.shotgun, 14.0f), 0.65f);
    EXPECT_FLOAT_EQ(DamageFalloff(c.weapons.shotgun, 25.0f), 0.3f);
    EXPECT_FLOAT_EQ(DamageFalloff(c.weapons.pistol, 100.0f), 1.0f);
}

// Phase 1: walking speed, jump height, walls
TEST_F(ShiftWorldTest, MovementMatchesBlueprint) {
    PlayerInput walk;
    walk.yaw = 90.0f;   // east, towards the empty side of the room
    walk.move_y = 1.0f;
    const Vector3 start = world.GetPlayer(shooter)->GetFeet();
    Run(15, walk);   // 0.5 s
    const float32 speed = (world.GetPlayer(shooter)->GetFeet().x - start.x) / 0.5f;
    EXPECT_NEAR(speed, 5.5f, 0.1f);

    Run(60, walk);   // runs into the east wall and stays inside
    EXPECT_LE(world.GetPlayer(shooter)->GetFeet().x, 6.0f - config.player.radius + 1e-3f);

    PlayerInput jump;
    jump.buttons = kButtonJump;
    float32 apex = 0.0f;
    Run(1, jump);
    for (int i = 0; i < 40; ++i) {
        Run(1);
        apex = std::max(apex, world.GetPlayer(shooter)->GetFeet().y);
    }
    EXPECT_NEAR(apex, 1.0f, 0.08f);
    EXPECT_TRUE(world.GetPlayer(shooter)->IsGrounded());
}

// Phase 2: pistol kills the 100 HP dummy with 5 hits in about 0.8 s
TEST_F(ShiftWorldTest, PistolKillsDummyInFiveHits) {
    const int ticks = ClickUntilDead(dummy, 1.0f, 120);
    ASSERT_GE(ticks, 0);

    int hits = 0;
    uint32 first = 0, last = 0;
    for (const WorldEvent& e : world.TakeEvents()) {
        if (e.type == WorldEventType::Damage && e.target == dummy) {
            if (hits++ == 0) first = e.tick;
            last = e.tick;
            EXPECT_FLOAT_EQ(e.amount, 20.0f);
            EXPECT_FALSE(e.headshot);
        }
    }
    EXPECT_EQ(hits, 5);
    EXPECT_NEAR(static_cast<float32>(last - first) * world.GetTickInterval(), 0.8f, 0.001f);
    EXPECT_EQ(world.GetPlayer(shooter)->pistol.ammo, 7);
}

TEST_F(ShiftWorldTest, HeadshotsDealMoreDamage) {
    PlayerInput input = AimAt(dummy, 1.7f);
    input.buttons = kButtonFire;
    Run(1, input);
    bool found = false;
    for (const WorldEvent& e : world.TakeEvents()) {
        if (e.type == WorldEventType::Damage) {
            EXPECT_TRUE(e.headshot);
            EXPECT_FLOAT_EQ(e.amount, 30.0f);
            found = true;
        }
    }
    EXPECT_TRUE(found);
    EXPECT_FLOAT_EQ(world.GetPlayer(dummy)->health, 70.0f);
}

// Phase 2: death, respawn after 8 s with 2 s protection that ends when shooting
TEST_F(ShiftWorldTest, RespawnAndSpawnProtection) {
    ASSERT_GE(ClickUntilDead(dummy, 1.0f, 120), 0);
    SimPlayer* target = world.GetPlayer(dummy);
    EXPECT_FALSE(target->alive);
    EXPECT_FALSE(target->controller->IsEnabled());

    // Dead players are not hit: the shot reaches the wall behind
    PlayerInput input = AimAt(dummy, 1.0f);
    world.TakeEvents();
    Run(8);   // releases the trigger
    input.buttons = kButtonFire;
    Run(1, input);
    for (const WorldEvent& e : world.TakeEvents()) {
        if (e.type == WorldEventType::Shot) EXPECT_EQ(e.target, kInvalidPlayer);
    }

    const int respawn_ticks = static_cast<int>(std::lround(config.player.respawn_time / world.GetTickInterval()));
    Run(respawn_ticks - 12);
    EXPECT_FALSE(world.GetPlayer(dummy)->alive);
    Run(4);
    target = world.GetPlayer(dummy);
    ASSERT_TRUE(target->alive);
    EXPECT_TRUE(target->IsProtected());
    EXPECT_FLOAT_EQ(target->health, 100.0f);
    EXPECT_FALSE(world.ApplyDamage(dummy, shooter, 50.0f, false));
    EXPECT_FLOAT_EQ(target->health, 100.0f);

    Run(static_cast<int>(std::lround(config.player.spawn_protection / world.GetTickInterval())) + 1);
    EXPECT_FALSE(target->IsProtected());
    world.ApplyDamage(dummy, shooter, 50.0f, false);
    EXPECT_FLOAT_EQ(target->health, 50.0f);

    // Shooting ends the shooter's own protection early
    world.Kill(shooter, dummy);
    Run(respawn_ticks + 1);
    SimPlayer* me = world.GetPlayer(shooter);
    ASSERT_TRUE(me->alive);
    ASSERT_TRUE(me->IsProtected());
    PlayerInput fire;
    fire.buttons = kButtonFire;
    Run(1, fire);
    EXPECT_FALSE(me->IsProtected());
}

TEST_F(ShiftWorldTest, SimulationIsDeterministic) {
    ShiftWorld other;
    ASSERT_TRUE(other.Initialize(config));
    BuildTestRoom().Apply(other);
    const PlayerId a = other.AddPlayer(Team::A);
    other.AddPlayer(Team::B, true);
    for (int i = 0; i < 5; ++i) other.Step();

    for (int i = 0; i < 300; ++i) {
        PlayerInput input;
        input.yaw = std::sin(static_cast<float32>(i) * 0.05f) * 120.0f;
        input.pitch = -3.0f;
        input.move_y = 1.0f;
        input.move_x = std::cos(static_cast<float32>(i) * 0.1f);
        input.buttons = static_cast<uint16>((i % 7 == 0 ? kButtonFire : 0) | (i % 45 == 0 ? kButtonJump : 0));
        world.SetInput(shooter, input);
        other.SetInput(a, input);
        world.Step();
        other.Step();
    }
    const Vector3 p1 = world.GetPlayer(shooter)->GetFeet();
    const Vector3 p2 = other.GetPlayer(a)->GetFeet();
    EXPECT_EQ(p1.x, p2.x);
    EXPECT_EQ(p1.y, p2.y);
    EXPECT_EQ(p1.z, p2.z);
    EXPECT_EQ(world.GetPlayer(dummy)->health, other.GetPlayer(2)->health);
}

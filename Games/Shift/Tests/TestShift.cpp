/**
 * SHIFT - Simulation tests (blueprint phase 0 to 2 acceptance criteria)
 */

#include <gtest/gtest.h>

#include "ShiftConfig.h"
#include "ShiftLevel.h"
#include "ShiftSchedule.h"
#include "ShiftWeapons.h"
#include "ShiftWorld.h"

#include <algorithm>
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
        if (e.type == WorldEventType::Shot) {
            EXPECT_EQ(e.target, kInvalidPlayer);
        }
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

// ============================================================================
// Phase 3: moving wall
// ============================================================================

namespace {

// Horizontal penetration of a player's capsule into a segment (0 when apart)
float32 Penetration(const SimPlayer& player, const SimSegment& segment, float32 radius) {
    const Vector3 feet = player.GetFeet();
    const Vector3 center = segment.pose.center;
    const Vector3 half = segment.def.size * 0.5f;
    const float32 dx = std::max(0.0f, std::abs(feet.x - center.x) - half.x);
    const float32 dz = std::max(0.0f, std::abs(feet.z - center.z) - half.z);
    return std::max(0.0f, radius - std::sqrt(dx * dx + dz * dz));
}

} // namespace

TEST(ShiftScheduleTest, BlueprintTiming) {
    const ShiftConfig c = LoadConfig();
    const WallShiftSettings& s = c.shifts;

    ShiftPhase p = EvaluateShiftPhase(s, 0.0f);
    EXPECT_FALSE(p.warning);
    EXPECT_FALSE(p.moving);
    EXPECT_FLOAT_EQ(p.time_to_next, 30.0f);

    EXPECT_FALSE(EvaluateShiftPhase(s, 24.9f).warning);
    EXPECT_TRUE(EvaluateShiftPhase(s, 25.0f).warning);    // siren 5 s before
    EXPECT_TRUE(EvaluateShiftPhase(s, 29.99f).warning);
    p = EvaluateShiftPhase(s, 30.0f);
    EXPECT_TRUE(p.moving);
    EXPECT_FALSE(p.warning);
    EXPECT_EQ(p.next, 1);
    EXPECT_TRUE(EvaluateShiftPhase(s, 33.99f).moving);
    p = EvaluateShiftPhase(s, 34.0f);
    EXPECT_FALSE(p.moving);
    EXPECT_EQ(p.completed, 1);
    EXPECT_FLOAT_EQ(p.time_to_next, 26.0f);
    EXPECT_TRUE(EvaluateShiftPhase(s, 89.0f).moving == false && EvaluateShiftPhase(s, 86.0f).warning);
}

TEST(ShiftScheduleTest, SegmentPositionAndSpeedLimit) {
    const ShiftConfig c = LoadConfig();
    const Level level = BuildTestRoom();
    ASSERT_EQ(level.segments.size(), 1u);
    const MovingSegmentDef& door = level.segments[0];
    const ShiftPlan plan(ShiftMode::ToggleAll, 7, GroupBit(SegmentGroup::Toggle), c.shifts.all_open_max_per);
    auto amount = [&](float32 t) { return SegmentAmount(c.shifts, plan, door, t); };

    EXPECT_FLOAT_EQ(amount(10.0f), 1.0f);
    EXPECT_FLOAT_EQ(amount(32.0f), 0.5f);   // closing, halfway
    EXPECT_FLOAT_EQ(amount(40.0f), 0.0f);
    EXPECT_FLOAT_EQ(amount(62.0f), 0.5f);   // reopening
    EXPECT_FLOAT_EQ(amount(70.0f), 1.0f);

    // Never faster than 1.5 m/s, sampled on the tick grid over 20 shifts
    const float32 dt = c.GetTickInterval();
    float32 max_speed = 0.0f;
    Vector3 previous = door.PoseAt(amount(0.0f)).center;
    for (int tick = 1; tick <= static_cast<int>(20 * 30.0f / dt); ++tick) {
        const Vector3 position = door.PoseAt(amount(static_cast<float32>(tick) * dt)).center;
        max_speed = std::max(max_speed, (position - previous).Length() / dt);
        previous = position;
    }
    EXPECT_LE(max_speed, c.shifts.max_speed + 1e-3f);
    EXPECT_GT(max_speed, 0.5f);

    // A segment that would need more than 1.5 m/s moves longer instead
    MovingSegmentDef far = door;
    far.move = Vector3(9.0f, 0, 0);
    EXPECT_FLOAT_EQ(SegmentMoveDuration(c.shifts, far), 6.0f);
}

TEST(ShiftWallTest, CrushedWhenGapBelowPlayerWidth) {
    ShiftConfig config = LoadConfig();
    ShiftWorld world;
    ASSERT_TRUE(world.Initialize(config));

    // Closed 1 m wide corridor along x with a stop block at x = 0; the segment closes towards it
    world.AddStaticBox({0, -0.25f, 0}, {30, 0.5f, 10});
    world.AddStaticBox({0, 2, -0.75f}, {30, 4, 0.5f});
    world.AddStaticBox({0, 2, 0.75f}, {30, 4, 0.5f});
    world.AddStaticBox({-0.5f, 2, 0}, {1, 4, 1});   // stop face at x = 0
    world.SetShiftMode(ShiftMode::ToggleAll);
    MovingSegmentDef segment;
    segment.size = {1.0f, 4.0f, 1.0f};
    segment.center = {0.5f, 2.0f, 0.0f};   // state 0 (closed) touches the stop; starts in state 1 (open)
    segment.move = {4.0f, 0.0f, 0.0f};
    world.AddSegment(segment);
    world.SetSpawn(Team::A, {Vector3(2.0f, 0.0f, 0.0f), 0.0f});
    const PlayerId id = world.AddPlayer(Team::A);

    float32 gap_at_death = -1.0f;
    for (int i = 0; i < static_cast<int>(36.0f / world.GetTickInterval()); ++i) {
        world.Step();
        for (const WorldEvent& e : world.TakeEvents()) {
            if (e.type == WorldEventType::Kill && e.target == id) {
                EXPECT_TRUE(e.crushed);
                const float32 leading_edge = world.GetSegments()[0].pose.center.x - 0.5f;
                gap_at_death = leading_edge;   // stop face is at x = 0
            }
        }
        const SimPlayer* p = world.GetPlayer(id);
        if (p->alive) {
            EXPECT_LT(Penetration(*p, world.GetSegments()[0], config.player.radius), 0.02f);
            EXPECT_GT(p->GetFeet().x, -0.01f);   // never pushed into the stop block
        }
    }
    ASSERT_GT(gap_at_death, 0.0f) << "player was not crushed";
    EXPECT_NEAR(gap_at_death, config.shifts.crush_gap, 0.04f);
    EXPECT_LT(gap_at_death, config.shifts.crush_gap);
}

TEST(ShiftWallTest, TwentyCyclesInTestRoomWithoutFallingThrough) {
    ShiftConfig config = LoadConfig();
    ShiftWorld world;
    ASSERT_TRUE(world.Initialize(config));
    Level level = BuildTestRoom();
    level.spawns[0] = {Vector3(0.0f, 0.0f, -2.0f), 0.0f};   // standing in the doorway
    level.Apply(world);
    const PlayerId stander = world.AddPlayer(Team::A);
    const PlayerId walker = world.AddPlayer(Team::A);

    const float32 dt = world.GetTickInterval();
    const int ticks = static_cast<int>((20.0f * config.shifts.interval + config.shifts.move_duration) / dt) + 10;
    int crushes = 0;
    int pushed_ticks = 0;
    for (int i = 0; i < ticks; ++i) {
        // The walker runs north/south through the doorway all the time
        PlayerInput run;
        run.move_y = 1.0f;
        run.yaw = (i / 90) % 2 == 0 ? 0.0f : 180.0f;
        world.SetInput(walker, run);
        PlayerInput stand;   // the stander does nothing
        world.SetInput(stander, stand);
        const Vector3 before = world.GetPlayer(stander)->GetFeet();
        world.Step();

        for (const WorldEvent& e : world.TakeEvents()) {
            if (e.type == WorldEventType::Kill && e.crushed) ++crushes;
        }
        for (const PlayerId id : {stander, walker}) {
            const SimPlayer* p = world.GetPlayer(id);
            if (!p->alive) continue;
            ASSERT_GT(p->GetFeet().y, -0.05f) << "fell through the floor at tick " << i;
            ASSERT_LT(Penetration(*p, world.GetSegments()[0], config.player.radius), 0.02f) << "inside the wall at tick " << i;
            ASSERT_GT(p->GetFeet().z, -6.0f + config.player.radius - 0.02f);
            ASSERT_LT(p->GetFeet().z, 6.0f - config.player.radius + 0.02f);
        }
        if ((world.GetPlayer(stander)->GetFeet() - before).Length() > 1e-4f && world.GetPlayer(stander)->alive) ++pushed_ticks;
    }
    EXPECT_EQ(world.GetShiftPhase().completed, 20);
    EXPECT_GT(pushed_ticks, 0);   // the wall did push the player standing in its way
    EXPECT_EQ(crushes, 0);        // the doorway is never narrower than the player
}

TEST(ShiftWallTest, WarningAndStartEvents) {
    ShiftConfig config = LoadConfig();
    ShiftWorld world;
    ASSERT_TRUE(world.Initialize(config));
    BuildTestRoom().Apply(world);

    Array<float32> warnings, starts;
    for (int i = 0; i < static_cast<int>(65.0f / world.GetTickInterval()); ++i) {
        world.Step();
        for (const WorldEvent& e : world.TakeEvents()) {
            if (e.type == WorldEventType::ShiftWarning) warnings.push_back(world.GetRoundTime());
            if (e.type == WorldEventType::ShiftStart) starts.push_back(world.GetRoundTime());
        }
    }
    ASSERT_EQ(warnings.size(), 2u);
    ASSERT_EQ(starts.size(), 2u);
    EXPECT_NEAR(warnings[0], 25.0f, 0.04f);
    EXPECT_NEAR(starts[0], 30.0f, 0.04f);
    EXPECT_NEAR(warnings[1], 55.0f, 0.04f);
    EXPECT_NEAR(starts[1], 60.0f, 0.04f);
}

// ============================================================================
// Phase 6: shift patterns and arena
// ============================================================================

namespace {

constexpr GroupMask kArenaGroups = GroupBit(SegmentGroup::NorthGates) | GroupBit(SegmentGroup::SouthGates) |
                                   GroupBit(SegmentGroup::MachineRoom) | GroupBit(SegmentGroup::CoreDoorsNS) |
                                   GroupBit(SegmentGroup::CoreDoorE) | GroupBit(SegmentGroup::NorthPanels);

bool Has(GroupMask mask, SegmentGroup group) {
    return (mask & GroupBit(group)) != 0;
}

} // namespace

TEST(ShiftPlanTest, PatternRules) {
    const ShiftConfig c = LoadConfig();
    for (uint32 seed = 1; seed <= 200; ++seed) {
        const ShiftPlan plan(ShiftMode::Patterns, seed, kArenaGroups, c.shifts.all_open_max_per);
        const ShiftPlan same(ShiftMode::Patterns, seed, kArenaGroups, c.shifts.all_open_max_per);
        int32 last_all_open = -100;
        for (int32 k = 1; k <= ShiftPlan::kMaxPlanned; ++k) {
            const ShiftPattern p = plan.GetPattern(k);
            ASSERT_EQ(p, same.GetPattern(k));   // same seed, same walls on every machine
            ASSERT_NE(p, ShiftPattern::None);
            if (k > 1) {
                ASSERT_NE(p, plan.GetPattern(k - 1)) << "seed " << seed << " shift " << k;
            }
            if (p == ShiftPattern::AllOpen) {
                ASSERT_GE(k - last_all_open, c.shifts.all_open_max_per) << "seed " << seed;
                last_all_open = k;
            }

            const GroupMask state = plan.GetStateAfter(k);
            if (p != ShiftPattern::AllOpen) {
                // Exactly one wing is open, the core has either both wing doors or the machine door
                ASSERT_NE(Has(state, SegmentGroup::NorthGates), Has(state, SegmentGroup::SouthGates));
                ASSERT_NE(Has(state, SegmentGroup::CoreDoorsNS), Has(state, SegmentGroup::CoreDoorE));
            } else {
                ASSERT_TRUE(Has(state, SegmentGroup::NorthGates) && Has(state, SegmentGroup::SouthGates) &&
                            Has(state, SegmentGroup::CoreDoorsNS) && Has(state, SegmentGroup::CoreDoorE) &&
                            Has(state, SegmentGroup::NorthPanels));
            }
        }
    }

    // Start: north wing closed, south open, core doors to the wings open, machine door closed
    const GroupMask start = ShiftPlan::InitialState();
    EXPECT_FALSE(Has(start, SegmentGroup::NorthGates));
    EXPECT_TRUE(Has(start, SegmentGroup::SouthGates));
    EXPECT_TRUE(Has(start, SegmentGroup::CoreDoorsNS));
    EXPECT_FALSE(Has(start, SegmentGroup::CoreDoorE));
}

TEST(ShiftPlanTest, EachPatternMovesItsWalls) {
    const ShiftConfig c = LoadConfig();
    // Find shifts with each pattern and check which groups change
    const ShiftPlan plan(ShiftMode::Patterns, 12345, kArenaGroups, c.shifts.all_open_max_per);
    bool seen[6] = {};
    for (int32 k = 1; k <= ShiftPlan::kMaxPlanned; ++k) {
        const ShiftPattern p = plan.GetPattern(k);
        const GroupMask changed = plan.GetChangedGroups(k);
        seen[static_cast<int>(p)] = true;
        switch (p) {
            case ShiftPattern::GateSwap:
                EXPECT_TRUE(Has(changed, SegmentGroup::NorthGates) || Has(changed, SegmentGroup::SouthGates));
                EXPECT_FALSE(Has(changed, SegmentGroup::MachineRoom));
                break;
            case ShiftPattern::Rotate:
                EXPECT_TRUE(Has(changed, SegmentGroup::MachineRoom));
                break;
            case ShiftPattern::Squeeze:
                EXPECT_TRUE(Has(changed, SegmentGroup::NorthPanels));
                break;
            case ShiftPattern::CoreRebuild:
                EXPECT_TRUE(Has(changed, SegmentGroup::CoreDoorsNS) || Has(changed, SegmentGroup::CoreDoorE));
                break;
            case ShiftPattern::AllOpen:
                EXPECT_FALSE(Has(changed, SegmentGroup::MachineRoom));
                EXPECT_TRUE(Has(plan.GetStateAfter(k), SegmentGroup::NorthGates));
                break;
            default:
                break;
        }
    }
    for (int p = 1; p <= 5; ++p) EXPECT_TRUE(seen[p]) << "pattern " << p << " never chosen";
}

TEST(ShiftArenaTest, WallsNeverExceedMaxSpeed) {
    const ShiftConfig c = LoadConfig();
    const Level arena = BuildArena();
    const ShiftPlan plan(ShiftMode::Patterns, 99, kArenaGroups, c.shifts.all_open_max_per);
    const float32 dt = c.GetTickInterval();
    for (const MovingSegmentDef& def : arena.segments) {
        Array<Vector3> previous;
        float32 max_speed = 0.0f;
        for (int tick = 0; tick <= static_cast<int>(12 * c.shifts.interval / dt); ++tick) {
            const SegmentPose pose = def.PoseAt(SegmentAmount(c.shifts, plan, def, static_cast<float32>(tick) * dt));
            // Corners of the segment in the horizontal plane
            Array<Vector3> corners;
            for (const float32 sx : {-0.5f, 0.5f}) {
                for (const float32 sz : {-0.5f, 0.5f}) {
                    corners.push_back(pose.center + pose.Rotation().Rotate(Vector3(def.size.x * sx, 0.0f, def.size.z * sz)));
                }
            }
            for (size_t i = 0; i < previous.size(); ++i) {
                max_speed = std::max(max_speed, (corners[i] - previous[i]).Length() / dt);
            }
            previous = corners;
        }
        EXPECT_LE(max_speed, c.shifts.max_speed + 0.01f) << def.name;
    }
}

TEST(ShiftArenaTest, TenMinutesShowAllPatternsAndNothingHangs) {
    ShiftConfig config = LoadConfig();
    ShiftWorld world;
    ASSERT_TRUE(world.Initialize(config));
    BuildArena().Apply(world);
    const PlayerId a = world.AddPlayer(Team::A);
    const PlayerId b = world.AddPlayer(Team::B);
    world.StartMatch(4242);

    bool seen[6] = {};
    int crushes = 0;
    const float32 dt = world.GetTickInterval();
    const int ticks = static_cast<int>(600.0f / dt);
    for (int i = 0; i < ticks; ++i) {
        // Both run around: straight, turning every few seconds, jumping now and then
        for (const PlayerId id : {a, b}) {
            PlayerInput in;
            in.move_y = 1.0f;
            in.yaw = static_cast<float32>((HashU32(id, static_cast<uint32>(i / 75), 9) % 360));
            if (i % 50 == 0) in.buttons = kButtonJump;
            world.SetInput(id, in);
        }
        world.Step();
        for (const WorldEvent& e : world.TakeEvents()) {
            if (e.type == WorldEventType::ShiftStart) {
                seen[static_cast<int>(world.GetPlan().GetPattern(world.GetShiftPhase().next))] = true;
            }
            if (e.type == WorldEventType::Kill && e.crushed) ++crushes;
        }
        for (const PlayerId id : {a, b}) {
            const SimPlayer* p = world.GetPlayer(id);
            if (!p->alive) continue;
            const Vector3 f = p->GetFeet();
            ASSERT_GT(f.y, -0.1f) << "fell through the floor at tick " << i;
            ASSERT_LT(std::abs(f.x), 37.0f) << "left the map at tick " << i;
            ASSERT_LT(std::abs(f.z), 13.5f) << "left the map at tick " << i;
        }
    }
    for (int p = 1; p <= 5; ++p) EXPECT_TRUE(seen[p]) << "pattern " << p << " not shown in 10 minutes";
    EXPECT_GE(world.GetMatch().round, 1);
    std::printf("[ arena  ] 10 min: %d crushes, round %d\n", crushes, world.GetMatch().round);
}

// ============================================================================
// Phase 5: round logic with the core
// ============================================================================

namespace {

// Flat open field: core in the middle, exits 12 m out, spawns 8 m out
Level OpenLevel() {
    Level level;
    level.name = "open";
    level.AddBlock(-30.0f, 30.0f, -30.0f, 30.0f, Surface::Floor, -0.5f, 0.0f);
    level.spawns[0] = {Vector3(-8.0f, 0.0f, 0.0f), 90.0f};
    level.spawns[1] = {Vector3(8.0f, 0.0f, 0.0f), 270.0f};
    level.has_core = true;
    level.core_home = Vector3(0.0f, 0.0f, 0.0f);
    level.exits[0] = {Vector3(-12.0f, 1.0f, 0.0f), Vector3(3.0f, 2.0f, 3.0f)};
    level.exits[1] = {Vector3(12.0f, 1.0f, 0.0f), Vector3(3.0f, 2.0f, 3.0f)};
    return level;
}

PlayerInput Towards(const Vector3& from, const Vector3& to, uint16 buttons = 0) {
    PlayerInput in;
    const Vector3 d = to - from;
    in.yaw = std::atan2(d.x, -d.z) * kRadToDeg;
    in.move_y = (d.x * d.x + d.z * d.z) > 0.04f ? 1.0f : 0.0f;
    in.buttons = buttons;
    return in;
}

class ShiftMatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        config = LoadConfig();
        ASSERT_TRUE(world.Initialize(config));
        OpenLevel().Apply(world);
        a = world.AddPlayer(Team::A);
        b = world.AddPlayer(Team::B);
        world.StartMatch(1);
    }

    void Step(const PlayerInput& in_a = {}, const PlayerInput& in_b = {}) {
        world.SetInput(a, in_a);
        world.SetInput(b, in_b);
        world.Step();
        for (const WorldEvent& e : world.TakeEvents()) events.push_back(e);
    }

    int Count(WorldEventType type) const {
        return static_cast<int>(std::count_if(events.begin(), events.end(), [type](const WorldEvent& e) { return e.type == type; }));
    }

    // Player `who` walks to the core, picks it up and carries it home; returns ticks used
    int CarryHome(PlayerId who) {
        const int start_round = world.GetMatch().round;
        for (int i = 0; i < 3000; ++i) {
            const SimPlayer* p = world.GetPlayer(who);
            const bool carrying = world.IsCarrier(who);
            const Vector3 goal = carrying ? world.GetExit(p->team).center : world.GetCore().position;
            const PlayerInput in = Towards(p->GetFeet(), goal, carrying ? 0 : kButtonInteract);
            if (who == a) Step(in, {});
            else Step({}, in);
            if (world.GetMatch().phase != RoundPhase::Live || world.GetMatch().round != start_round) return i;
        }
        return -1;
    }

    void WaitForLive() {
        for (int i = 0; i < 2000 && world.GetMatch().phase != RoundPhase::Live; ++i) Step();
    }

    ShiftConfig config;
    ShiftWorld world;
    PlayerId a = kInvalidPlayer;
    PlayerId b = kInvalidPlayer;
    Array<WorldEvent> events;
};

} // namespace

TEST_F(ShiftMatchTest, FreezeAtRoundStart) {
    EXPECT_EQ(world.GetMatch().phase, RoundPhase::Freeze);
    const Vector3 start = world.GetPlayer(a)->GetFeet();
    PlayerInput run;
    run.move_y = 1.0f;
    run.yaw = 90.0f;
    run.buttons = kButtonFire;
    const int freeze_ticks = static_cast<int>(std::lround(config.round.freeze_time / world.GetTickInterval()));
    for (int i = 0; i < freeze_ticks - 1; ++i) Step(run);
    EXPECT_LT((world.GetPlayer(a)->GetFeet() - start).Length(), 0.01f);   // nothing moves for 10 s
    EXPECT_EQ(Count(WorldEventType::Shot), 0);
    Step(run);
    Step(run);
    EXPECT_EQ(world.GetMatch().phase, RoundPhase::Live);
    EXPECT_GT((world.GetPlayer(a)->GetFeet() - start).Length(), 0.05f);
}

TEST_F(ShiftMatchTest, PickupNeedsOneSecondAndCarrierIsSlowWithPistolOnly) {
    WaitForLive();
    // Walk to the core without holding the key: nothing happens
    for (int i = 0; i < 200; ++i) Step(Towards(world.GetPlayer(a)->GetFeet(), world.GetCore().position));
    EXPECT_EQ(world.GetCore().location, CoreLocation::Home);

    // Hold for just under a second: still not picked up; the full second picks it up
    const int pickup_ticks = static_cast<int>(std::lround(config.player.core_pickup_time / world.GetTickInterval()));
    PlayerInput hold;
    hold.buttons = kButtonInteract;
    for (int i = 0; i < pickup_ticks - 1; ++i) Step(hold);
    EXPECT_EQ(world.GetCore().location, CoreLocation::Home);
    Step(hold);
    EXPECT_TRUE(world.IsCarrier(a));
    EXPECT_EQ(Count(WorldEventType::CorePickup), 1);

    // Carrier speed 4.0 m/s
    PlayerInput run;
    run.move_y = 1.0f;
    run.yaw = 270.0f;
    for (int i = 0; i < 10; ++i) Step(run);
    const Vector3 v = world.GetPlayer(a)->controller->GetVelocity();
    EXPECT_NEAR(std::sqrt(v.x * v.x + v.z * v.z), config.player.carrier_speed, 0.05f);
}

TEST_F(ShiftMatchTest, CompleteMatchWithoutRestart) {
    // Round 1 and 2: A carries the core home -> 2:0, match over
    for (int round = 1; round <= 2; ++round) {
        WaitForLive();
        ASSERT_EQ(world.GetMatch().round, round);
        ASSERT_GE(CarryHome(a), 0) << "round " << round;
        EXPECT_EQ(world.GetMatch().score[0], round);
        if (round == 1) {
            EXPECT_EQ(world.GetMatch().phase, RoundPhase::RoundOver);
            // Next round: everybody back at the spawn, core in the middle
            for (int i = 0; i < 2000 && world.GetMatch().phase == RoundPhase::RoundOver; ++i) Step();
            EXPECT_EQ(world.GetMatch().round, 2);
            EXPECT_EQ(world.GetCore().location, CoreLocation::Home);
            EXPECT_LT((world.GetPlayer(a)->GetFeet() - world.GetSpawn(Team::A).position).Length(), 0.1f);
        }
    }
    EXPECT_EQ(world.GetMatch().phase, RoundPhase::MatchOver);
    EXPECT_EQ(world.GetMatch().match_winner, Team::A);
    EXPECT_EQ(Count(WorldEventType::MatchEnd), 1);

    // After the result screen a new match starts at 0:0
    for (int i = 0; i < 2000 && world.GetMatch().phase == RoundPhase::MatchOver; ++i) Step();
    EXPECT_EQ(world.GetMatch().score[0], 0);
    EXPECT_EQ(world.GetMatch().score[1], 0);
    EXPECT_EQ(world.GetMatch().round, 1);
    EXPECT_EQ(world.GetMatch().phase, RoundPhase::Freeze);
}

TEST(ShiftMatchRules, DropReturnAndTimeLimit) {
    ShiftConfig config = LoadConfig();
    config.round.freeze_time = 0.0f;
    config.round.round_time = 60.0f;
    ShiftWorld world;
    ASSERT_TRUE(world.Initialize(config));
    OpenLevel().Apply(world);
    const PlayerId a = world.AddPlayer(Team::A);
    const PlayerId b = world.AddPlayer(Team::B);
    world.StartMatch(3);
    world.Step();

    // Teleport A to the core and pick it up
    Physics::CharacterState at_core;
    at_core.position = Vector3(0.5f, 0.0f, 0.0f);
    world.GetPlayer(a)->controller->SetState(at_core);
    PlayerInput hold;
    hold.buttons = kButtonInteract;
    for (int i = 0; i < 40 && !world.IsCarrier(a); ++i) {
        world.SetInput(a, hold);
        world.Step();
    }
    ASSERT_TRUE(world.IsCarrier(a));

    // Carrier dies -> core lies where he fell
    world.ApplyDamage(a, b, 1000.0f, false);
    world.Step();
    EXPECT_EQ(world.GetCore().location, CoreLocation::Dropped);
    EXPECT_LT((world.GetCore().position - Vector3(0.5f, 0.0f, 0.0f)).Length(), 0.2f);

    // 20 s untouched -> back in the middle
    const int ticks = static_cast<int>(config.round.core_return_time / world.GetTickInterval());
    for (int i = 0; i < ticks - 2; ++i) world.Step();
    EXPECT_EQ(world.GetCore().location, CoreLocation::Dropped);
    world.Step();
    world.Step();
    EXPECT_EQ(world.GetCore().location, CoreLocation::Home);

    // Time limit: team A carried it last -> A wins the round
    while (world.GetMatch().phase == RoundPhase::Live) world.Step();
    EXPECT_EQ(world.GetMatch().phase, RoundPhase::RoundOver);
    EXPECT_TRUE(world.GetMatch().round_has_winner);
    EXPECT_EQ(world.GetMatch().round_winner, Team::A);
    EXPECT_EQ(world.GetMatch().score[0], 1);

    // Next round nobody touches the core -> no winner, the round is repeated
    while (world.GetMatch().phase != RoundPhase::Live) world.Step();
    EXPECT_EQ(world.GetMatch().round, 2);
    while (world.GetMatch().phase == RoundPhase::Live) world.Step();
    EXPECT_FALSE(world.GetMatch().round_has_winner);
    while (world.GetMatch().phase != RoundPhase::Live) world.Step();
    EXPECT_EQ(world.GetMatch().round, 2);
    EXPECT_EQ(world.GetMatch().score[0], 1);
    EXPECT_EQ(world.GetMatch().score[1], 0);
}

TEST(ShiftMatchRules, CoreIsPushedOutOfAClosingGap) {
    ShiftConfig config = LoadConfig();
    config.round.freeze_time = 0.0f;
    config.round.core_return_time = 1000.0f;
    ShiftWorld world;
    ASSERT_TRUE(world.Initialize(config));
    Level level = OpenLevel();
    // A wall that sinks into the floor and rises again under the dropped core (north gate group)
    MovingSegmentDef gate;
    gate.name = "Test";
    gate.group = SegmentGroup::NorthGates;
    gate.size = {4.0f, 4.0f, 1.0f};
    gate.center = {3.0f, 2.0f, 0.0f};
    gate.move = {0.0f, -4.1f, 0.0f};
    level.segments.push_back(gate);
    level.Apply(world);
    const PlayerId a = world.AddPlayer(Team::A);
    world.StartMatch(5);
    world.Step();

    // Pick up the core, wait until the gate has sunk, then drop the core on its footprint
    Physics::CharacterState at_core;
    at_core.position = Vector3(0.5f, 0.0f, 0.0f);
    world.GetPlayer(a)->controller->SetState(at_core);
    PlayerInput hold;
    hold.buttons = kButtonInteract;
    for (int i = 0; i < 40 && !world.IsCarrier(a); ++i) {
        world.SetInput(a, hold);
        world.Step();
    }
    ASSERT_TRUE(world.IsCarrier(a));
    for (int i = 0; i < 4000; ++i) {
        // Wait until the gate is fully sunk, then drop the core on it
        if (world.GetSegments()[0].amount >= 1.0f) break;
        world.SetInput(a, {});
        world.Step();
    }
    ASSERT_GE(world.GetSegments()[0].amount, 1.0f);
    Physics::CharacterState on_gate;
    on_gate.position = Vector3(3.0f, 0.0f, 0.2f);
    world.GetPlayer(a)->controller->SetState(on_gate);
    world.Step();
    world.Kill(a, kInvalidPlayer);
    world.Step();
    ASSERT_EQ(world.GetCore().location, CoreLocation::Dropped);

    // Run until the gate has risen again: the core must never be inside it
    bool moved = false;
    for (int i = 0; i < 4000; ++i) {
        world.Step();
        for (const WorldEvent& e : world.TakeEvents()) moved |= e.type == WorldEventType::CoreMoved;
        const SimSegment& s = world.GetSegments()[0];
        const Vector3 c = world.GetCore().position;
        const bool inside = std::abs(c.x - s.pose.center.x) < 2.0f && std::abs(c.z - s.pose.center.z) < 0.5f &&
                            c.y + 0.3f > s.pose.center.y - 2.0f && c.y < s.pose.center.y + 2.0f;
        ASSERT_FALSE(inside && world.GetCore().location != CoreLocation::Home) << "core inside the wall at tick " << i;
        if (s.amount <= 0.0f && moved) break;
    }
    EXPECT_TRUE(moved);
}

// ============================================================================
// Phase 4: network duel over UDP with 100 ms artificial latency
// ============================================================================

#include "ShiftNet.h"

#include <map>

namespace {

struct DuelResult {
    int server_shots = 0;
    int server_hits = 0;
    int compared_ticks = 0;
    int wall_mismatches = 0;
    uint32 corrections = 0;
    uint32 missing_inputs = 0;
    float32 rtt_ms = 0.0f;
};

Level DuelLevel() {
    Level level = OpenLevel();
    // A gate far away from the duel that moves on every pattern shift
    MovingSegmentDef gate;
    gate.name = "Duelltor";
    gate.group = SegmentGroup::NorthGates;
    gate.size = {4.0f, 4.0f, 0.4f};
    gate.center = {0.0f, 2.0f, -20.0f};
    gate.move = {0.0f, -4.1f, 0.0f};
    level.segments.push_back(gate);
    return level;
}

// Host (team A) strafes; the client (team B) shoots at where it sees the host
DuelResult RunDuel(float32 lag_compensation_ms, float32 seconds) {
    ShiftConfig config = LoadConfig();
    config.round.freeze_time = 0.0f;
    config.player.max_health = 1.0e6f;   // the target never dies
    config.net.lag_compensation_max_ms = lag_compensation_ms;

    ShiftWorld server_world;
    EXPECT_TRUE(server_world.Initialize(config));
    DuelLevel().Apply(server_world);
    const PlayerId host = server_world.AddPlayer(Team::A);
    server_world.StartMatch(77);

    // 50 ms each way = 100 ms round trip
    DRX::Network::ConditionerSettings lag;
    lag.latency_ms = 50.0f;
    ShiftServer server;
    EXPECT_TRUE(server.Start(server_world, "duel", 0, lag));

    float64 now = 1.0;
    ShiftClient client;
    EXPECT_TRUE(client.Connect(config, DRX::Network::Address::Loopback(server.GetPort()), now,
                               [](const String&) { return DuelLevel(); }, lag));

    const float32 dt = config.GetTickInterval();
    std::map<uint32, SegmentPose> server_walls;
    std::map<uint32, SegmentPose> client_walls;
    DuelResult result;
    const int ticks = static_cast<int>(seconds / dt);
    for (int i = 0; i < ticks; ++i) {
        now += dt;
        // --- server tick ---
        server.Receive(now);
        PlayerInput strafe;
        strafe.yaw = 90.0f;
        strafe.move_x = ((i / 25) % 2 == 0) ? 1.0f : -1.0f;
        server_world.SetInput(host, strafe);
        server_world.Step();
        const Array<WorldEvent> events = server_world.TakeEvents();
        for (const WorldEvent& e : events) {
            if (e.type == WorldEventType::Shot && e.source != host) {
                ++result.server_shots;
                if (e.target == host) ++result.server_hits;
            }
        }
        server_walls[server_world.GetTick()] = server_world.GetSegments()[0].pose;
        server.Send(events, now);

        // --- client tick ---
        client.Receive(now);
        PlayerInput aim;
        if (client.IsReady()) {
            const SimPlayer* me = client.GetWorld().GetPlayer(client.GetLocalId());
            RemotePlayerState target;
            if (me && client.GetRemotePlayer(host, client.GetRenderTick(now), target)) {
                const Vector3 eye = me->GetFeet() + Vector3(0.0f, config.player.eye_height, 0.0f);
                const Vector3 d = (target.feet + Vector3(0.0f, 1.1f, 0.0f) - eye).Normalized();
                aim.yaw = std::atan2(d.x, -d.z) * kRadToDeg;
                aim.pitch = std::asin(d.y) * kRadToDeg;
                // Start shooting after two seconds of synchronisation; one click every 12 ticks
                if (i > 60 && i % 12 == 0) aim.buttons = kButtonFire;
            }
        }
        client.Tick(aim, now);
        client.Flush(now);
        client.TakeEvents();
        if (client.IsReady() && !client.GetWorld().GetSegments().empty()) {
            client_walls[client.GetWorld().GetTick()] = client.GetWorld().GetSegments()[0].pose;
        }
    }

    // Both machines must have had the wall at the same place at the same tick
    for (const auto& [tick, pose] : client_walls) {
        const auto it = server_walls.find(tick);
        if (it == server_walls.end() || tick < 90) continue;
        ++result.compared_ticks;
        if ((it->second.center - pose.center).Length() > 1.0e-4f) ++result.wall_mismatches;
    }
    result.corrections = client.GetStats().corrections;
    result.missing_inputs = server.GetStats().missing_inputs;
    result.rtt_ms = client.GetStats().rtt_ms;
    return result;
}

} // namespace

TEST(ShiftNetTest, WallsAgreeAndHitsCountAt100ms) {
    const DuelResult with = RunDuel(400.0f, 70.0f);   // covers two wall shifts
    std::printf("[ duel   ] rtt %.0f ms, %d/%d hits, %u corrections, %u missing inputs, %d wall ticks compared\n",
                with.rtt_ms, with.server_hits, with.server_shots, with.corrections, with.missing_inputs,
                with.compared_ticks);
    EXPECT_GT(with.rtt_ms, 80.0f);
    EXPECT_GT(with.compared_ticks, 1500);
    EXPECT_EQ(with.wall_mismatches, 0);
    ASSERT_GT(with.server_shots, 100);
    EXPECT_GE(with.server_hits, with.server_shots * 9 / 10) << "hits must count despite 100 ms latency";
    EXPECT_LT(with.missing_inputs, 30u);

    // Without lag compensation most shots at the strafing target miss
    const DuelResult without = RunDuel(0.0f, 20.0f);
    std::printf("[ duel   ] without lag compensation: %d/%d hits\n", without.server_hits, without.server_shots);
    EXPECT_LT(without.server_hits, without.server_shots * 6 / 10);
}

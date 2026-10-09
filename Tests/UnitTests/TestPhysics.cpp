/**
 * DRX Engine - Physics tests (character controller, queries, events)
 */

#include <gtest/gtest.h>
#include "Physics/Public/PhysicsWorld.h"

#include <cmath>

using namespace DRX;
using namespace DRX::Physics;
using Math::Vector3;

namespace {

constexpr float32 kDt = 1.0f / 60.0f;

class PhysicsTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(world.Initialize());
        AddBox({0, -0.5f, 0}, {50, 0.5f, 50});   // floor, top at y = 0
    }

    SharedPtr<RigidBody> AddBox(const Vector3& center, const Vector3& half_extents,
                                RigidBodyType type = RigidBodyType::Static, void* user_data = nullptr,
                                bool trigger = false, const Math::Quaternion& rotation = {}) {
        RigidBodyDesc desc;
        desc.type = type;
        desc.shape.type = CollisionShapeType::Box;
        desc.shape.half_extents = half_extents;
        desc.initial_transform.position = center;
        desc.initial_transform.rotation = rotation;
        desc.layer = type == RigidBodyType::Kinematic ? CollisionLayer::Kinematic : CollisionLayer::Static;
        desc.user_data = user_data;
        desc.is_trigger = trigger;
        if (trigger) desc.layer = CollisionLayer::Trigger;
        return world.CreateRigidBody(desc);
    }

    SharedPtr<CharacterController> AddCharacter(const Vector3& feet, void* user_data = nullptr) {
        CharacterSettings settings;
        settings.user_data = user_data;
        return world.CreateCharacterController(settings, feet);
    }

    void Simulate(CharacterController& character, const Vector3& wish, float32 seconds, float32 jump = 0.0f) {
        const int steps = static_cast<int>(std::lround(seconds / kDt));
        for (int i = 0; i < steps; ++i) {
            character.Move(wish, i == 0 ? jump : 0.0f, kDt);
            world.Step(kDt);
        }
    }

    PhysicsWorld world;
};

Math::Quaternion RotationZ(float32 degrees) {
    const float32 half = degrees * 3.14159265f / 360.0f;
    return Math::Quaternion(0.0f, 0.0f, std::sin(half), std::cos(half));
}

} // namespace

TEST_F(PhysicsTest, CharacterFallsAndLands) {
    auto character = AddCharacter({0, 3, 0});
    ASSERT_TRUE(character);
    EXPECT_FALSE(character->IsGrounded());

    Simulate(*character, Vector3::Zero, 1.5f);
    EXPECT_TRUE(character->IsGrounded());
    EXPECT_NEAR(character->GetPosition().y, 0.0f, 0.03f);
    EXPECT_FLOAT_EQ(character->GetVelocity().y, 0.0f);
}

TEST_F(PhysicsTest, WalkingStopsAtWall) {
    AddBox({3, 1.5f, 0}, {0.5f, 1.5f, 5});   // wall face at x = 2.5
    auto character = AddCharacter({0, 0, 0});
    Simulate(*character, Vector3::Zero, 0.2f);

    Simulate(*character, {5.5f, 0, 0}, 2.0f);
    EXPECT_NEAR(character->GetPosition().x, 2.5f - 0.35f, 0.05f);
    EXPECT_LT(character->GetPosition().x, 2.5f - 0.35f + 1e-3f);
    EXPECT_TRUE(character->IsGrounded());
}

TEST_F(PhysicsTest, SlidesAlongWall) {
    AddBox({3, 1.5f, 0}, {0.5f, 1.5f, 20});
    auto character = AddCharacter({0, 0, 0});
    Simulate(*character, {4.0f, 0, 4.0f}, 2.0f);
    EXPECT_LT(character->GetPosition().x, 2.2f);
    EXPECT_GT(character->GetPosition().z, 6.0f);   // keeps moving along the wall
}

TEST_F(PhysicsTest, StepsUpLowLedgeButNotHighOne) {
    AddBox({3, 0.15f, 0}, {1.0f, 0.15f, 2.0f});   // 0.3 m step, x 2..4
    auto character = AddCharacter({0, 0, 0});
    Simulate(*character, {3.0f, 0, 0}, 1.2f);
    EXPECT_NEAR(character->GetPosition().y, 0.3f, 0.03f);
    EXPECT_GT(character->GetPosition().x, 2.0f);

    AddBox({3, 0.3f, 6}, {1.0f, 0.3f, 2.0f});     // 0.6 m block
    auto blocked = AddCharacter({0, 0, 6});
    Simulate(*blocked, {3.0f, 0, 0}, 1.2f);
    EXPECT_NEAR(blocked->GetPosition().y, 0.0f, 0.03f);
    EXPECT_LT(blocked->GetPosition().x, 2.0f - 0.3f);
}

TEST_F(PhysicsTest, JumpHeightMatchesJumpSpeed) {
    auto character = AddCharacter({0, 0, 0});
    Simulate(*character, Vector3::Zero, 0.1f);
    ASSERT_TRUE(character->IsGrounded());

    const float32 gravity = character->GetSettings().gravity;
    const float32 jump_speed = std::sqrt(2.0f * gravity * 1.0f);   // 1 m apex
    float32 apex = 0.0f;
    character->Move(Vector3::Zero, jump_speed, kDt);
    for (int i = 0; i < 120; ++i) {
        apex = std::max(apex, character->GetPosition().y);
        character->Move(Vector3::Zero, 0.0f, kDt);
    }
    EXPECT_NEAR(apex, 1.0f, 0.06f);
    EXPECT_TRUE(character->IsGrounded());
}

TEST_F(PhysicsTest, CeilingStopsJump) {
    AddBox({0, 2.5f, 0}, {3, 0.25f, 3});   // ceiling underside at 2.25
    auto character = AddCharacter({0, 0, 0});
    Simulate(*character, Vector3::Zero, 0.1f);
    float32 top = 0.0f;
    character->Move(Vector3::Zero, 10.0f, kDt);
    for (int i = 0; i < 60; ++i) {
        top = std::max(top, character->GetPosition().y + 1.8f);
        character->Move(Vector3::Zero, 0.0f, kDt);
    }
    EXPECT_LE(top, 2.25f + 1e-3f);
}

TEST_F(PhysicsTest, WalkableAndSteepSlopes) {
    // 30 degree ramp rising towards +x
    AddBox({4, 0, 0}, {3, 0.2f, 1.5f}, RigidBodyType::Static, nullptr, false, RotationZ(30.0f));
    auto walker = AddCharacter({0, 0, 0});
    Simulate(*walker, {3.0f, 0, 0}, 2.0f);   // ramp surface meets the floor at x = 3.6
    EXPECT_GT(walker->GetPosition().y, 0.8f);
    EXPECT_TRUE(walker->IsGrounded());

    // 65 degree ramp cannot be climbed
    AddBox({4, 0, 8}, {3, 0.2f, 1.5f}, RigidBodyType::Static, nullptr, false, RotationZ(65.0f));
    auto blocked = AddCharacter({0, 0, 8});
    Simulate(*blocked, {3.0f, 0, 0}, 2.0f);
    EXPECT_LT(blocked->GetPosition().y, 0.4f);
}

TEST_F(PhysicsTest, KinematicWallPushesAndCrushes) {
    auto character = AddCharacter({0, 0, 0});
    Simulate(*character, Vector3::Zero, 0.1f);

    auto wall = AddBox({-1.0f, 1.5f, 0}, {0.25f, 1.5f, 3}, RigidBodyType::Kinematic);
    for (int i = 0; i < 60; ++i) {   // wall moves +x at 1.5 m/s for 1 s
        Math::Transform t = wall->GetTransform();
        t.position.x += 1.5f * kDt;
        wall->SetTransform(t);
        world.Step(kDt);
        character->Move(Vector3::Zero, 0.0f, kDt);
        EXPECT_FALSE(character->IsCrushed());
    }
    EXPECT_NEAR(character->GetPosition().x, wall->GetPosition().x + 0.25f + 0.35f, 0.05f);

    // Second wall closing from the other side
    auto other = AddBox({character->GetPosition().x + 1.2f, 1.5f, 0}, {0.25f, 1.5f, 3}, RigidBodyType::Kinematic);
    bool crushed = false;
    for (int i = 0; i < 120 && !crushed; ++i) {
        Math::Transform t = other->GetTransform();
        t.position.x -= 1.5f * kDt;
        other->SetTransform(t);
        world.Step(kDt);
        character->Move(Vector3::Zero, 0.0f, kDt);
        crushed = character->IsCrushed();
    }
    EXPECT_TRUE(crushed);
}

TEST_F(PhysicsTest, MovementIsDeterministic) {
    PhysicsWorld other;
    ASSERT_TRUE(other.Initialize());
    auto make_level = [](PhysicsWorld& w) {
        for (const auto& [c, h] : {std::pair<Vector3, Vector3>{{0, -0.5f, 0}, {50, 0.5f, 50}},
                                   {{3, 1, 1}, {0.5f, 1, 2}},
                                   {{-2, 0.15f, 4}, {1, 0.15f, 1}}}) {
            RigidBodyDesc desc;
            desc.type = RigidBodyType::Static;
            desc.shape.half_extents = h;
            desc.initial_transform.position = c;
            w.CreateRigidBody(desc);
        }
    };
    PhysicsWorld a;
    ASSERT_TRUE(a.Initialize());
    make_level(a);
    make_level(other);
    auto ca = a.CreateCharacterController({}, {0, 1, 0});
    auto cb = other.CreateCharacterController({}, {0, 1, 0});
    for (int i = 0; i < 300; ++i) {
        const Vector3 wish(std::sin(i * 0.05f) * 5.0f, 0, std::cos(i * 0.03f) * 5.0f);
        const float32 jump = i % 90 == 0 ? 7.0f : 0.0f;
        ca->Move(wish, jump, kDt);
        cb->Move(wish, jump, kDt);
    }
    EXPECT_EQ(ca->GetPosition().x, cb->GetPosition().x);
    EXPECT_EQ(ca->GetPosition().y, cb->GetPosition().y);
    EXPECT_EQ(ca->GetPosition().z, cb->GetPosition().z);

    // Re-simulation from a saved state reproduces the result (client-side prediction)
    CharacterState saved = ca->GetState();
    for (int i = 0; i < 30; ++i) ca->Move({3, 0, 0}, 0.0f, kDt);
    const Vector3 first = ca->GetPosition();
    ca->SetState(saved);
    for (int i = 0; i < 30; ++i) ca->Move({3, 0, 0}, 0.0f, kDt);
    EXPECT_EQ(first.x, ca->GetPosition().x);
    EXPECT_EQ(first.z, ca->GetPosition().z);
}

TEST_F(PhysicsTest, RaycastFiltersAndUserData) {
    int wall_tag = 1, trigger_tag = 2, player_tag = 3;
    AddBox({0, 1, -5}, {2, 1, 0.25f}, RigidBodyType::Static, &wall_tag);
    AddBox({0, 1, -2}, {2, 1, 0.25f}, RigidBodyType::Static, &trigger_tag, true);
    auto character = AddCharacter({0, 0, -3.5f}, &player_tag);

    RaycastResult hit = world.Raycast({0, 1, 0}, {0, 1, -10});
    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.body_user_data, &player_tag);   // trigger skipped, character hit first
    EXPECT_EQ(hit.character, character.get());

    QueryFilter ignore_player;
    ignore_player.ignore_character = character.get();
    hit = world.Raycast({0, 1, 0}, {0, 1, -10}, ignore_player);
    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.body_user_data, &wall_tag);
    EXPECT_NEAR(hit.distance, 4.75f, 1e-3f);
    EXPECT_NEAR(hit.normal.z, 1.0f, 1e-3f);

    QueryFilter with_triggers;
    with_triggers.include_triggers = true;
    const auto all = world.RaycastAll({0, 1, 0}, {0, 1, -10}, with_triggers);
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].body_user_data, &trigger_tag);
    EXPECT_EQ(all[2].body_user_data, &wall_tag);

    QueryFilter static_only;
    static_only.mask = CollisionLayer::Character;
    EXPECT_EQ(world.Raycast({0, 1, 0}, {0, 1, -10}, static_only).character, character.get());

    EXPECT_FALSE(world.Raycast({0, 5, 0}, {0, 5, -10}).hit);
    EXPECT_TRUE(world.SphereCast({0, 1, 0}, {0, 1, -10}, 0.2f, ignore_player).hit);
    EXPECT_EQ(world.OverlapSphere({0, 1, -5}, 0.5f).size(), 1u);
}

namespace {
struct EventRecorder : ICollisionCallback {
    int begins = 0, ends = 0, trigger_enters = 0, trigger_exits = 0;
    void* last_trigger_other = nullptr;
    void OnCollisionBegin(void*, void*, const ContactPoint&) override { ++begins; }
    void OnCollisionEnd(void*, void*) override { ++ends; }
    void OnTriggerEnter(void*, void* other) override { ++trigger_enters; last_trigger_other = other; }
    void OnTriggerExit(void*, void*) override { ++trigger_exits; }
};
} // namespace

TEST_F(PhysicsTest, TriggerAndCollisionEvents) {
    EventRecorder recorder;
    world.SetCollisionCallback(&recorder);

    int zone_tag = 0, player_tag = 0;
    AddBox({5, 1, 0}, {1, 1, 1}, RigidBodyType::Static, &zone_tag, true);
    auto character = AddCharacter({0, 0, 0}, &player_tag);
    Simulate(*character, {4, 0, 0}, 2.0f);   // walks through the zone (triggers do not block)
    EXPECT_GT(character->GetPosition().x, 7.0f);
    Simulate(*character, Vector3::Zero, 0.2f);
    EXPECT_EQ(recorder.trigger_enters, 1);
    EXPECT_EQ(recorder.trigger_exits, 1);
    EXPECT_EQ(recorder.last_trigger_other, &player_tag);

    RigidBodyDesc ball;
    ball.shape.type = CollisionShapeType::Sphere;
    ball.shape.radius = 0.25f;
    ball.layer = CollisionLayer::Dynamic;
    ball.initial_transform.position = {-5, 2, 0};
    auto body = world.CreateRigidBody(ball);
    for (int i = 0; i < 120; ++i) world.Step(kDt);
    EXPECT_NEAR(body->GetPosition().y, 0.25f, 0.02f);
    EXPECT_GE(recorder.begins, 1);
    world.DestroyRigidBody(body);
    EXPECT_EQ(world.GetRigidBodyCount(), 2u);
    world.SetCollisionCallback(nullptr);
}

TEST_F(PhysicsTest, ClimbsAndDescendsStairsGrounded) {
    // 6 steps of 0.25 m rise and 0.3 m depth going +x, then a platform
    for (int i = 0; i < 6; ++i) {
        const float32 height = 0.25f * static_cast<float32>(i + 1);
        AddBox({2.0f + 0.3f * static_cast<float32>(i) + 0.15f, height * 0.5f, 0}, {0.15f, height * 0.5f, 1.0f});
    }
    AddBox({5.3f, 0.75f, 0}, {1.5f, 0.75f, 1.0f});   // platform at 1.5 m, x 3.8 .. 6.8

    auto character = AddCharacter({0, 0, 0});
    Simulate(*character, {3.0f, 0, 0}, 2.0f);
    EXPECT_NEAR(character->GetPosition().y, 1.5f, 0.03f);
    EXPECT_TRUE(character->IsGrounded());

    // Walk back down: stays grounded on every frame (no falling between steps)
    int airborne_frames = 0;
    for (int i = 0; i < 120; ++i) {
        character->Move({-3.0f, 0, 0}, 0.0f, kDt);
        if (!character->IsGrounded()) ++airborne_frames;
    }
    EXPECT_NEAR(character->GetPosition().y, 0.0f, 0.03f);
    EXPECT_EQ(airborne_frames, 0);
}

TEST_F(PhysicsTest, WalksOffHighLedgeAndFalls) {
    AddBox({0, 1.0f, 0}, {2.0f, 1.0f, 2.0f});   // 2 m high block
    auto character = AddCharacter({0, 2.0f, 0});
    Simulate(*character, Vector3::Zero, 0.1f);
    ASSERT_TRUE(character->IsGrounded());

    bool left_ground = false;
    for (int i = 0; i < 90; ++i) {
        character->Move({4.0f, 0, 0}, 0.0f, kDt);
        left_ground |= !character->IsGrounded();
    }
    EXPECT_TRUE(left_ground);
    EXPECT_NEAR(character->GetPosition().y, 0.0f, 0.03f);
    EXPECT_TRUE(character->IsGrounded());
}

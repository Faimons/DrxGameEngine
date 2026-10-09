/**
 * DRX Engine - Gameplay Systems Unit Tests
 */

#include <gtest/gtest.h>
#include <cmath>

#include "Core/Public/Log.h"
#include "Animation/Public/AnimationSystem.h"
#include "Particles/Public/ParticleSystem.h"
#include "UI/Public/UISystem.h"

using namespace DRX;

// ============================================================================
// Logger
// ============================================================================

TEST(LoggerTest, FormatsStringsSafely) {
    // Strings passed through the logger must be type-safe (no varargs UB)
    const String name = "Player";
    const int32 health = 100;
    EXPECT_NO_THROW(DRX_LOG_DEBUG("{} has {} hp ({:.1f}%)", name, health, 99.5f));
    EXPECT_NO_THROW(DRX_LOG_DEBUG("no arguments"));
}

TEST(LoggerTest, LevelFilter) {
    Logger& logger = Logger::Instance();
    const LogLevel previous = logger.GetLevel();

    logger.SetLevel(LogLevel::Error);
    EXPECT_FALSE(logger.IsEnabled(LogLevel::Info));
    EXPECT_TRUE(logger.IsEnabled(LogLevel::Fatal));

    logger.SetLevel(previous);
}

// ============================================================================
// Animation
// ============================================================================

namespace {

SharedPtr<Animation::Skeleton> MakeArmSkeleton() {
    // root at origin, mid one unit up, end two units up (straight arm along +Y)
    auto skeleton = MakeShared<Animation::Skeleton>();

    Animation::Bone root;
    root.name = "root";
    EXPECT_EQ(skeleton->AddBone(root), 0);

    Animation::Bone mid;
    mid.name = "mid";
    mid.parent_index = 0;
    mid.bind_pose.position = Math::Vector3(0, 1, 0);
    EXPECT_EQ(skeleton->AddBone(mid), 1);

    Animation::Bone end;
    end.name = "end";
    end.parent_index = 1;
    end.bind_pose.position = Math::Vector3(0, 1, 0);
    EXPECT_EQ(skeleton->AddBone(end), 2);

    skeleton->CalculateInverseBindPoses();
    return skeleton;
}

SharedPtr<Animation::AnimationClip> MakeMoveClip(const String& name, float32 end_x) {
    auto clip = MakeShared<Animation::AnimationClip>();
    clip->name = name;
    clip->loop = false;

    Animation::BoneAnimationTrack track;
    track.bone_name = "root";
    track.position_keys = {{0.0f, Math::Vector3(0, 0, 0)}, {1.0f, Math::Vector3(end_x, 0, 0)}};
    clip->AddTrack(track);
    return clip;
}

Math::Vector3 Translation(const Math::Matrix4& m) {
    return Math::Vector3(m[3][0], m[3][1], m[3][2]);
}

} // namespace

TEST(AnimationTest, SkeletonRejectsChildBeforeParent) {
    Animation::Skeleton skeleton;
    Animation::Bone orphan;
    orphan.parent_index = 3;
    EXPECT_EQ(skeleton.AddBone(orphan), -1);
    EXPECT_EQ(skeleton.GetBoneCount(), 0);
}

TEST(AnimationTest, BindPoseSkinMatricesAreIdentity) {
    Animation::Animator animator;
    animator.SetSkeleton(MakeArmSkeleton());
    animator.Update(0.016f);

    for (const auto& m : animator.GetSkinMatrices()) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                EXPECT_NEAR(m[c][r], c == r ? 1.0f : 0.0f, 1e-4f);
            }
        }
    }
}

TEST(AnimationTest, ClipSamplingInterpolates) {
    auto skeleton = MakeArmSkeleton();
    auto clip = MakeMoveClip("move", 2.0f);
    clip->BindToSkeleton(*skeleton);
    EXPECT_FLOAT_EQ(clip->duration, 1.0f);

    Animation::Pose pose;
    skeleton->GetBindPose(pose);
    clip->Sample(0.5f, pose);
    EXPECT_NEAR(pose[0].position.x, 1.0f, 1e-4f);

    // Untracked bones keep their bind pose
    EXPECT_NEAR(pose[1].position.y, 1.0f, 1e-4f);

    // Non-looping clips clamp at the end
    clip->Sample(5.0f, pose);
    EXPECT_NEAR(pose[0].position.x, 2.0f, 1e-4f);
}

TEST(AnimationTest, AnimatorAppliesClipToGlobals) {
    auto skeleton = MakeArmSkeleton();
    Animation::Animator animator;
    animator.SetSkeleton(skeleton);
    animator.PlayClip(MakeMoveClip("move", 2.0f), 0.0f);

    animator.Update(0.5f);
    ASSERT_EQ(animator.GetGlobalMatrices().size(), 3u);
    const Math::Vector3 end = Translation(animator.GetGlobalMatrices()[2]);
    EXPECT_NEAR(end.x, 1.0f, 1e-4f);
    EXPECT_NEAR(end.y, 2.0f, 1e-4f);
}

TEST(AnimationTest, StateMachineTransitionsOnParameter) {
    auto skeleton = MakeArmSkeleton();

    auto idle = MakeShared<Animation::AnimatorState>();
    idle->name = "idle";
    idle->clip = MakeMoveClip("idle", 0.0f);
    idle->clip->BindToSkeleton(*skeleton);

    auto run = MakeShared<Animation::AnimatorState>();
    run->name = "run";
    run->clip = MakeMoveClip("run", 4.0f);
    run->clip->BindToSkeleton(*skeleton);

    auto machine = MakeShared<Animation::AnimatorStateMachine>();
    machine->AddState(idle);
    machine->AddState(run);

    Animation::StateTransition transition;
    transition.from_state = "idle";
    transition.to_state = "run";
    transition.duration = 0.2f;
    transition.conditions.push_back({"speed", Animation::TransitionCondition::Comparison::Greater, 0.5f});
    machine->AddTransition(transition);

    ASSERT_NE(machine->GetCurrentState(), nullptr);
    EXPECT_EQ(machine->GetCurrentState()->name, "idle");

    machine->Update(0.1f);
    EXPECT_FALSE(machine->IsTransitioning());

    machine->SetParameter("speed", 1.0f);
    machine->Update(0.1f);
    EXPECT_TRUE(machine->IsTransitioning());

    machine->Update(0.3f);
    EXPECT_FALSE(machine->IsTransitioning());
    EXPECT_EQ(machine->GetCurrentState()->name, "run");
}

TEST(AnimationTest, TriggerIsConsumed) {
    auto machine = MakeShared<Animation::AnimatorStateMachine>();
    auto a = MakeShared<Animation::AnimatorState>();
    a->name = "a";
    auto b = MakeShared<Animation::AnimatorState>();
    b->name = "b";
    machine->AddState(a);
    machine->AddState(b);

    Animation::StateTransition to_b;
    to_b.from_state = "a";
    to_b.to_state = "b";
    to_b.duration = 0.0f;
    to_b.conditions.push_back({"fire", Animation::TransitionCondition::Comparison::Greater, 0.5f});
    machine->AddTransition(to_b);

    machine->SetTrigger("fire");
    machine->Update(0.016f);
    EXPECT_EQ(machine->GetCurrentState()->name, "b");
    EXPECT_FLOAT_EQ(machine->GetParameter("fire"), 0.0f);
}

TEST(AnimationTest, TwoBoneIKReachesTarget) {
    auto skeleton = MakeArmSkeleton();
    Animation::Pose pose;
    skeleton->GetBindPose(pose);

    Animation::TwoBoneIKSolver solver;
    solver.root_bone_index = 0;
    solver.mid_bone_index = 1;
    solver.end_bone_index = 2;
    solver.pole_target = Math::Vector3(0, 1, 1);

    const Math::Vector3 target(1.0f, 1.0f, 0.0f);
    ASSERT_TRUE(solver.Solve(*skeleton, pose, target));

    Array<Math::Matrix4> globals;
    skeleton->ComputeGlobalMatrices(pose, globals);
    const Math::Vector3 end = Translation(globals[2]);
    EXPECT_NEAR(end.x, target.x, 1e-3f);
    EXPECT_NEAR(end.y, target.y, 1e-3f);
    EXPECT_NEAR(end.z, target.z, 1e-3f);

    // Elbow bends towards the pole target (+Z)
    EXPECT_GT(Translation(globals[1]).z, 0.1f);

    // Bone lengths are preserved
    EXPECT_NEAR((Translation(globals[1]) - Translation(globals[0])).Length(), 1.0f, 1e-3f);
    EXPECT_NEAR((end - Translation(globals[1])).Length(), 1.0f, 1e-3f);
}

TEST(AnimationTest, TwoBoneIKReachesTargetsFromBentPose) {
    auto skeleton = MakeArmSkeleton();
    const Math::Vector3 targets[] = {
        {0.5f, 0.5f, 1.0f}, {-1.2f, 0.3f, 0.4f}, {0.2f, -1.5f, 0.1f}, {0.0f, 1.0f, -1.0f}};

    for (const auto& target : targets) {
        Animation::Pose pose;
        skeleton->GetBindPose(pose);
        pose[1].rotation = Math::Quaternion::FromAxisAngle(Math::Vector3::Right, 0.6f);  // pre-bent elbow

        Animation::TwoBoneIKSolver solver;
        solver.root_bone_index = 0;
        solver.mid_bone_index = 1;
        solver.end_bone_index = 2;
        solver.pole_target = Math::Vector3(0, 1, 1);
        ASSERT_TRUE(solver.Solve(*skeleton, pose, target));

        Array<Math::Matrix4> globals;
        skeleton->ComputeGlobalMatrices(pose, globals);
        const Math::Vector3 end = Translation(globals[2]);
        EXPECT_NEAR((end - target).Length(), 0.0f, 1e-3f);
    }
}

TEST(AnimationTest, FABRIKReachesTarget) {
    Array<Math::Vector3> chain = {Math::Vector3(0, 0, 0), Math::Vector3(0, 1, 0), Math::Vector3(0, 2, 0)};
    Animation::FABRIKSolver solver;
    const Math::Vector3 target(1.0f, 1.0f, 0.0f);

    EXPECT_TRUE(solver.Solve(chain, target));
    EXPECT_NEAR((chain.back() - target).Length(), 0.0f, 1e-2f);
    EXPECT_NEAR((chain[1] - chain[0]).Length(), 1.0f, 1e-3f);
}

// ============================================================================
// Particles
// ============================================================================

TEST(ParticleTest, BurstRespectsCapacity) {
    Particles::ParticleEmitter emitter;
    emitter.SetRandomSeed(42);
    emitter.SetMaxParticles(10);
    emitter.SetEmissionRate(0.0f);
    emitter.Emit(25);
    EXPECT_EQ(emitter.GetParticleCount(), 10u);
}

TEST(ParticleTest, ParticlesExpire) {
    Particles::ParticleEmitter emitter;
    emitter.SetRandomSeed(7);
    emitter.SetEmissionRate(0.0f);
    emitter.lifetime = {0.5f, 0.5f};
    emitter.Emit(20);
    ASSERT_EQ(emitter.GetParticleCount(), 20u);

    emitter.Update(0.25f);
    EXPECT_EQ(emitter.GetParticleCount(), 20u);

    emitter.Update(0.3f);
    EXPECT_EQ(emitter.GetParticleCount(), 0u);
}

TEST(ParticleTest, ContinuousEmissionRate) {
    Particles::ParticleEmitter emitter;
    emitter.SetRandomSeed(1);
    emitter.SetEmissionRate(100.0f);
    emitter.lifetime = {10.0f, 10.0f};
    emitter.SetDuration(0.0f, true);
    emitter.Play();

    for (int i = 0; i < 10; ++i) {
        emitter.Update(0.01f);
    }
    EXPECT_NEAR(static_cast<float>(emitter.GetParticleCount()), 10.0f, 1.0f);
}

TEST(ParticleTest, GravityPullsDown) {
    Particles::ParticleEmitter emitter;
    emitter.SetRandomSeed(3);
    emitter.SetEmissionRate(0.0f);
    emitter.start_speed = {0.0f, 0.0f};
    emitter.lifetime = {5.0f, 5.0f};
    emitter.AddModule<Particles::ForceModule>();
    emitter.Emit(1);

    emitter.Update(1.0f);
    EXPECT_LT(emitter.GetParticles().velocities[0].y, -9.0f);
    EXPECT_LT(emitter.GetParticles().positions[0].y, -9.0f);
}

TEST(ParticleTest, GroundCollisionKeepsParticlesAbove) {
    Particles::ParticleEmitter emitter;
    emitter.SetRandomSeed(5);
    emitter.SetEmissionRate(0.0f);
    emitter.start_speed = {0.0f, 0.0f};
    emitter.start_size = {0.0f, 0.0f};
    emitter.lifetime = {5.0f, 5.0f};
    emitter.position = Math::Vector3(0, 1, 0);
    emitter.AddModule<Particles::ForceModule>();
    emitter.AddModule<Particles::CollisionModule>();
    emitter.Emit(1);

    for (int i = 0; i < 120; ++i) {
        emitter.Update(1.0f / 60.0f);
        EXPECT_GE(emitter.GetParticles().positions[0].y, -0.2f);
    }
}

TEST(ParticleTest, LoadFromJson) {
    const String json = R"({
        "emitters": [{
            "max_particles": 64,
            "emission_rate": 0,
            "burst": [16, 0],
            "lifetime": [0.2, 0.4],
            "shape": "cone",
            "angle": 15,
            "gravity": [0, -9.81, 0],
            "color_over_lifetime": [[0, 1, 1, 1, 1], [1, 1, 0.5, 0, 0]],
            "size_over_lifetime": [[0, 1], [1, 0]]
        }]
    })";

    auto system = Particles::ParticleManager::Instance().LoadFromString(json, "muzzle");
    ASSERT_NE(system, nullptr);
    ASSERT_EQ(system->GetEmitters().size(), 1u);
    EXPECT_EQ(system->GetEmitters()[0]->GetMaxParticles(), 64u);

    system->Play();
    system->Update(0.016f);
    EXPECT_EQ(system->GetTotalParticleCount(), 16u);

    Particles::ParticleManager::Instance().DestroySystem(system);
}

TEST(ParticleTest, InvalidJsonIsRejected) {
    EXPECT_EQ(Particles::ParticleManager::Instance().LoadFromString("{ not json", "broken"), nullptr);
}

// ============================================================================
// UI
// ============================================================================

class UITest : public ::testing::Test {
protected:
    void SetUp() override {
        ui().Shutdown();
        ui().Initialize(800.0f, 600.0f);
        ui().SetMouseButton(0, false);
        ui().SetMousePosition(-100.0f, -100.0f);
    }

    static UI::UIContext& ui() { return UI::UIContext::Instance(); }
};

TEST_F(UITest, ButtonClicksOnRelease) {
    // Frame 1: press inside the button
    ui().SetMousePosition(10.0f, 10.0f);
    ui().SetMouseButton(0, true);
    ui().BeginFrame();
    EXPECT_FALSE(ui().Button("Play"));
    ui().EndFrame();

    // Frame 2: release inside -> click
    ui().SetMouseButton(0, false);
    ui().BeginFrame();
    EXPECT_TRUE(ui().Button("Play"));
    ui().EndFrame();

    // Frame 3: nothing happens anymore
    ui().BeginFrame();
    EXPECT_FALSE(ui().Button("Play"));
    ui().EndFrame();
}

TEST_F(UITest, SameLinePlacesItemsHorizontally) {
    ui().BeginFrame();
    ui().Button("A");
    ui().SameLine();
    ui().Button("B");
    ui().EndFrame();

    Array<const UI::DrawCommand*> button_rects;
    for (const auto& cmd : ui().GetDrawCommands()) {
        if (cmd.type == UI::DrawCommandType::Rect) {
            button_rects.push_back(&cmd);
        }
    }
    ASSERT_EQ(button_rects.size(), 2u);
    EXPECT_FLOAT_EQ(button_rects[0]->rect.y, button_rects[1]->rect.y);
    EXPECT_GT(button_rects[1]->rect.x, button_rects[0]->rect.x + button_rects[0]->rect.width);
}

TEST_F(UITest, InputFloatRejectsGarbage) {
    float32 value = 1.5f;
    const UI::Rect field_hit = {5.0f, 25.0f, 10.0f, 10.0f};

    // Focus the field
    ui().SetMousePosition(field_hit.x, field_hit.y);
    ui().SetMouseButton(0, true);
    ui().BeginFrame();
    ui().InputFloat("Value", value);
    ui().EndFrame();
    ui().SetMouseButton(0, false);
    EXPECT_TRUE(ui().WantCaptureKeyboard());

    // Replace text with garbage and submit
    for (int i = 0; i < 10; ++i) {
        ui().SetKeyState(UI::UIKey::Backspace, true);
        ui().BeginFrame();
        ui().InputFloat("Value", value);
        ui().EndFrame();
    }
    ui().AddCharacter('a');
    ui().AddCharacter('b');
    ui().SetKeyState(UI::UIKey::Enter, true);
    ui().BeginFrame();
    EXPECT_FALSE(ui().InputFloat("Value", value));
    ui().EndFrame();

    EXPECT_FLOAT_EQ(value, 1.5f);
    EXPECT_FALSE(ui().WantCaptureKeyboard());
}

TEST_F(UITest, SliderClampsValue) {
    float32 value = 50.0f;
    ui().BeginFrame();
    ui().SliderFloat("Volume", value, 0.0f, 1.0f);
    ui().EndFrame();
    EXPECT_FLOAT_EQ(value, 1.0f);
}

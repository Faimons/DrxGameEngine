/**
 * DRX Engine - Animation System
 * Skeletal animation with blending, state machines and IK
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Matrix4.h"
#include "Core/Public/Math/Transform.h"

namespace DRX::Animation {

/**
 * Local transform of a single bone
 */
struct BoneTransform {
    Math::Vector3 position = Math::Vector3::Zero;
    Math::Quaternion rotation = Math::Quaternion::Identity();
    Math::Vector3 scale = Math::Vector3::One;

    Math::Matrix4 ToMatrix() const {
        return Math::Transform(position, rotation, scale).ToMatrix();
    }

    static BoneTransform Lerp(const BoneTransform& a, const BoneTransform& b, float32 t);
};

using Pose = Array<BoneTransform>;

/**
 * Bone data (bind pose in parent space)
 */
struct Bone {
    String name;
    int32 parent_index = -1;
    BoneTransform bind_pose;
    Math::Matrix4 inverse_bind_pose = Math::Matrix4::Identity();
};

/**
 * Skeleton - immutable bone hierarchy shared between animators.
 * Bones are stored parent-before-child.
 */
class Skeleton {
public:
    bool LoadFromFile(const String& path);

    // Returns the new bone index, or -1 if the parent index is invalid
    int32 AddBone(const Bone& bone);

    int32 GetBoneCount() const { return static_cast<int32>(bones_.size()); }
    const Bone& GetBone(int32 index) const { return bones_[static_cast<size_t>(index)]; }
    const Array<Bone>& GetBones() const { return bones_; }

    int32 FindBoneIndex(const String& name) const;
    const Bone* FindBone(const String& name) const;

    // Recomputes inverse bind poses from the bind pose hierarchy
    void CalculateInverseBindPoses();

    void GetBindPose(Pose& out_pose) const;
    void ComputeGlobalMatrices(const Pose& pose, Array<Math::Matrix4>& out_globals) const;

private:
    Array<Bone> bones_;
    HashMap<String, int32> bone_lookup_;
};

/**
 * Keyframes
 */
struct VectorKeyframe {
    float32 time = 0.0f;
    Math::Vector3 value;
};

struct QuaternionKeyframe {
    float32 time = 0.0f;
    Math::Quaternion value;
};

/**
 * Animation track for one bone (keys sorted by time, in seconds)
 */
struct BoneAnimationTrack {
    String bone_name;
    int32 bone_index = -1;

    Array<VectorKeyframe> position_keys;
    Array<QuaternionKeyframe> rotation_keys;
    Array<VectorKeyframe> scale_keys;
};

/**
 * Animation clip - collection of bone tracks
 */
class AnimationClip {
public:
    // Loads the animation at `animation_index` from a model file
    bool LoadFromFile(const String& path, uint32 animation_index = 0);

    void AddTrack(const BoneAnimationTrack& track);
    void BindToSkeleton(const Skeleton& skeleton);

    // Overwrites the tracked bones of `pose`; untracked bones keep their value.
    void Sample(float32 time, Pose& pose) const;

    // Maps an arbitrary time into the clip's range (wraps when looping)
    float32 WrapTime(float32 time) const;

    String name;
    float32 duration = 0.0f;
    bool loop = true;

    const Array<BoneAnimationTrack>& GetTracks() const { return tracks_; }

private:
    Array<BoneAnimationTrack> tracks_;
};

/**
 * Pose blending helpers
 */
void BlendPoses(const Pose& a, const Pose& b, float32 t, Pose& out);
void AddAdditivePose(Pose& base, const Pose& additive, const Pose& reference, float32 weight);

/**
 * Animator state - plays a single clip
 */
struct AnimatorState {
    String name;
    SharedPtr<AnimationClip> clip;
    float32 speed = 1.0f;
    float32 current_time = 0.0f;

    void Enter();
    void Update(float32 delta_time);
    void Exit();
    void Sample(const Skeleton& skeleton, Pose& out_pose) const;

    // 0..1 progress through the clip
    float32 GetNormalizedTime() const;
};

/**
 * State machine transitions
 */
struct TransitionCondition {
    enum class Comparison : uint8 {
        Equal,
        NotEqual,
        Greater,
        Less,
        GreaterEqual,
        LessEqual
    };

    String parameter_name;
    Comparison comparison = Comparison::Greater;
    float32 threshold = 0.0f;

    bool Evaluate(float32 value) const;
};

struct StateTransition {
    String from_state;   // empty = any state
    String to_state;
    float32 duration = 0.25f;
    bool has_exit_time = false;
    float32 exit_time = 0.9f;  // normalized time
    Array<TransitionCondition> conditions;
};

/**
 * Animator state machine
 */
class AnimatorStateMachine {
public:
    void Update(float32 delta_time);

    void AddState(SharedPtr<AnimatorState> state);
    void RemoveState(const String& name);
    AnimatorState* GetState(const String& name);
    const AnimatorState* GetCurrentState() const { return current_state_; }

    void AddTransition(const StateTransition& transition);

    void SetState(const String& name);
    void TransitionTo(const String& name, float32 duration);
    bool IsTransitioning() const { return transitioning_; }

    void Sample(const Skeleton& skeleton, Pose& out_pose) const;

    void SetParameter(const String& name, float32 value);
    float32 GetParameter(const String& name) const;
    void SetTrigger(const String& name);

private:
    void CheckTransitions();

    HashMap<String, SharedPtr<AnimatorState>> states_;
    Array<StateTransition> transitions_;
    HashMap<String, float32> parameters_;
    HashMap<String, bool> triggers_;

    AnimatorState* current_state_ = nullptr;
    AnimatorState* next_state_ = nullptr;
    bool transitioning_ = false;
    float32 transition_time_ = 0.0f;
    float32 transition_duration_ = 0.0f;
};

/**
 * 1D blend tree (e.g. idle -> walk -> run by speed)
 */
class BlendTree1D {
public:
    void AddNode(SharedPtr<AnimationClip> clip, float32 threshold);
    void SetParameter(float32 value) { blend_parameter_ = value; }
    float32 GetParameter() const { return blend_parameter_; }

    void Update(float32 delta_time);
    void Sample(const Skeleton& skeleton, Pose& out_pose) const;

private:
    struct Node {
        SharedPtr<AnimationClip> clip;
        float32 threshold = 0.0f;
        float32 current_time = 0.0f;
    };

    Array<Node> nodes_;
    float32 blend_parameter_ = 0.0f;
};

/**
 * 2D blend tree (inverse distance weighting, e.g. strafe directions)
 */
class BlendTree2D {
public:
    void AddNode(SharedPtr<AnimationClip> clip, float32 x, float32 y);
    void SetParameters(float32 x, float32 y) { blend_x_ = x; blend_y_ = y; }

    void Update(float32 delta_time);
    void Sample(const Skeleton& skeleton, Pose& out_pose) const;

private:
    struct Node {
        SharedPtr<AnimationClip> clip;
        float32 x = 0.0f;
        float32 y = 0.0f;
        float32 current_time = 0.0f;
    };

    Array<Node> nodes_;
    float32 blend_x_ = 0.0f;
    float32 blend_y_ = 0.0f;
};

/**
 * Base clip plus additive layer (e.g. recoil on top of aim)
 */
class AdditiveBlend {
public:
    void SetBaseClip(SharedPtr<AnimationClip> clip) { base_clip_ = std::move(clip); base_time_ = 0.0f; }
    void SetAdditiveClip(SharedPtr<AnimationClip> clip) { additive_clip_ = std::move(clip); additive_time_ = 0.0f; }
    void SetWeight(float32 weight) { blend_weight_ = weight; }

    void Update(float32 delta_time);
    void Sample(const Skeleton& skeleton, Pose& out_pose) const;

private:
    SharedPtr<AnimationClip> base_clip_;
    SharedPtr<AnimationClip> additive_clip_;
    float32 base_time_ = 0.0f;
    float32 additive_time_ = 0.0f;
    float32 blend_weight_ = 1.0f;
};

/**
 * Two-bone IK (arms, legs). Operates on a pose, never on the shared skeleton.
 */
class TwoBoneIKSolver {
public:
    int32 root_bone_index = -1;
    int32 mid_bone_index = -1;
    int32 end_bone_index = -1;
    Math::Vector3 pole_target;   // model space hint for the bend direction
    float32 weight = 1.0f;

    bool Solve(const Skeleton& skeleton, Pose& pose, const Math::Vector3& target) const;
};

/**
 * FABRIK chain solver on model-space positions
 */
class FABRIKSolver {
public:
    int32 max_iterations = 10;
    float32 tolerance = 0.001f;

    bool Solve(Array<Math::Vector3>& chain, const Math::Vector3& target) const;
};

/**
 * Rotates a bone towards a model-space target with angle limits (radians)
 */
class LookAtSolver {
public:
    int32 bone_index = -1;
    Math::Vector3 local_forward = Math::Vector3::Forward;
    float32 max_angle = 1.2f;
    float32 weight = 1.0f;

    bool Solve(const Skeleton& skeleton, Pose& pose, const Math::Vector3& target) const;
};

/**
 * Animator - evaluates a state machine on a skeleton and produces skin matrices
 */
class Animator {
public:
    void SetSkeleton(SharedPtr<Skeleton> skeleton);
    SharedPtr<Skeleton> GetSkeleton() const { return skeleton_; }

    void SetStateMachine(SharedPtr<AnimatorStateMachine> state_machine);
    SharedPtr<AnimatorStateMachine> GetStateMachine() const { return state_machine_; }

    // Plays a clip, cross-fading from the current state
    void PlayClip(SharedPtr<AnimationClip> clip, float32 blend_time = 0.2f);

    void SetTrigger(const String& name);
    void SetFloat(const String& name, float32 value);
    void SetBool(const String& name, bool value);
    void SetInteger(const String& name, int32 value);

    void AddTwoBoneIK(const TwoBoneIKSolver& solver, const Math::Vector3& target);
    void ClearIK();

    void Update(float32 delta_time);

    const Pose& GetLocalPose() const { return local_pose_; }
    const Array<Math::Matrix4>& GetGlobalMatrices() const { return global_matrices_; }
    const Array<Math::Matrix4>& GetSkinMatrices() const { return skin_matrices_; }

private:
    struct IKTarget {
        TwoBoneIKSolver solver;
        Math::Vector3 target;
    };

    void CalculateFinalMatrices();

    SharedPtr<Skeleton> skeleton_;
    SharedPtr<AnimatorStateMachine> state_machine_;
    Array<IKTarget> ik_targets_;

    Pose local_pose_;
    Array<Math::Matrix4> global_matrices_;
    Array<Math::Matrix4> skin_matrices_;
};

/**
 * Animation Manager - resource cache and animator updates
 */
class AnimationManager {
public:
    static AnimationManager& Instance();

    bool Initialize();
    void Shutdown();
    void Update(float32 delta_time);

    SharedPtr<Skeleton> LoadSkeleton(const String& path);
    SharedPtr<AnimationClip> LoadClip(const String& path, uint32 animation_index = 0);

    SharedPtr<Animator> CreateAnimator();
    void DestroyAnimator(const SharedPtr<Animator>& animator);

    void SetTimeScale(float32 scale) { time_scale_ = scale; }
    float32 GetTimeScale() const { return time_scale_; }

private:
    AnimationManager() = default;

    HashMap<String, SharedPtr<Skeleton>> skeletons_;
    HashMap<String, SharedPtr<AnimationClip>> clips_;
    Array<SharedPtr<Animator>> animators_;
    float32 time_scale_ = 1.0f;
};

} // namespace DRX::Animation

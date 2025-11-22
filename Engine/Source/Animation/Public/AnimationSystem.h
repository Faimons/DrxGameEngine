/**
 * DRX Engine - Animation System
 * Skeletal animation with blending and state machines
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Matrix4.h"
#include "Core/Public/Math/Transform.h"

namespace DRX::Animation {

// Forward declarations
class Skeleton;
class AnimationClip;
class Animator;

/**
 * Bone data
 */
struct Bone {
    String name;
    int32 parent_index = -1;
    Math::Transform local_bind_pose;
    Math::Matrix4 inverse_bind_matrix;
};

/**
 * Skeleton - bone hierarchy
 */
class Skeleton {
public:
    Skeleton() = default;
    ~Skeleton() = default;

    bool LoadFromFile(const String& path);

    int32 GetBoneCount() const { return static_cast<int32>(bones_.size()); }
    const Bone& GetBone(int32 index) const { return bones_[index]; }
    int32 FindBone(const String& name) const;

    const Array<Bone>& GetBones() const { return bones_; }
    const Array<Math::Matrix4>& GetInverseBindMatrices() const { return inverse_bind_matrices_; }

    void AddBone(const Bone& bone);
    void CalculateInverseBindMatrices();

private:
    Array<Bone> bones_;
    Array<Math::Matrix4> inverse_bind_matrices_;
    HashMap<String, int32> bone_name_to_index_;
};

/**
 * Keyframe data
 */
template<typename T>
struct Keyframe {
    float32 time;
    T value;
};

/**
 * Animation channel - animates a single bone
 */
struct AnimationChannel {
    String bone_name;
    int32 bone_index = -1;

    Array<Keyframe<Math::Vector3>> position_keys;
    Array<Keyframe<Math::Quaternion>> rotation_keys;
    Array<Keyframe<Math::Vector3>> scale_keys;

    Math::Vector3 SamplePosition(float32 time) const;
    Math::Quaternion SampleRotation(float32 time) const;
    Math::Vector3 SampleScale(float32 time) const;
    Math::Transform Sample(float32 time) const;
};

/**
 * Animation clip - collection of animation channels
 */
class AnimationClip {
public:
    AnimationClip() = default;
    ~AnimationClip() = default;

    bool LoadFromFile(const String& path);

    const String& GetName() const { return name_; }
    void SetName(const String& name) { name_ = name; }

    float32 GetDuration() const { return duration_; }
    float32 GetTicksPerSecond() const { return ticks_per_second_; }

    const Array<AnimationChannel>& GetChannels() const { return channels_; }
    AnimationChannel* GetChannel(const String& bone_name);

    void AddChannel(const AnimationChannel& channel);
    void BindToSkeleton(const Skeleton& skeleton);

    // Sample all channels at given time
    void Sample(float32 time, Array<Math::Transform>& out_transforms) const;

private:
    String name_;
    float32 duration_ = 0.0f;
    float32 ticks_per_second_ = 24.0f;
    Array<AnimationChannel> channels_;
    HashMap<String, int32> channel_map_;
};

/**
 * Animation pose - snapshot of skeleton state
 */
struct AnimationPose {
    Array<Math::Transform> local_transforms;
    Array<Math::Matrix4> global_matrices;
    Array<Math::Matrix4> skin_matrices;

    void Resize(int32 bone_count);
    void CalculateGlobalMatrices(const Skeleton& skeleton);
    void CalculateSkinMatrices(const Skeleton& skeleton);

    static AnimationPose Blend(const AnimationPose& a, const AnimationPose& b, float32 t);
    static AnimationPose AdditiveBlend(const AnimationPose& base, const AnimationPose& additive, float32 weight);
};

/**
 * Animation playback state
 */
struct AnimationState {
    SharedPtr<AnimationClip> clip;
    float32 time = 0.0f;
    float32 speed = 1.0f;
    float32 weight = 1.0f;
    bool looping = true;
    bool playing = true;

    void Update(float32 delta_time);
    void Reset();
    bool IsFinished() const;
    float32 GetNormalizedTime() const;
};

/**
 * Blend tree node types
 */
enum class BlendNodeType : uint32 {
    Clip,           // Single animation clip
    Blend1D,        // 1D blend space
    Blend2D,        // 2D blend space
    Additive,       // Additive blend
    Override,       // Override specific bones
    LayeredBlend    // Layer-based blending
};

/**
 * Blend tree node
 */
class BlendNode {
public:
    virtual ~BlendNode() = default;

    virtual BlendNodeType GetType() const = 0;
    virtual void Update(float32 delta_time) = 0;
    virtual void Evaluate(AnimationPose& out_pose) = 0;

    float32 GetWeight() const { return weight_; }
    void SetWeight(float32 weight) { weight_ = weight; }

protected:
    float32 weight_ = 1.0f;
};

/**
 * Clip node - plays single animation
 */
class ClipNode : public BlendNode {
public:
    ClipNode(SharedPtr<AnimationClip> clip);

    BlendNodeType GetType() const override { return BlendNodeType::Clip; }
    void Update(float32 delta_time) override;
    void Evaluate(AnimationPose& out_pose) override;

    AnimationState& GetState() { return state_; }

private:
    AnimationState state_;
    AnimationPose pose_;
};

/**
 * 1D Blend node - blends between clips based on single parameter
 */
class Blend1DNode : public BlendNode {
public:
    struct BlendPoint {
        SharedPtr<AnimationClip> clip;
        float32 position;
    };

    BlendNodeType GetType() const override { return BlendNodeType::Blend1D; }
    void Update(float32 delta_time) override;
    void Evaluate(AnimationPose& out_pose) override;

    void AddBlendPoint(SharedPtr<AnimationClip> clip, float32 position);
    void SetParameter(float32 value) { parameter_ = value; }
    float32 GetParameter() const { return parameter_; }

private:
    Array<BlendPoint> blend_points_;
    Array<AnimationState> states_;
    float32 parameter_ = 0.0f;
};

/**
 * 2D Blend node - blends based on two parameters
 */
class Blend2DNode : public BlendNode {
public:
    struct BlendPoint {
        SharedPtr<AnimationClip> clip;
        float32 x, y;
    };

    BlendNodeType GetType() const override { return BlendNodeType::Blend2D; }
    void Update(float32 delta_time) override;
    void Evaluate(AnimationPose& out_pose) override;

    void AddBlendPoint(SharedPtr<AnimationClip> clip, float32 x, float32 y);
    void SetParameters(float32 x, float32 y) { param_x_ = x; param_y_ = y; }

private:
    Array<BlendPoint> blend_points_;
    Array<AnimationState> states_;
    float32 param_x_ = 0.0f;
    float32 param_y_ = 0.0f;
};

/**
 * Animation state machine state
 */
struct AnimatorState {
    String name;
    UniquePtr<BlendNode> blend_node;
    float32 speed = 1.0f;

    // Transitions
    struct Transition {
        String target_state;
        float32 duration = 0.25f;
        bool has_exit_time = false;
        float32 exit_time = 0.9f;
        std::function<bool()> condition;
    };
    Array<Transition> transitions;
};

/**
 * Animation layer
 */
struct AnimationLayer {
    String name;
    float32 weight = 1.0f;
    bool additive = false;
    uint32 mask = 0xFFFFFFFF;  // Bone mask

    String current_state;
    String previous_state;
    float32 transition_time = 0.0f;
    float32 transition_duration = 0.0f;

    HashMap<String, UniquePtr<AnimatorState>> states;
};

/**
 * Animator - animation state machine
 */
class Animator {
public:
    Animator();
    ~Animator();

    void SetSkeleton(SharedPtr<Skeleton> skeleton);
    SharedPtr<Skeleton> GetSkeleton() const { return skeleton_; }

    // Layer management
    int32 AddLayer(const String& name, bool additive = false);
    AnimationLayer* GetLayer(int32 index);
    AnimationLayer* GetLayer(const String& name);

    // State management
    void AddState(int32 layer, const String& name, SharedPtr<AnimationClip> clip);
    void AddState(int32 layer, const String& name, UniquePtr<BlendNode> blend_node);
    void AddTransition(int32 layer, const String& from, const String& to,
                       float32 duration, std::function<bool()> condition = nullptr);

    // Playback
    void Play(const String& state_name, int32 layer = 0, float32 fade_duration = 0.25f);
    void CrossFade(const String& state_name, int32 layer = 0, float32 duration = 0.25f);
    void SetLayerWeight(int32 layer, float32 weight);

    // Parameters for blend trees
    void SetFloat(const String& name, float32 value);
    void SetBool(const String& name, bool value);
    void SetTrigger(const String& name);
    float32 GetFloat(const String& name) const;
    bool GetBool(const String& name) const;

    // Update
    void Update(float32 delta_time);

    // Output
    const AnimationPose& GetCurrentPose() const { return final_pose_; }
    const Array<Math::Matrix4>& GetSkinMatrices() const { return final_pose_.skin_matrices; }

    // Events
    using AnimationEventCallback = std::function<void(const String& event_name)>;
    void SetEventCallback(AnimationEventCallback callback) { event_callback_ = callback; }

private:
    void UpdateLayer(AnimationLayer& layer, float32 delta_time);
    void EvaluateLayer(AnimationLayer& layer, AnimationPose& out_pose);
    void CheckTransitions(AnimationLayer& layer);

    SharedPtr<Skeleton> skeleton_;
    Array<AnimationLayer> layers_;
    AnimationPose final_pose_;

    HashMap<String, float32> float_params_;
    HashMap<String, bool> bool_params_;
    HashMap<String, bool> triggers_;

    AnimationEventCallback event_callback_;
};

/**
 * Animation Manager
 */
class AnimationManager {
public:
    static AnimationManager& Instance();

    bool Initialize();
    void Shutdown();
    void Update(float32 delta_time);

    // Resources
    SharedPtr<Skeleton> LoadSkeleton(const String& path);
    SharedPtr<AnimationClip> LoadClip(const String& path);

    // Animators
    SharedPtr<Animator> CreateAnimator();
    void DestroyAnimator(SharedPtr<Animator> animator);

    // Global time scale
    void SetTimeScale(float32 scale) { time_scale_ = scale; }
    float32 GetTimeScale() const { return time_scale_; }

private:
    AnimationManager() = default;

    HashMap<String, SharedPtr<Skeleton>> skeletons_;
    HashMap<String, SharedPtr<AnimationClip>> clips_;
    Array<SharedPtr<Animator>> animators_;
    float32 time_scale_ = 1.0f;
};

/**
 * IK (Inverse Kinematics) Solver
 */
class IKSolver {
public:
    // Two-bone IK (e.g., arm, leg)
    static bool SolveTwoBone(
        const Math::Vector3& root_pos,
        const Math::Vector3& mid_pos,
        const Math::Vector3& end_pos,
        const Math::Vector3& target_pos,
        const Math::Vector3& pole_vector,
        Math::Quaternion& out_root_rotation,
        Math::Quaternion& out_mid_rotation
    );

    // FABRIK solver for chains
    static bool SolveFABRIK(
        Array<Math::Vector3>& chain_positions,
        const Array<float32>& bone_lengths,
        const Math::Vector3& target_pos,
        int32 max_iterations = 10,
        float32 tolerance = 0.001f
    );

    // Look-at IK
    static Math::Quaternion SolveLookAt(
        const Math::Vector3& current_forward,
        const Math::Vector3& target_direction,
        const Math::Vector3& up_vector = Math::Vector3::Up
    );
};

} // namespace DRX::Animation

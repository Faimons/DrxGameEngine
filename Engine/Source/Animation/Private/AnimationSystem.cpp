/**
 * DRX Engine - Animation System Implementation
 */

#include "Animation/Public/AnimationSystem.h"
#include <algorithm>
#include <cmath>

namespace DRX::Animation {

// ============================================================================
// Skeleton
// ============================================================================

int32 Skeleton::FindBoneIndex(const String& name) const {
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].name == name) {
            return static_cast<int32>(i);
        }
    }
    return -1;
}

Bone* Skeleton::FindBone(const String& name) {
    int32 idx = FindBoneIndex(name);
    return idx >= 0 ? &bones[idx] : nullptr;
}

void Skeleton::CalculateGlobalTransforms() {
    global_transforms.resize(bones.size());

    for (size_t i = 0; i < bones.size(); ++i) {
        const Bone& bone = bones[i];
        Math::Matrix4 local = Math::Matrix4::TRS(
            bone.local_position,
            bone.local_rotation,
            bone.local_scale
        );

        if (bone.parent_index >= 0) {
            global_transforms[i] = global_transforms[bone.parent_index] * local;
        } else {
            global_transforms[i] = local;
        }
    }
}

// ============================================================================
// AnimationClip
// ============================================================================

bool AnimationClip::Sample(float32 time, Array<BoneTransform>& out_transforms) const {
    out_transforms.resize(bone_tracks.size());

    float32 sample_time = time;
    if (loop && duration > 0) {
        sample_time = std::fmod(time, duration);
    }
    sample_time = std::clamp(sample_time, 0.0f, duration);

    for (size_t i = 0; i < bone_tracks.size(); ++i) {
        out_transforms[i] = SampleTrack(bone_tracks[i], sample_time);
    }

    return true;
}

BoneTransform AnimationClip::SampleTrack(const BoneAnimationTrack& track, float32 time) const {
    BoneTransform result;
    result.position = SampleVector3Track(track.position_keys, time);
    result.rotation = SampleQuaternionTrack(track.rotation_keys, time);
    result.scale = SampleVector3Track(track.scale_keys, time);
    return result;
}

Math::Vector3 AnimationClip::SampleVector3Track(const Array<VectorKeyframe>& keys, float32 time) const {
    if (keys.empty()) return Math::Vector3::Zero;
    if (keys.size() == 1) return keys[0].value;

    // Find surrounding keyframes
    size_t next_idx = 0;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].time > time) {
            next_idx = i;
            break;
        }
        next_idx = i;
    }

    size_t prev_idx = next_idx > 0 ? next_idx - 1 : 0;

    if (prev_idx == next_idx) return keys[prev_idx].value;

    const VectorKeyframe& prev = keys[prev_idx];
    const VectorKeyframe& next = keys[next_idx];

    float32 t = (time - prev.time) / (next.time - prev.time);

    return Math::Vector3::Lerp(prev.value, next.value, t);
}

Math::Quaternion AnimationClip::SampleQuaternionTrack(const Array<QuaternionKeyframe>& keys, float32 time) const {
    if (keys.empty()) return Math::Quaternion::Identity;
    if (keys.size() == 1) return keys[0].value;

    size_t next_idx = 0;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].time > time) {
            next_idx = i;
            break;
        }
        next_idx = i;
    }

    size_t prev_idx = next_idx > 0 ? next_idx - 1 : 0;

    if (prev_idx == next_idx) return keys[prev_idx].value;

    const QuaternionKeyframe& prev = keys[prev_idx];
    const QuaternionKeyframe& next = keys[next_idx];

    float32 t = (time - prev.time) / (next.time - prev.time);

    return Math::Quaternion::Slerp(prev.value, next.value, t);
}

// ============================================================================
// AnimatorState
// ============================================================================

void AnimatorState::Enter() {
    current_time = 0;
}

void AnimatorState::Update(float32 delta_time) {
    if (!clip) return;

    current_time += delta_time * speed;

    if (clip->loop) {
        current_time = std::fmod(current_time, clip->duration);
    } else {
        current_time = std::min(current_time, clip->duration);
    }
}

void AnimatorState::Exit() {
    // Cleanup if needed
}

bool AnimatorState::Sample(Array<BoneTransform>& out_transforms) const {
    if (!clip) return false;
    return clip->Sample(current_time, out_transforms);
}

// ============================================================================
// AnimatorStateMachine
// ============================================================================

void AnimatorStateMachine::Update(float32 delta_time) {
    // Update transition
    if (transitioning_ && current_state_ && next_state_) {
        transition_time_ += delta_time;
        float32 t = transition_time_ / transition_duration_;

        if (t >= 1.0f) {
            current_state_->Exit();
            current_state_ = next_state_;
            next_state_ = nullptr;
            transitioning_ = false;
        }
    }

    // Update current state
    if (current_state_) {
        current_state_->Update(delta_time);
    }

    // Check transitions
    CheckTransitions();
}

void AnimatorStateMachine::AddState(SharedPtr<AnimatorState> state) {
    states_[state->name] = state;

    if (!current_state_) {
        current_state_ = state.get();
        current_state_->Enter();
    }
}

void AnimatorStateMachine::RemoveState(const String& name) {
    states_.erase(name);
}

AnimatorState* AnimatorStateMachine::GetState(const String& name) {
    auto it = states_.find(name);
    return it != states_.end() ? it->second.get() : nullptr;
}

void AnimatorStateMachine::AddTransition(const StateTransition& transition) {
    transitions_.push_back(transition);
}

void AnimatorStateMachine::SetState(const String& name) {
    AnimatorState* state = GetState(name);
    if (state && state != current_state_) {
        if (current_state_) {
            current_state_->Exit();
        }
        current_state_ = state;
        current_state_->Enter();
    }
}

void AnimatorStateMachine::TransitionTo(const String& name, float32 duration) {
    AnimatorState* state = GetState(name);
    if (state && state != current_state_) {
        next_state_ = state;
        transition_duration_ = duration;
        transition_time_ = 0;
        transitioning_ = true;
        next_state_->Enter();
    }
}

bool AnimatorStateMachine::Sample(Array<BoneTransform>& out_transforms) const {
    if (!current_state_) return false;

    if (transitioning_ && next_state_) {
        Array<BoneTransform> current_pose, next_pose;
        current_state_->Sample(current_pose);
        next_state_->Sample(next_pose);

        float32 t = transition_time_ / transition_duration_;
        BlendPoses(current_pose, next_pose, t, out_transforms);
        return true;
    }

    return current_state_->Sample(out_transforms);
}

void AnimatorStateMachine::SetParameter(const String& name, float32 value) {
    parameters_[name] = value;
}

float32 AnimatorStateMachine::GetParameter(const String& name) const {
    auto it = parameters_.find(name);
    return it != parameters_.end() ? it->second : 0.0f;
}

void AnimatorStateMachine::CheckTransitions() {
    if (!current_state_ || transitioning_) return;

    for (const auto& transition : transitions_) {
        if (transition.from_state != current_state_->name) continue;

        bool should_transition = true;
        for (const auto& condition : transition.conditions) {
            float32 param_value = GetParameter(condition.parameter_name);

            switch (condition.comparison) {
                case TransitionCondition::Comparison::Equal:
                    should_transition &= (param_value == condition.threshold);
                    break;
                case TransitionCondition::Comparison::NotEqual:
                    should_transition &= (param_value != condition.threshold);
                    break;
                case TransitionCondition::Comparison::Greater:
                    should_transition &= (param_value > condition.threshold);
                    break;
                case TransitionCondition::Comparison::Less:
                    should_transition &= (param_value < condition.threshold);
                    break;
                case TransitionCondition::Comparison::GreaterEqual:
                    should_transition &= (param_value >= condition.threshold);
                    break;
                case TransitionCondition::Comparison::LessEqual:
                    should_transition &= (param_value <= condition.threshold);
                    break;
            }
        }

        if (should_transition) {
            TransitionTo(transition.to_state, transition.duration);
            break;
        }
    }
}

void AnimatorStateMachine::BlendPoses(const Array<BoneTransform>& a, const Array<BoneTransform>& b,
                                       float32 t, Array<BoneTransform>& out) const {
    size_t count = std::min(a.size(), b.size());
    out.resize(count);

    for (size_t i = 0; i < count; ++i) {
        out[i].position = Math::Vector3::Lerp(a[i].position, b[i].position, t);
        out[i].rotation = Math::Quaternion::Slerp(a[i].rotation, b[i].rotation, t);
        out[i].scale = Math::Vector3::Lerp(a[i].scale, b[i].scale, t);
    }
}

// ============================================================================
// BlendTree
// ============================================================================

void BlendTree1D::Update(float32 delta_time) {
    // Update all nodes
    for (auto& node : nodes_) {
        if (node.clip) {
            node.current_time += delta_time;
            if (node.clip->loop && node.clip->duration > 0) {
                node.current_time = std::fmod(node.current_time, node.clip->duration);
            }
        }
    }
}

bool BlendTree1D::Sample(Array<BoneTransform>& out_transforms) const {
    if (nodes_.empty()) return false;
    if (nodes_.size() == 1) {
        return nodes_[0].clip->Sample(nodes_[0].current_time, out_transforms);
    }

    // Find surrounding nodes
    size_t lower = 0, upper = 0;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].threshold <= blend_parameter_) {
            lower = i;
        }
        if (nodes_[i].threshold >= blend_parameter_) {
            upper = i;
            break;
        }
        upper = i;
    }

    if (lower == upper) {
        return nodes_[lower].clip->Sample(nodes_[lower].current_time, out_transforms);
    }

    // Blend between nodes
    float32 range = nodes_[upper].threshold - nodes_[lower].threshold;
    float32 t = (blend_parameter_ - nodes_[lower].threshold) / range;

    Array<BoneTransform> pose_a, pose_b;
    nodes_[lower].clip->Sample(nodes_[lower].current_time, pose_a);
    nodes_[upper].clip->Sample(nodes_[upper].current_time, pose_b);

    out_transforms.resize(pose_a.size());
    for (size_t i = 0; i < pose_a.size(); ++i) {
        out_transforms[i].position = Math::Vector3::Lerp(pose_a[i].position, pose_b[i].position, t);
        out_transforms[i].rotation = Math::Quaternion::Slerp(pose_a[i].rotation, pose_b[i].rotation, t);
        out_transforms[i].scale = Math::Vector3::Lerp(pose_a[i].scale, pose_b[i].scale, t);
    }

    return true;
}

void BlendTree1D::AddNode(SharedPtr<AnimationClip> clip, float32 threshold) {
    BlendNode node;
    node.clip = clip;
    node.threshold = threshold;
    node.current_time = 0;
    nodes_.push_back(node);

    // Sort by threshold
    std::sort(nodes_.begin(), nodes_.end(),
        [](const BlendNode& a, const BlendNode& b) { return a.threshold < b.threshold; });
}

// ============================================================================
// BlendTree2D
// ============================================================================

void BlendTree2D::Update(float32 delta_time) {
    for (auto& node : nodes_) {
        if (node.clip) {
            node.current_time += delta_time;
            if (node.clip->loop && node.clip->duration > 0) {
                node.current_time = std::fmod(node.current_time, node.clip->duration);
            }
        }
    }
}

bool BlendTree2D::Sample(Array<BoneTransform>& out_transforms) const {
    if (nodes_.empty()) return false;

    // Calculate weights using gradient band interpolation
    Array<float32> weights(nodes_.size(), 0.0f);
    float32 total_weight = 0;

    for (size_t i = 0; i < nodes_.size(); ++i) {
        float32 dx = blend_x_ - nodes_[i].position_x;
        float32 dy = blend_y_ - nodes_[i].position_y;
        float32 dist = std::sqrt(dx * dx + dy * dy);

        // Inverse distance weighting
        if (dist < 0.001f) {
            weights[i] = 1000.0f;
        } else {
            weights[i] = 1.0f / (dist * dist);
        }
        total_weight += weights[i];
    }

    // Normalize weights
    if (total_weight > 0) {
        for (auto& w : weights) {
            w /= total_weight;
        }
    }

    // Blend poses
    bool first = true;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (weights[i] < 0.001f) continue;

        Array<BoneTransform> pose;
        nodes_[i].clip->Sample(nodes_[i].current_time, pose);

        if (first) {
            out_transforms.resize(pose.size());
            for (size_t j = 0; j < pose.size(); ++j) {
                out_transforms[j].position = pose[j].position * weights[i];
                out_transforms[j].rotation = pose[j].rotation;  // Will slerp later
                out_transforms[j].scale = pose[j].scale * weights[i];
            }
            first = false;
        } else {
            for (size_t j = 0; j < pose.size(); ++j) {
                out_transforms[j].position = out_transforms[j].position + pose[j].position * weights[i];
                out_transforms[j].scale = out_transforms[j].scale + pose[j].scale * weights[i];
            }
        }
    }

    return true;
}

void BlendTree2D::AddNode(SharedPtr<AnimationClip> clip, float32 x, float32 y) {
    BlendNode2D node;
    node.clip = clip;
    node.position_x = x;
    node.position_y = y;
    node.current_time = 0;
    nodes_.push_back(node);
}

// ============================================================================
// AdditiveBlend
// ============================================================================

void AdditiveBlend::Update(float32 delta_time) {
    base_time_ += delta_time;
    additive_time_ += delta_time;

    if (base_clip_ && base_clip_->loop && base_clip_->duration > 0) {
        base_time_ = std::fmod(base_time_, base_clip_->duration);
    }
    if (additive_clip_ && additive_clip_->loop && additive_clip_->duration > 0) {
        additive_time_ = std::fmod(additive_time_, additive_clip_->duration);
    }
}

bool AdditiveBlend::Sample(Array<BoneTransform>& out_transforms) const {
    if (!base_clip_) return false;

    base_clip_->Sample(base_time_, out_transforms);

    if (additive_clip_ && blend_weight_ > 0) {
        Array<BoneTransform> additive_pose;
        additive_clip_->Sample(additive_time_, additive_pose);

        for (size_t i = 0; i < std::min(out_transforms.size(), additive_pose.size()); ++i) {
            out_transforms[i].position = out_transforms[i].position +
                additive_pose[i].position * blend_weight_;
            out_transforms[i].rotation = Math::Quaternion::Slerp(
                out_transforms[i].rotation,
                out_transforms[i].rotation * additive_pose[i].rotation,
                blend_weight_
            );
        }
    }

    return true;
}

// ============================================================================
// TwoBoneIKSolver
// ============================================================================

bool TwoBoneIKSolver::Solve(Skeleton& skeleton, const Math::Vector3& target) {
    if (root_bone_index < 0 || mid_bone_index < 0 || end_bone_index < 0) return false;

    Bone& root = skeleton.bones[root_bone_index];
    Bone& mid = skeleton.bones[mid_bone_index];
    Bone& end = skeleton.bones[end_bone_index];

    // Get global positions
    skeleton.CalculateGlobalTransforms();
    Math::Vector3 root_pos = skeleton.global_transforms[root_bone_index].GetTranslation();
    Math::Vector3 mid_pos = skeleton.global_transforms[mid_bone_index].GetTranslation();
    Math::Vector3 end_pos = skeleton.global_transforms[end_bone_index].GetTranslation();

    // Calculate bone lengths
    float32 upper_len = (mid_pos - root_pos).Length();
    float32 lower_len = (end_pos - mid_pos).Length();

    // Direction to target
    Math::Vector3 to_target = target - root_pos;
    float32 target_dist = to_target.Length();

    // Clamp target distance to reachable range
    float32 max_reach = upper_len + lower_len - 0.001f;
    float32 min_reach = std::abs(upper_len - lower_len) + 0.001f;
    target_dist = std::clamp(target_dist, min_reach, max_reach);

    // Calculate angles using law of cosines
    float32 cos_mid = (upper_len * upper_len + lower_len * lower_len - target_dist * target_dist) /
                      (2.0f * upper_len * lower_len);
    cos_mid = std::clamp(cos_mid, -1.0f, 1.0f);
    float32 mid_angle = std::acos(cos_mid);

    float32 cos_root = (target_dist * target_dist + upper_len * upper_len - lower_len * lower_len) /
                       (2.0f * target_dist * upper_len);
    cos_root = std::clamp(cos_root, -1.0f, 1.0f);
    float32 root_angle = std::acos(cos_root);

    // Apply rotations
    Math::Vector3 target_dir = to_target.Normalized();
    Math::Vector3 pole_dir = (pole_target - root_pos).Normalized();

    // Calculate rotation plane
    Math::Vector3 plane_normal = Math::Vector3::Cross(target_dir, pole_dir).Normalized();

    // Root rotation
    Math::Quaternion root_rot = Math::Quaternion::FromAxisAngle(plane_normal, root_angle);
    root.local_rotation = root_rot * root.local_rotation;

    // Mid rotation
    Math::Quaternion mid_rot = Math::Quaternion::FromAxisAngle(plane_normal, 3.14159265359f - mid_angle);
    mid.local_rotation = mid_rot * mid.local_rotation;

    return true;
}

// ============================================================================
// FABRIKSolver
// ============================================================================

bool FABRIKSolver::Solve(Array<Math::Vector3>& chain, const Math::Vector3& target) {
    if (chain.size() < 2) return false;

    // Calculate bone lengths
    Array<float32> lengths(chain.size() - 1);
    float32 total_length = 0;
    for (size_t i = 0; i < chain.size() - 1; ++i) {
        lengths[i] = (chain[i + 1] - chain[i]).Length();
        total_length += lengths[i];
    }

    Math::Vector3 root_pos = chain[0];
    float32 target_dist = (target - root_pos).Length();

    // Check if target is reachable
    if (target_dist > total_length) {
        // Stretch towards target
        Math::Vector3 dir = (target - root_pos).Normalized();
        for (size_t i = 0; i < chain.size() - 1; ++i) {
            chain[i + 1] = chain[i] + dir * lengths[i];
        }
        return true;
    }

    // FABRIK iterations
    for (int32 iter = 0; iter < max_iterations; ++iter) {
        // Forward reaching
        chain.back() = target;
        for (int32 i = static_cast<int32>(chain.size()) - 2; i >= 0; --i) {
            Math::Vector3 dir = (chain[i] - chain[i + 1]).Normalized();
            chain[i] = chain[i + 1] + dir * lengths[i];
        }

        // Backward reaching
        chain[0] = root_pos;
        for (size_t i = 0; i < chain.size() - 1; ++i) {
            Math::Vector3 dir = (chain[i + 1] - chain[i]).Normalized();
            chain[i + 1] = chain[i] + dir * lengths[i];
        }

        // Check convergence
        float32 error = (chain.back() - target).Length();
        if (error < tolerance) {
            return true;
        }
    }

    return true;
}

// ============================================================================
// LookAtSolver
// ============================================================================

bool LookAtSolver::Solve(Bone& bone, const Math::Vector3& target, const Math::Vector3& bone_world_pos) {
    Math::Vector3 to_target = (target - bone_world_pos).Normalized();
    Math::Vector3 forward = bone.local_rotation.Rotate(Math::Vector3::Forward);

    // Calculate rotation to look at target
    Math::Quaternion look_rot = Math::Quaternion::FromToRotation(forward, to_target);

    // Apply weight
    look_rot = Math::Quaternion::Slerp(Math::Quaternion::Identity, look_rot, weight);

    // Apply constraints
    Math::Vector3 euler = look_rot.ToEuler();
    euler.x = std::clamp(euler.x, -max_angle_x, max_angle_x);
    euler.y = std::clamp(euler.y, -max_angle_y, max_angle_y);

    bone.local_rotation = Math::Quaternion::FromEuler(euler) * bone.local_rotation;

    return true;
}

// ============================================================================
// Animator
// ============================================================================

void Animator::Update(float32 delta_time) {
    if (!skeleton_) return;

    if (state_machine_) {
        state_machine_->Update(delta_time);
        state_machine_->Sample(bone_transforms_);
    }

    // Apply IK
    ApplyIK();

    // Calculate final matrices
    CalculateFinalMatrices();
}

void Animator::SetSkeleton(SharedPtr<Skeleton> skeleton) {
    skeleton_ = skeleton;
    if (skeleton_) {
        bone_transforms_.resize(skeleton_->bones.size());
        final_matrices_.resize(skeleton_->bones.size());
    }
}

void Animator::SetStateMachine(SharedPtr<AnimatorStateMachine> state_machine) {
    state_machine_ = state_machine;
}

void Animator::PlayClip(SharedPtr<AnimationClip> clip, float32 blend_time) {
    if (!state_machine_) {
        state_machine_ = std::make_shared<AnimatorStateMachine>();
    }

    auto state = std::make_shared<AnimatorState>();
    state->name = clip->name;
    state->clip = clip;
    state_machine_->AddState(state);
    state_machine_->TransitionTo(clip->name, blend_time);
}

void Animator::SetTrigger(const String& name) {
    if (state_machine_) {
        state_machine_->SetParameter(name, 1.0f);
    }
}

void Animator::SetFloat(const String& name, float32 value) {
    if (state_machine_) {
        state_machine_->SetParameter(name, value);
    }
}

void Animator::SetBool(const String& name, bool value) {
    if (state_machine_) {
        state_machine_->SetParameter(name, value ? 1.0f : 0.0f);
    }
}

void Animator::SetInteger(const String& name, int32 value) {
    if (state_machine_) {
        state_machine_->SetParameter(name, static_cast<float32>(value));
    }
}

void Animator::ApplyIK() {
    // Apply any registered IK solvers
}

void Animator::CalculateFinalMatrices() {
    if (!skeleton_) return;

    skeleton_->CalculateGlobalTransforms();

    for (size_t i = 0; i < skeleton_->bones.size(); ++i) {
        final_matrices_[i] = skeleton_->global_transforms[i] * skeleton_->bones[i].inverse_bind_pose;
    }
}

// ============================================================================
// AnimationManager
// ============================================================================

AnimationManager& AnimationManager::Instance() {
    static AnimationManager instance;
    return instance;
}

bool AnimationManager::Initialize() {
    return true;
}

void AnimationManager::Shutdown() {
    clips_.clear();
    skeletons_.clear();
    animators_.clear();
}

void AnimationManager::Update(float32 delta_time) {
    for (auto& animator : animators_) {
        animator->Update(delta_time);
    }
}

SharedPtr<AnimationClip> AnimationManager::LoadClip(const String& path) {
    auto it = clips_.find(path);
    if (it != clips_.end()) {
        return it->second;
    }

    // Load clip from file (would implement actual loading)
    auto clip = std::make_shared<AnimationClip>();
    clip->name = path;
    clips_[path] = clip;

    return clip;
}

SharedPtr<Skeleton> AnimationManager::LoadSkeleton(const String& path) {
    auto it = skeletons_.find(path);
    if (it != skeletons_.end()) {
        return it->second;
    }

    // Load skeleton from file
    auto skeleton = std::make_shared<Skeleton>();
    skeletons_[path] = skeleton;

    return skeleton;
}

SharedPtr<Animator> AnimationManager::CreateAnimator() {
    auto animator = std::make_shared<Animator>();
    animators_.push_back(animator);
    return animator;
}

void AnimationManager::DestroyAnimator(SharedPtr<Animator> animator) {
    animators_.erase(
        std::remove(animators_.begin(), animators_.end(), animator),
        animators_.end()
    );
}

} // namespace DRX::Animation

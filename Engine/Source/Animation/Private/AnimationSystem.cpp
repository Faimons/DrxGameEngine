/**
 * DRX Engine - Animation System Implementation
 */

#include "Animation/Public/AnimationSystem.h"
#include "Core/Public/Log.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <algorithm>
#include <cmath>

namespace DRX::Animation {

namespace {

constexpr float32 kEpsilon = 1e-5f;

Math::Matrix4 ToMatrix4(const aiMatrix4x4& m) {
    // Assimp is row-major, the engine is column-major (m[col][row])
    Math::Matrix4 result;
    result[0][0] = m.a1; result[1][0] = m.a2; result[2][0] = m.a3; result[3][0] = m.a4;
    result[0][1] = m.b1; result[1][1] = m.b2; result[2][1] = m.b3; result[3][1] = m.b4;
    result[0][2] = m.c1; result[1][2] = m.c2; result[2][2] = m.c3; result[3][2] = m.c4;
    result[0][3] = m.d1; result[1][3] = m.d2; result[2][3] = m.d3; result[3][3] = m.d4;
    return result;
}

BoneTransform Decompose(const aiMatrix4x4& m) {
    aiVector3D scaling;
    aiQuaternion rotation;
    aiVector3D position;
    m.Decompose(scaling, rotation, position);

    BoneTransform result;
    result.position = {position.x, position.y, position.z};
    result.rotation = Math::Quaternion(rotation.x, rotation.y, rotation.z, rotation.w).Normalized();
    result.scale = {scaling.x, scaling.y, scaling.z};
    return result;
}

// Slerp that always takes the short path
Math::Quaternion NLerpShortest(const Math::Quaternion& a, const Math::Quaternion& b, float32 t) {
    return Math::Quaternion::Slerp(a, b, t).Normalized();
}

template<typename Key>
std::pair<size_t, float32> FindKeySpan(const Array<Key>& keys, float32 time) {
    // keys.size() >= 2
    auto it = std::upper_bound(keys.begin(), keys.end(), time,
        [](float32 t, const Key& key) { return t < key.time; });

    if (it == keys.begin()) {
        return {0, 0.0f};
    }
    if (it == keys.end()) {
        return {keys.size() - 2, 1.0f};
    }

    const size_t next = static_cast<size_t>(it - keys.begin());
    const size_t prev = next - 1;
    const float32 span = keys[next].time - keys[prev].time;
    const float32 t = span > kEpsilon ? (time - keys[prev].time) / span : 0.0f;
    return {prev, std::clamp(t, 0.0f, 1.0f)};
}

Math::Vector3 SampleVectorKeys(const Array<VectorKeyframe>& keys, float32 time, const Math::Vector3& fallback) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1) return keys[0].value;

    const auto [index, t] = FindKeySpan(keys, time);
    return Math::Vector3::Lerp(keys[index].value, keys[index + 1].value, t);
}

Math::Quaternion SampleQuaternionKeys(const Array<QuaternionKeyframe>& keys, float32 time,
                                      const Math::Quaternion& fallback) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1) return keys[0].value;

    const auto [index, t] = FindKeySpan(keys, time);
    return NLerpShortest(keys[index].value, keys[index + 1].value, t);
}

float32 AdvanceClipTime(const AnimationClip* clip, float32 time, float32 delta_time) {
    if (!clip) return 0.0f;
    return clip->WrapTime(time + delta_time);
}

// Global rotation of every bone in the pose (assumes uniform scale for IK purposes)
void ComputeGlobalRotations(const Skeleton& skeleton, const Pose& pose, Array<Math::Quaternion>& out) {
    const auto& bones = skeleton.GetBones();
    out.resize(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        const int32 parent = bones[i].parent_index;
        out[i] = parent >= 0 ? (out[static_cast<size_t>(parent)] * pose[i].rotation).Normalized()
                             : pose[i].rotation;
    }
}

Math::Vector3 GetTranslation(const Math::Matrix4& m) {
    return Math::Vector3(m[3][0], m[3][1], m[3][2]);
}

float32 SafeAcos(float32 value) {
    return std::acos(std::clamp(value, -1.0f, 1.0f));
}

} // namespace

// ============================================================================
// BoneTransform
// ============================================================================

BoneTransform BoneTransform::Lerp(const BoneTransform& a, const BoneTransform& b, float32 t) {
    BoneTransform result;
    result.position = Math::Vector3::Lerp(a.position, b.position, t);
    result.rotation = NLerpShortest(a.rotation, b.rotation, t);
    result.scale = Math::Vector3::Lerp(a.scale, b.scale, t);
    return result;
}

// ============================================================================
// Skeleton
// ============================================================================

int32 Skeleton::AddBone(const Bone& bone) {
    const int32 index = static_cast<int32>(bones_.size());
    if (bone.parent_index >= index) {
        DRX_LOG_ERROR("Bone '{}' must be added after its parent", bone.name);
        return -1;
    }

    bones_.push_back(bone);
    if (!bone.name.empty()) {
        bone_lookup_.emplace(bone.name, index);
    }
    return index;
}

int32 Skeleton::FindBoneIndex(const String& name) const {
    auto it = bone_lookup_.find(name);
    return it != bone_lookup_.end() ? it->second : -1;
}

const Bone* Skeleton::FindBone(const String& name) const {
    const int32 index = FindBoneIndex(name);
    return index >= 0 ? &bones_[static_cast<size_t>(index)] : nullptr;
}

void Skeleton::GetBindPose(Pose& out_pose) const {
    out_pose.resize(bones_.size());
    for (size_t i = 0; i < bones_.size(); ++i) {
        out_pose[i] = bones_[i].bind_pose;
    }
}

void Skeleton::ComputeGlobalMatrices(const Pose& pose, Array<Math::Matrix4>& out_globals) const {
    out_globals.resize(bones_.size());
    for (size_t i = 0; i < bones_.size(); ++i) {
        const Math::Matrix4 local = i < pose.size() ? pose[i].ToMatrix() : bones_[i].bind_pose.ToMatrix();
        const int32 parent = bones_[i].parent_index;
        out_globals[i] = parent >= 0 ? out_globals[static_cast<size_t>(parent)] * local : local;
    }
}

void Skeleton::CalculateInverseBindPoses() {
    Pose bind_pose;
    GetBindPose(bind_pose);

    Array<Math::Matrix4> globals;
    ComputeGlobalMatrices(bind_pose, globals);

    for (size_t i = 0; i < bones_.size(); ++i) {
        bones_[i].inverse_bind_pose = globals[i].Inverse();
    }
}

bool Skeleton::LoadFromFile(const String& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_LimitBoneWeights);
    if (!scene || !scene->mRootNode) {
        DRX_LOG_ERROR("Failed to load skeleton {}: {}", path, importer.GetErrorString());
        return false;
    }

    bones_.clear();
    bone_lookup_.clear();

    // Offset matrices of skinned bones
    HashMap<String, Math::Matrix4> offsets;
    for (uint32 m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        for (uint32 b = 0; b < mesh->mNumBones; ++b) {
            offsets.emplace(mesh->mBones[b]->mName.C_Str(), ToMatrix4(mesh->mBones[b]->mOffsetMatrix));
        }
    }

    // Depth-first traversal keeps parents before children
    struct Pending { const aiNode* node; int32 parent; };
    Array<Pending> stack{{scene->mRootNode, -1}};
    while (!stack.empty()) {
        const Pending current = stack.back();
        stack.pop_back();

        Bone bone;
        bone.name = current.node->mName.C_Str();
        bone.parent_index = current.parent;
        bone.bind_pose = Decompose(current.node->mTransformation);
        const int32 index = AddBone(bone);

        for (uint32 c = current.node->mNumChildren; c > 0; --c) {
            stack.push_back({current.node->mChildren[c - 1], index});
        }
    }

    CalculateInverseBindPoses();
    for (auto& bone : bones_) {
        auto it = offsets.find(bone.name);
        if (it != offsets.end()) {
            bone.inverse_bind_pose = it->second;
        }
    }

    DRX_LOG_INFO("Loaded skeleton {} ({} bones, {} skinned)", path, bones_.size(), offsets.size());
    return !bones_.empty();
}

// ============================================================================
// AnimationClip
// ============================================================================

bool AnimationClip::LoadFromFile(const String& path, uint32 animation_index) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, 0);
    if (!scene || animation_index >= scene->mNumAnimations) {
        DRX_LOG_ERROR("Failed to load animation {} #{}: {}", path, animation_index,
                      scene ? "index out of range" : importer.GetErrorString());
        return false;
    }

    const aiAnimation* anim = scene->mAnimations[animation_index];
    const float64 ticks_per_second = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 25.0;

    name = anim->mName.length > 0 ? String(anim->mName.C_Str()) : path;
    duration = static_cast<float32>(anim->mDuration / ticks_per_second);
    tracks_.clear();

    for (uint32 c = 0; c < anim->mNumChannels; ++c) {
        const aiNodeAnim* channel = anim->mChannels[c];

        BoneAnimationTrack track;
        track.bone_name = channel->mNodeName.C_Str();

        track.position_keys.reserve(channel->mNumPositionKeys);
        for (uint32 k = 0; k < channel->mNumPositionKeys; ++k) {
            const auto& key = channel->mPositionKeys[k];
            track.position_keys.push_back({static_cast<float32>(key.mTime / ticks_per_second),
                                           {key.mValue.x, key.mValue.y, key.mValue.z}});
        }

        track.rotation_keys.reserve(channel->mNumRotationKeys);
        for (uint32 k = 0; k < channel->mNumRotationKeys; ++k) {
            const auto& key = channel->mRotationKeys[k];
            track.rotation_keys.push_back({static_cast<float32>(key.mTime / ticks_per_second),
                                           Math::Quaternion(key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w).Normalized()});
        }

        track.scale_keys.reserve(channel->mNumScalingKeys);
        for (uint32 k = 0; k < channel->mNumScalingKeys; ++k) {
            const auto& key = channel->mScalingKeys[k];
            track.scale_keys.push_back({static_cast<float32>(key.mTime / ticks_per_second),
                                        {key.mValue.x, key.mValue.y, key.mValue.z}});
        }

        AddTrack(track);
    }

    DRX_LOG_INFO("Loaded animation '{}' ({:.2f}s, {} tracks)", name, duration, tracks_.size());
    return true;
}

void AnimationClip::AddTrack(const BoneAnimationTrack& track) {
    BoneAnimationTrack sorted = track;
    auto by_time = [](const auto& a, const auto& b) { return a.time < b.time; };
    std::stable_sort(sorted.position_keys.begin(), sorted.position_keys.end(), by_time);
    std::stable_sort(sorted.rotation_keys.begin(), sorted.rotation_keys.end(), by_time);
    std::stable_sort(sorted.scale_keys.begin(), sorted.scale_keys.end(), by_time);

    auto last_time = [](const auto& keys) { return keys.empty() ? 0.0f : keys.back().time; };
    duration = std::max({duration, last_time(sorted.position_keys),
                         last_time(sorted.rotation_keys), last_time(sorted.scale_keys)});

    tracks_.push_back(std::move(sorted));
}

void AnimationClip::BindToSkeleton(const Skeleton& skeleton) {
    for (auto& track : tracks_) {
        track.bone_index = skeleton.FindBoneIndex(track.bone_name);
    }
}

float32 AnimationClip::WrapTime(float32 time) const {
    if (duration <= kEpsilon) {
        return 0.0f;
    }
    if (loop) {
        float32 wrapped = std::fmod(time, duration);
        return wrapped < 0.0f ? wrapped + duration : wrapped;
    }
    return std::clamp(time, 0.0f, duration);
}

void AnimationClip::Sample(float32 time, Pose& pose) const {
    const float32 sample_time = WrapTime(time);

    for (const auto& track : tracks_) {
        if (track.bone_index < 0 || static_cast<size_t>(track.bone_index) >= pose.size()) {
            continue;
        }

        BoneTransform& target = pose[static_cast<size_t>(track.bone_index)];
        target.position = SampleVectorKeys(track.position_keys, sample_time, target.position);
        target.rotation = SampleQuaternionKeys(track.rotation_keys, sample_time, target.rotation);
        target.scale = SampleVectorKeys(track.scale_keys, sample_time, target.scale);
    }
}

// ============================================================================
// Pose Blending
// ============================================================================

void BlendPoses(const Pose& a, const Pose& b, float32 t, Pose& out) {
    const size_t count = std::min(a.size(), b.size());
    out.resize(count);
    for (size_t i = 0; i < count; ++i) {
        out[i] = BoneTransform::Lerp(a[i], b[i], t);
    }
}

void AddAdditivePose(Pose& base, const Pose& additive, const Pose& reference, float32 weight) {
    const size_t count = std::min({base.size(), additive.size(), reference.size()});
    for (size_t i = 0; i < count; ++i) {
        const Math::Vector3 delta_position = additive[i].position - reference[i].position;
        const Math::Quaternion delta_rotation = (reference[i].rotation.Inverse() * additive[i].rotation).Normalized();

        base[i].position += delta_position * weight;
        base[i].rotation = (base[i].rotation *
            NLerpShortest(Math::Quaternion::Identity(), delta_rotation, weight)).Normalized();
    }
}

// ============================================================================
// AnimatorState
// ============================================================================

void AnimatorState::Enter() {
    current_time = 0.0f;
}

void AnimatorState::Update(float32 delta_time) {
    current_time = AdvanceClipTime(clip.get(), current_time, delta_time * speed);
}

void AnimatorState::Exit() {
}

void AnimatorState::Sample(const Skeleton& skeleton, Pose& out_pose) const {
    skeleton.GetBindPose(out_pose);
    if (clip) {
        clip->Sample(current_time, out_pose);
    }
}

float32 AnimatorState::GetNormalizedTime() const {
    if (!clip || clip->duration <= kEpsilon) {
        return 1.0f;
    }
    return std::clamp(current_time / clip->duration, 0.0f, 1.0f);
}

// ============================================================================
// AnimatorStateMachine
// ============================================================================

bool TransitionCondition::Evaluate(float32 value) const {
    switch (comparison) {
        case Comparison::Equal:        return std::abs(value - threshold) <= kEpsilon;
        case Comparison::NotEqual:     return std::abs(value - threshold) > kEpsilon;
        case Comparison::Greater:      return value > threshold;
        case Comparison::Less:         return value < threshold;
        case Comparison::GreaterEqual: return value >= threshold;
        case Comparison::LessEqual:    return value <= threshold;
    }
    return false;
}

void AnimatorStateMachine::Update(float32 delta_time) {
    if (current_state_) {
        current_state_->Update(delta_time);
    }

    if (transitioning_ && next_state_) {
        next_state_->Update(delta_time);
        transition_time_ += delta_time;

        if (transition_time_ >= transition_duration_) {
            if (current_state_) {
                current_state_->Exit();
            }
            current_state_ = next_state_;
            next_state_ = nullptr;
            transitioning_ = false;
        }
    }

    CheckTransitions();
}

void AnimatorStateMachine::AddState(SharedPtr<AnimatorState> state) {
    if (!state) return;

    AnimatorState* raw = state.get();
    states_[state->name] = std::move(state);

    if (!current_state_) {
        current_state_ = raw;
        current_state_->Enter();
    }
}

void AnimatorStateMachine::RemoveState(const String& name) {
    auto it = states_.find(name);
    if (it == states_.end()) return;

    AnimatorState* raw = it->second.get();
    if (current_state_ == raw) {
        current_state_ = next_state_;
        next_state_ = nullptr;
        transitioning_ = false;
    } else if (next_state_ == raw) {
        next_state_ = nullptr;
        transitioning_ = false;
    }
    states_.erase(it);
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
    if (!state) return;

    if (current_state_ && current_state_ != state) {
        current_state_->Exit();
    }
    current_state_ = state;
    current_state_->Enter();
    next_state_ = nullptr;
    transitioning_ = false;
}

void AnimatorStateMachine::TransitionTo(const String& name, float32 duration) {
    AnimatorState* state = GetState(name);
    if (!state || state == current_state_ || state == next_state_) return;

    if (!current_state_ || duration <= kEpsilon) {
        SetState(name);
        return;
    }

    if (transitioning_ && next_state_) {
        // Snap the in-flight transition before starting a new one
        current_state_->Exit();
        current_state_ = next_state_;
    }

    next_state_ = state;
    next_state_->Enter();
    transition_duration_ = duration;
    transition_time_ = 0.0f;
    transitioning_ = true;
}

void AnimatorStateMachine::Sample(const Skeleton& skeleton, Pose& out_pose) const {
    if (!current_state_) {
        skeleton.GetBindPose(out_pose);
        return;
    }

    if (transitioning_ && next_state_) {
        Pose current_pose;
        Pose next_pose;
        current_state_->Sample(skeleton, current_pose);
        next_state_->Sample(skeleton, next_pose);

        const float32 t = transition_duration_ > kEpsilon
            ? std::clamp(transition_time_ / transition_duration_, 0.0f, 1.0f)
            : 1.0f;
        BlendPoses(current_pose, next_pose, t, out_pose);
        return;
    }

    current_state_->Sample(skeleton, out_pose);
}

void AnimatorStateMachine::SetParameter(const String& name, float32 value) {
    parameters_[name] = value;
}

float32 AnimatorStateMachine::GetParameter(const String& name) const {
    auto trigger = triggers_.find(name);
    if (trigger != triggers_.end() && trigger->second) {
        return 1.0f;
    }
    auto it = parameters_.find(name);
    return it != parameters_.end() ? it->second : 0.0f;
}

void AnimatorStateMachine::SetTrigger(const String& name) {
    triggers_[name] = true;
}

void AnimatorStateMachine::CheckTransitions() {
    if (!current_state_ || transitioning_) return;

    for (const auto& transition : transitions_) {
        if (!transition.from_state.empty() && transition.from_state != current_state_->name) continue;
        if (transition.to_state == current_state_->name) continue;

        if (transition.has_exit_time && current_state_->GetNormalizedTime() < transition.exit_time) {
            continue;
        }

        const bool conditions_met = std::all_of(transition.conditions.begin(), transition.conditions.end(),
            [this](const TransitionCondition& condition) {
                return condition.Evaluate(GetParameter(condition.parameter_name));
            });

        if (conditions_met) {
            // Triggers are consumed by the transition that used them
            for (const auto& condition : transition.conditions) {
                triggers_.erase(condition.parameter_name);
            }
            TransitionTo(transition.to_state, transition.duration);
            break;
        }
    }
}

// ============================================================================
// BlendTree1D
// ============================================================================

void BlendTree1D::AddNode(SharedPtr<AnimationClip> clip, float32 threshold) {
    if (!clip) return;

    nodes_.push_back({std::move(clip), threshold, 0.0f});
    std::stable_sort(nodes_.begin(), nodes_.end(),
        [](const Node& a, const Node& b) { return a.threshold < b.threshold; });
}

void BlendTree1D::Update(float32 delta_time) {
    for (auto& node : nodes_) {
        node.current_time = AdvanceClipTime(node.clip.get(), node.current_time, delta_time);
    }
}

void BlendTree1D::Sample(const Skeleton& skeleton, Pose& out_pose) const {
    skeleton.GetBindPose(out_pose);
    if (nodes_.empty()) return;

    if (blend_parameter_ <= nodes_.front().threshold) {
        nodes_.front().clip->Sample(nodes_.front().current_time, out_pose);
        return;
    }
    if (blend_parameter_ >= nodes_.back().threshold) {
        nodes_.back().clip->Sample(nodes_.back().current_time, out_pose);
        return;
    }

    size_t upper = 1;
    while (upper < nodes_.size() && nodes_[upper].threshold < blend_parameter_) {
        ++upper;
    }
    const Node& lo = nodes_[upper - 1];
    const Node& hi = nodes_[upper];

    const float32 range = hi.threshold - lo.threshold;
    const float32 t = range > kEpsilon ? (blend_parameter_ - lo.threshold) / range : 0.0f;

    Pose pose_a = out_pose;
    Pose pose_b = out_pose;
    lo.clip->Sample(lo.current_time, pose_a);
    hi.clip->Sample(hi.current_time, pose_b);
    BlendPoses(pose_a, pose_b, t, out_pose);
}

// ============================================================================
// BlendTree2D
// ============================================================================

void BlendTree2D::AddNode(SharedPtr<AnimationClip> clip, float32 x, float32 y) {
    if (!clip) return;
    nodes_.push_back({std::move(clip), x, y, 0.0f});
}

void BlendTree2D::Update(float32 delta_time) {
    for (auto& node : nodes_) {
        node.current_time = AdvanceClipTime(node.clip.get(), node.current_time, delta_time);
    }
}

void BlendTree2D::Sample(const Skeleton& skeleton, Pose& out_pose) const {
    skeleton.GetBindPose(out_pose);
    if (nodes_.empty()) return;

    // Inverse distance weighting
    Array<float32> weights(nodes_.size(), 0.0f);
    float32 total_weight = 0.0f;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const float32 dx = blend_x_ - nodes_[i].x;
        const float32 dy = blend_y_ - nodes_[i].y;
        const float32 dist_sq = dx * dx + dy * dy;
        if (dist_sq < kEpsilon) {
            nodes_[i].clip->Sample(nodes_[i].current_time, out_pose);
            return;
        }
        weights[i] = 1.0f / dist_sq;
        total_weight += weights[i];
    }

    const Pose bind_pose = out_pose;
    Pose accumulated(bind_pose.size());
    Array<Math::Quaternion> reference_rotation(bind_pose.size());
    for (size_t b = 0; b < bind_pose.size(); ++b) {
        accumulated[b].position = Math::Vector3::Zero;
        accumulated[b].scale = Math::Vector3::Zero;
        accumulated[b].rotation = Math::Quaternion(0, 0, 0, 0);
    }

    bool first = true;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const float32 w = weights[i] / total_weight;
        if (w < 1e-4f) continue;

        Pose pose = bind_pose;
        nodes_[i].clip->Sample(nodes_[i].current_time, pose);

        for (size_t b = 0; b < pose.size(); ++b) {
            if (first) {
                reference_rotation[b] = pose[b].rotation;
            }
            // Keep all quaternions in the same hemisphere before averaging
            Math::Quaternion q = pose[b].rotation;
            if (Math::Quaternion::Dot(q, reference_rotation[b]) < 0.0f) {
                q = -q;
            }
            accumulated[b].position += pose[b].position * w;
            accumulated[b].scale += pose[b].scale * w;
            accumulated[b].rotation = accumulated[b].rotation + q * w;
        }
        first = false;
    }

    for (size_t b = 0; b < accumulated.size(); ++b) {
        accumulated[b].rotation = accumulated[b].rotation.Normalized();
    }
    out_pose = std::move(accumulated);
}

// ============================================================================
// AdditiveBlend
// ============================================================================

void AdditiveBlend::Update(float32 delta_time) {
    base_time_ = AdvanceClipTime(base_clip_.get(), base_time_, delta_time);
    additive_time_ = AdvanceClipTime(additive_clip_.get(), additive_time_, delta_time);
}

void AdditiveBlend::Sample(const Skeleton& skeleton, Pose& out_pose) const {
    skeleton.GetBindPose(out_pose);
    const Pose reference = out_pose;

    if (base_clip_) {
        base_clip_->Sample(base_time_, out_pose);
    }

    if (additive_clip_ && blend_weight_ > 0.0f) {
        Pose additive = reference;
        additive_clip_->Sample(additive_time_, additive);
        AddAdditivePose(out_pose, additive, reference, blend_weight_);
    }
}

// ============================================================================
// TwoBoneIKSolver
// ============================================================================

bool TwoBoneIKSolver::Solve(const Skeleton& skeleton, Pose& pose, const Math::Vector3& target) const {
    const int32 bone_count = skeleton.GetBoneCount();
    if (root_bone_index < 0 || mid_bone_index < 0 || end_bone_index < 0 ||
        root_bone_index >= bone_count || mid_bone_index >= bone_count || end_bone_index >= bone_count ||
        static_cast<int32>(pose.size()) != bone_count || weight <= 0.0f) {
        return false;
    }

    Array<Math::Matrix4> globals;
    skeleton.ComputeGlobalMatrices(pose, globals);
    Array<Math::Quaternion> global_rotations;
    ComputeGlobalRotations(skeleton, pose, global_rotations);

    const auto root = static_cast<size_t>(root_bone_index);
    const auto mid = static_cast<size_t>(mid_bone_index);
    const auto end = static_cast<size_t>(end_bone_index);

    const Math::Vector3 a = GetTranslation(globals[root]);
    const Math::Vector3 b = GetTranslation(globals[mid]);
    const Math::Vector3 c = GetTranslation(globals[end]);

    const float32 lab = (b - a).Length();
    const float32 lcb = (b - c).Length();
    if (lab < kEpsilon || lcb < kEpsilon) {
        return false;
    }

    const float32 lat = std::clamp((target - a).Length(), kEpsilon, lab + lcb - kEpsilon);

    const Math::Vector3 ac = (c - a).Normalized();
    const Math::Vector3 ab = (b - a).Normalized();
    const Math::Vector3 ba = (a - b).Normalized();
    const Math::Vector3 bc = (c - b).Normalized();
    const Math::Vector3 at = (target - a).Normalized();

    const float32 ac_ab_0 = SafeAcos(Math::Vector3::Dot(ac, ab));
    const float32 ba_bc_0 = SafeAcos(Math::Vector3::Dot(ba, bc));
    const float32 ac_at_0 = SafeAcos(Math::Vector3::Dot(ac, at));

    const float32 ac_ab_1 = SafeAcos((lcb * lcb - lab * lab - lat * lat) / (-2.0f * lab * lat));
    const float32 ba_bc_1 = SafeAcos((lat * lat - lab * lab - lcb * lcb) / (-2.0f * lab * lcb));

    // Bend inside the plane of the chain; a straight chain bends towards the pole
    Math::Vector3 bend_axis = Math::Vector3::Cross(c - a, b - a);
    if (bend_axis.LengthSquared() < kEpsilon) {
        bend_axis = Math::Vector3::Cross(c - a, pole_target - a);
    }
    if (bend_axis.LengthSquared() < kEpsilon) {
        bend_axis = Math::Vector3::Cross(c - a, std::abs(ac.y) < 0.9f ? Math::Vector3::Up : Math::Vector3::Right);
    }
    bend_axis.Normalize();

    // All rotations below are in model space
    const Math::Quaternion bend_root = Math::Quaternion::FromAxisAngle(bend_axis, ac_ab_1 - ac_ab_0);
    const Math::Quaternion bend_mid = Math::Quaternion::FromAxisAngle(bend_axis, ba_bc_1 - ba_bc_0);

    Math::Vector3 swing_axis = Math::Vector3::Cross(c - a, target - a);
    const Math::Quaternion swing = swing_axis.LengthSquared() > kEpsilon
        ? Math::Quaternion::FromAxisAngle(swing_axis.Normalized(), ac_at_0)
        : Math::Quaternion::Identity();

    Math::Quaternion root_delta = (swing * bend_root).Normalized();

    // Twist around the root->target axis so the elbow points at the pole target
    const Math::Vector3 bent_mid = a + root_delta * (b - a);
    const Math::Vector3 elbow = (bent_mid - a) - at * Math::Vector3::Dot(bent_mid - a, at);
    const Math::Vector3 pole = (pole_target - a) - at * Math::Vector3::Dot(pole_target - a, at);
    if (elbow.LengthSquared() > kEpsilon && pole.LengthSquared() > kEpsilon) {
        const Math::Vector3 e = elbow.Normalized();
        const Math::Vector3 p = pole.Normalized();
        const float32 twist = std::atan2(Math::Vector3::Dot(Math::Vector3::Cross(e, p), at), Math::Vector3::Dot(e, p));
        root_delta = (Math::Quaternion::FromAxisAngle(at, twist) * root_delta).Normalized();
    }

    // Convert model-space deltas back into local rotations
    const int32 root_parent = skeleton.GetBone(root_bone_index).parent_index;
    const Math::Quaternion root_parent_rot = root_parent >= 0 ? global_rotations[static_cast<size_t>(root_parent)]
                                                              : Math::Quaternion::Identity();
    const Math::Quaternion new_root_global = (root_delta * global_rotations[root]).Normalized();
    const Math::Quaternion solved_root = (root_parent_rot.Inverse() * new_root_global).Normalized();

    // Bones between root and mid move rigidly with the root
    const int32 mid_parent = skeleton.GetBone(mid_bone_index).parent_index;
    const Math::Quaternion mid_parent_rot = mid_parent >= 0
        ? (root_delta * global_rotations[static_cast<size_t>(mid_parent)]).Normalized()
        : Math::Quaternion::Identity();
    const Math::Quaternion new_mid_global = (root_delta * bend_mid * global_rotations[mid]).Normalized();
    const Math::Quaternion solved_mid = (mid_parent_rot.Inverse() * new_mid_global).Normalized();

    const float32 w = std::clamp(weight, 0.0f, 1.0f);
    pose[root].rotation = NLerpShortest(pose[root].rotation, solved_root, w);
    pose[mid].rotation = NLerpShortest(pose[mid].rotation, solved_mid, w);
    return true;
}

// ============================================================================
// FABRIKSolver
// ============================================================================

bool FABRIKSolver::Solve(Array<Math::Vector3>& chain, const Math::Vector3& target) const {
    if (chain.size() < 2) return false;

    Array<float32> lengths(chain.size() - 1);
    float32 total_length = 0.0f;
    for (size_t i = 0; i + 1 < chain.size(); ++i) {
        lengths[i] = (chain[i + 1] - chain[i]).Length();
        total_length += lengths[i];
    }

    const Math::Vector3 root_pos = chain[0];
    const float32 target_dist = (target - root_pos).Length();

    if (target_dist >= total_length) {
        // Unreachable: stretch towards target
        const Math::Vector3 dir = (target - root_pos).Normalized();
        for (size_t i = 0; i + 1 < chain.size(); ++i) {
            chain[i + 1] = chain[i] + dir * lengths[i];
        }
        return false;
    }

    for (int32 iter = 0; iter < max_iterations; ++iter) {
        // Forward reaching
        chain.back() = target;
        for (size_t i = chain.size() - 1; i-- > 0;) {
            const Math::Vector3 dir = (chain[i] - chain[i + 1]).Normalized();
            chain[i] = chain[i + 1] + dir * lengths[i];
        }

        // Backward reaching
        chain[0] = root_pos;
        for (size_t i = 0; i + 1 < chain.size(); ++i) {
            const Math::Vector3 dir = (chain[i + 1] - chain[i]).Normalized();
            chain[i + 1] = chain[i] + dir * lengths[i];
        }

        if ((chain.back() - target).Length() < tolerance) {
            return true;
        }
    }

    return (chain.back() - target).Length() < tolerance;
}

// ============================================================================
// LookAtSolver
// ============================================================================

bool LookAtSolver::Solve(const Skeleton& skeleton, Pose& pose, const Math::Vector3& target) const {
    if (bone_index < 0 || bone_index >= skeleton.GetBoneCount() ||
        static_cast<int32>(pose.size()) != skeleton.GetBoneCount()) {
        return false;
    }

    const auto index = static_cast<size_t>(bone_index);
    Array<Math::Matrix4> globals;
    skeleton.ComputeGlobalMatrices(pose, globals);
    Array<Math::Quaternion> global_rotations;
    ComputeGlobalRotations(skeleton, pose, global_rotations);

    const Math::Vector3 bone_pos = GetTranslation(globals[index]);
    const Math::Vector3 to_target = (target - bone_pos).Normalized();
    if (to_target.LengthSquared() < kEpsilon) {
        return false;
    }

    const Math::Vector3 forward = (global_rotations[index] * local_forward).Normalized();
    Math::Quaternion delta = Math::Quaternion::FromToRotation(forward, to_target);

    // Limit rotation angle
    const float32 angle = 2.0f * SafeAcos(std::abs(delta.w));
    if (angle > max_angle && angle > kEpsilon) {
        delta = NLerpShortest(Math::Quaternion::Identity(), delta, max_angle / angle);
    }
    delta = NLerpShortest(Math::Quaternion::Identity(), delta, std::clamp(weight, 0.0f, 1.0f));

    // Apply world-space delta in the bone's parent space
    const int32 parent = skeleton.GetBone(bone_index).parent_index;
    const Math::Quaternion parent_rot = parent >= 0 ? global_rotations[static_cast<size_t>(parent)]
                                                    : Math::Quaternion::Identity();
    pose[index].rotation = (parent_rot.Inverse() * delta * global_rotations[index]).Normalized();
    return true;
}

// ============================================================================
// Animator
// ============================================================================

void Animator::SetSkeleton(SharedPtr<Skeleton> skeleton) {
    skeleton_ = std::move(skeleton);
    local_pose_.clear();
    global_matrices_.clear();
    skin_matrices_.clear();

    if (skeleton_) {
        skeleton_->GetBindPose(local_pose_);
        CalculateFinalMatrices();
    }
}

void Animator::SetStateMachine(SharedPtr<AnimatorStateMachine> state_machine) {
    state_machine_ = std::move(state_machine);
}

void Animator::PlayClip(SharedPtr<AnimationClip> clip, float32 blend_time) {
    if (!clip) return;

    if (!state_machine_) {
        state_machine_ = MakeShared<AnimatorStateMachine>();
    }
    if (skeleton_) {
        clip->BindToSkeleton(*skeleton_);
    }

    if (!state_machine_->GetState(clip->name)) {
        auto state = MakeShared<AnimatorState>();
        state->name = clip->name;
        state->clip = clip;
        state_machine_->AddState(state);
    }
    state_machine_->TransitionTo(clip->name, blend_time);
}

void Animator::SetTrigger(const String& name) {
    if (state_machine_) state_machine_->SetTrigger(name);
}

void Animator::SetFloat(const String& name, float32 value) {
    if (state_machine_) state_machine_->SetParameter(name, value);
}

void Animator::SetBool(const String& name, bool value) {
    if (state_machine_) state_machine_->SetParameter(name, value ? 1.0f : 0.0f);
}

void Animator::SetInteger(const String& name, int32 value) {
    if (state_machine_) state_machine_->SetParameter(name, static_cast<float32>(value));
}

void Animator::AddTwoBoneIK(const TwoBoneIKSolver& solver, const Math::Vector3& target) {
    ik_targets_.push_back({solver, target});
}

void Animator::ClearIK() {
    ik_targets_.clear();
}

void Animator::Update(float32 delta_time) {
    if (!skeleton_) return;

    if (state_machine_) {
        state_machine_->Update(delta_time);
        state_machine_->Sample(*skeleton_, local_pose_);
    } else {
        skeleton_->GetBindPose(local_pose_);
    }

    for (const auto& ik : ik_targets_) {
        ik.solver.Solve(*skeleton_, local_pose_, ik.target);
    }

    CalculateFinalMatrices();
}

void Animator::CalculateFinalMatrices() {
    if (!skeleton_) return;

    skeleton_->ComputeGlobalMatrices(local_pose_, global_matrices_);

    const auto& bones = skeleton_->GetBones();
    skin_matrices_.resize(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        skin_matrices_[i] = global_matrices_[i] * bones[i].inverse_bind_pose;
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
    animators_.clear();
    clips_.clear();
    skeletons_.clear();
}

void AnimationManager::Update(float32 delta_time) {
    const float32 scaled = delta_time * time_scale_;
    for (auto& animator : animators_) {
        animator->Update(scaled);
    }
}

SharedPtr<Skeleton> AnimationManager::LoadSkeleton(const String& path) {
    auto it = skeletons_.find(path);
    if (it != skeletons_.end()) {
        return it->second;
    }

    auto skeleton = MakeShared<Skeleton>();
    if (!skeleton->LoadFromFile(path)) {
        return nullptr;
    }
    skeletons_[path] = skeleton;
    return skeleton;
}

SharedPtr<AnimationClip> AnimationManager::LoadClip(const String& path, uint32 animation_index) {
    const String key = path + "#" + std::to_string(animation_index);
    auto it = clips_.find(key);
    if (it != clips_.end()) {
        return it->second;
    }

    auto clip = MakeShared<AnimationClip>();
    if (!clip->LoadFromFile(path, animation_index)) {
        return nullptr;
    }
    clips_[key] = clip;
    return clip;
}

SharedPtr<Animator> AnimationManager::CreateAnimator() {
    auto animator = MakeShared<Animator>();
    animators_.push_back(animator);
    return animator;
}

void AnimationManager::DestroyAnimator(const SharedPtr<Animator>& animator) {
    animators_.erase(std::remove(animators_.begin(), animators_.end(), animator), animators_.end());
}

} // namespace DRX::Animation

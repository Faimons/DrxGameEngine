/**
 * DRX Engine - Skeletal animation runtime
 */

#include "Renderer/Public/SkinnedModel.h"
#include "Renderer/Public/SceneRenderer.h"
#include "Core/Public/Log.h"

#include <algorithm>
#include <cmath>

namespace DRX::Renderer {

namespace {

// Index of the last key at or before `time` and the blend factor to the next key
void FindKeys(const Array<float32>& times, float32 time, size_t& a, size_t& b, float32& t) {
    if (times.size() <= 1 || time <= times.front()) {
        a = b = 0;
        t = 0.0f;
        return;
    }
    if (time >= times.back()) {
        a = b = times.size() - 1;
        t = 0.0f;
        return;
    }
    const auto it = std::upper_bound(times.begin(), times.end(), time);
    b = static_cast<size_t>(it - times.begin());
    a = b - 1;
    const float32 span = times[b] - times[a];
    t = span > 0.0f ? (time - times[a]) / span : 0.0f;
}

Math::Matrix4 ToMatrix(const NodeTransform& n) {
    return Math::Matrix4::Translation(n.position) * n.rotation.ToMatrix() * Math::Matrix4::Scale(n.scale);
}

} // namespace

void BindPose(const SkinnedModelData& model, SkeletonPose& out) {
    out.resize(model.nodes.size());
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        out[i].position = model.nodes[i].position;
        out[i].rotation = model.nodes[i].rotation;
        out[i].scale = model.nodes[i].scale;
    }
}

namespace {

Array<Math::Quaternion> RestGlobalRotations(const Array<SkinNode>& nodes) {
    Array<Math::Quaternion> globals(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        const int32 parent = nodes[i].parent;
        globals[i] = parent >= 0 ? (globals[static_cast<size_t>(parent)] * nodes[i].rotation).Normalized()
                                 : nodes[i].rotation;
    }
    return globals;
}

int32 FindByKey(const Array<SkinNode>& nodes, const String& key) {
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].key == key) return static_cast<int32>(i);
    }
    return -1;
}

template<typename T>
T SampleKeys(const Array<float32>& times, const Array<T>& values, float32 time);

template<>
Math::Vector3 SampleKeys(const Array<float32>& times, const Array<Math::Vector3>& values, float32 time) {
    size_t a = 0, b = 0;
    float32 t = 0.0f;
    FindKeys(times, time, a, b, t);
    return values[a] + (values[b] - values[a]) * t;
}

template<>
Math::Quaternion SampleKeys(const Array<float32>& times, const Array<Math::Quaternion>& values, float32 time) {
    size_t a = 0, b = 0;
    float32 t = 0.0f;
    FindKeys(times, time, a, b, t);
    return Math::Quaternion::Slerp(values[a], values[b], t);
}

} // namespace

ClipBinding BindClip(const SkinnedModelData& model, const AnimationClipData& clip) {
    ClipBinding binding;
    binding.clip_node.assign(model.nodes.size(), -1);
    binding.channel.assign(clip.skeleton.size(), -1);
    for (size_t c = 0; c < clip.channels.size(); ++c) {
        const int32 node = FindByKey(clip.skeleton, clip.channels[c].key);
        if (node >= 0 && binding.channel[static_cast<size_t>(node)] < 0) binding.channel[static_cast<size_t>(node)] = static_cast<int32>(c);
    }
    for (size_t n = 0; n < model.nodes.size(); ++n) {
        const int32 node = FindByKey(clip.skeleton, model.nodes[n].key);
        // Only bones the clip animates are retargeted; everything else keeps the model's pose
        if (node >= 0 && binding.channel[static_cast<size_t>(node)] >= 0) binding.clip_node[n] = node;
    }
    binding.model_rest_global = RestGlobalRotations(model.nodes);
    binding.clip_rest_global = RestGlobalRotations(clip.skeleton);
    return binding;
}

void SampleClip(const SkinnedModelData& model, const AnimationClipData& clip, const ClipBinding& binding, float32 time,
                SkeletonPose& pose, const SampleOptions& options) {
    if (pose.size() != model.nodes.size()) BindPose(model, pose);
    if (binding.clip_node.size() != model.nodes.size() || binding.channel.size() != clip.skeleton.size()) return;
    if (clip.duration > 0.0f) {
        time = options.loop ? std::fmod(std::max(0.0f, time), clip.duration) : std::clamp(time, 0.0f, clip.duration);
    }

    // Clip rig: animated global orientations
    Array<Math::Quaternion> clip_global(clip.skeleton.size());
    for (size_t i = 0; i < clip.skeleton.size(); ++i) {
        Math::Quaternion local = clip.skeleton[i].rotation;
        const int32 c = binding.channel[i];
        if (c >= 0 && !clip.channels[static_cast<size_t>(c)].rotations.empty()) {
            const AnimationChannel& channel = clip.channels[static_cast<size_t>(c)];
            local = SampleKeys(channel.rotation_times, channel.rotations, time);
        }
        const int32 parent = clip.skeleton[i].parent;
        clip_global[i] = parent >= 0 ? (clip_global[static_cast<size_t>(parent)] * local).Normalized() : local;
    }

    // Model rig: same change of orientation from the rest pose, converted back to local rotations
    Array<Math::Quaternion> model_global(model.nodes.size());
    for (size_t n = 0; n < model.nodes.size(); ++n) {
        const int32 parent = model.nodes[n].parent;
        const Math::Quaternion parent_global = parent >= 0 ? model_global[static_cast<size_t>(parent)] : Math::Quaternion::Identity();
        const int32 a = binding.clip_node[n];
        if (a < 0) {
            model_global[n] = (parent_global * pose[n].rotation).Normalized();
            continue;
        }
        const auto ai = static_cast<size_t>(a);
        const Math::Quaternion target = (clip_global[ai] * binding.clip_rest_global[ai].Conjugate() *
                                         binding.model_rest_global[n]).Normalized();
        pose[n].rotation = (parent_global.Conjugate() * target).Normalized();
        model_global[n] = target;

        // Root height follows the clip (scaled to the model's leg length); x/z stay in place
        if (model.nodes[n].key == options.root_key) {
            const AnimationChannel& channel = clip.channels[static_cast<size_t>(binding.channel[ai])];
            if (!channel.positions.empty()) {
                const float32 clip_height = clip.skeleton[ai].position.y;
                const float32 scale = std::abs(clip_height) > 1.0e-4f ? model.nodes[n].position.y / clip_height : 1.0f;
                Math::Vector3 p = SampleKeys(channel.position_times, channel.positions, time) * scale;
                if (options.lock_root_xz) {
                    p.x = model.nodes[n].position.x;
                    p.z = model.nodes[n].position.z;
                }
                pose[n].position = p;
            }
        }
    }
}

void BlendPoses(const SkeletonPose& a, const SkeletonPose& b, float32 t, SkeletonPose& out) {
    const size_t count = std::min(a.size(), b.size());
    out.resize(count);
    for (size_t i = 0; i < count; ++i) {
        out[i].position = a[i].position + (b[i].position - a[i].position) * t;
        out[i].rotation = Math::Quaternion::Slerp(a[i].rotation, b[i].rotation, t);
        out[i].scale = a[i].scale + (b[i].scale - a[i].scale) * t;
    }
}

void ComputeGlobals(const SkinnedModelData& model, const SkeletonPose& pose, Array<Math::Matrix4>& globals) {
    globals.resize(model.nodes.size());
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const Math::Matrix4 local = ToMatrix(pose[i]);
        const int32 parent = model.nodes[i].parent;
        globals[i] = parent >= 0 ? globals[static_cast<size_t>(parent)] * local : local;
    }
}

AABB ComputeSkinnedBounds(const SkinnedModelData& model, const Array<Math::Matrix4>& globals) {
    Array<Math::Matrix4> skin(model.bones.size());
    for (size_t b = 0; b < model.bones.size(); ++b) {
        skin[b] = globals[static_cast<size_t>(model.bones[b].node)] * model.bones[b].offset;
    }
    AABB bounds;
    for (const SkinnedPart& part : model.parts) {
        for (const SkinnedVertex& v : part.vertices) {
            Math::Vector3 p = Math::Vector3::Zero;
            for (int k = 0; k < 4; ++k) {
                if (v.weights[k] > 0.0f) p = p + skin[v.bones[k]].TransformPoint(v.position) * v.weights[k];
            }
            bounds.Expand(p);
        }
    }
    return bounds;
}

bool SkinnedMesh::Create(SceneRenderer& renderer, const SkinnedModelData& model, const String& name) {
    Release(renderer);
    for (const ModelTexture& texture : model.textures) {
        textures_.push_back(texture.rgba.empty()
                                ? TextureHandle()
                                : renderer.CreateTexture(texture.width, texture.height, texture.rgba.data(), true, true,
                                                         name + "_texture"));
    }
    for (const SkinnedPart& part : model.parts) {
        MeshData data;
        data.vertices.resize(part.vertices.size());
        for (size_t i = 0; i < part.vertices.size(); ++i) {
            data.vertices[i].position = part.vertices[i].position;
            data.vertices[i].normal = part.vertices[i].normal;
            data.vertices[i].tex_coord = part.vertices[i].tex_coord;
        }
        data.indices = part.indices;
        const MeshHandle mesh = renderer.CreateDynamicMesh(data, name + "_" + part.name);
        if (!mesh.IsValid()) continue;
        Material material = part.material;
        if (part.albedo_texture >= 0 && static_cast<size_t>(part.albedo_texture) < textures_.size()) {
            material.albedo = textures_[static_cast<size_t>(part.albedo_texture)];
        }
        meshes_.push_back(mesh);
        materials_.push_back(material);
    }
    if (meshes_.size() != model.parts.size()) {
        DRX_LOG_ERROR("Skinned mesh '{}': {} of {} parts could not be created", name, model.parts.size() - meshes_.size(),
                      model.parts.size());
        Release(renderer);
        return false;
    }
    return true;
}

void SkinnedMesh::Release(SceneRenderer& renderer) {
    for (MeshHandle mesh : meshes_) renderer.DestroyMesh(mesh);
    for (TextureHandle texture : textures_) {
        if (texture.IsValid()) renderer.DestroyTexture(texture);
    }
    meshes_.clear();
    materials_.clear();
    textures_.clear();
}

void SkinnedMesh::Update(SceneRenderer& renderer, const SkinnedModelData& model, const Array<Math::Matrix4>& globals) {
    skin_.resize(model.bones.size());
    for (size_t b = 0; b < model.bones.size(); ++b) {
        skin_[b] = globals[static_cast<size_t>(model.bones[b].node)] * model.bones[b].offset;
    }
    for (size_t p = 0; p < meshes_.size() && p < model.parts.size(); ++p) {
        const SkinnedPart& part = model.parts[p];
        scratch_.resize(part.vertices.size());
        for (size_t i = 0; i < part.vertices.size(); ++i) {
            const SkinnedVertex& v = part.vertices[i];
            Math::Vector3 position = Math::Vector3::Zero;
            Math::Vector3 normal = Math::Vector3::Zero;
            for (int k = 0; k < 4; ++k) {
                const float32 w = v.weights[k];
                if (w <= 0.0f) continue;
                const Math::Matrix4& m = skin_[v.bones[k]];
                position = position + m.TransformPoint(v.position) * w;
                normal = normal + m.TransformVector(v.normal) * w;
            }
            Vertex& out = scratch_[i];
            out.position = position;
            out.normal = normal.LengthSquared() > 1.0e-12f ? normal.Normalized() : Math::Vector3::Up;
            out.tex_coord = v.tex_coord;
        }
        renderer.UpdateDynamicMesh(meshes_[p], scratch_);
    }
}

void SkinnedMesh::Draw(SceneRenderer& renderer, const Math::Matrix4& transform, const Math::Vector4& tint) const {
    for (size_t i = 0; i < meshes_.size(); ++i) {
        Material material = materials_[i];
        material.color = Math::Vector4(material.color.x * tint.x, material.color.y * tint.y, material.color.z * tint.z,
                                       material.color.w * tint.w);
        if (albedo_override_.IsValid()) material.albedo = albedo_override_;
        renderer.DrawMesh(meshes_[i], transform, material);
    }
}

} // namespace DRX::Renderer

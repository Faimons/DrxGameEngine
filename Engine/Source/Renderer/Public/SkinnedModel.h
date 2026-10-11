#pragma once

/**
 * DRX Engine - Skeletal animation runtime
 *
 *   SkinnedModelData + AnimationClipData (ModelLoader.h)
 *     -> BindClip:        clip channels to model nodes (by bone key)
 *     -> SampleClip:      local node transforms at a time (rotations of every bone, position of the root)
 *     -> BlendPoses:      cross-fade between clips
 *     -> ComputeGlobals:  model space node matrices
 *     -> SkinnedMesh:     CPU skinning into dynamic meshes and drawing
 */

#include "Renderer/Public/ModelLoader.h"

namespace DRX::Renderer {

struct NodeTransform {
    Math::Vector3 position;
    Math::Quaternion rotation;
    Math::Vector3 scale = Math::Vector3::One;
};

using SkeletonPose = Array<NodeTransform>;

void BindPose(const SkinnedModelData& model, SkeletonPose& out);

/**
 * Retargeting map between a clip's rig and a model. Bones are matched by key; a bone's motion is
 * transferred as the change of its model space orientation from the rest pose, so rigs whose
 * bones have different local axes (FBX pre-rotations, re-exported files) still match.
 */
struct ClipBinding {
    Array<int32> clip_node;                     // per model node: clip skeleton node or -1
    Array<int32> channel;                       // per clip skeleton node: channel or -1
    Array<Math::Quaternion> model_rest_global;  // per model node
    Array<Math::Quaternion> clip_rest_global;   // per clip skeleton node
    float32 root_scale = 1.0f;                  // model / clip root height (different body sizes)
};

ClipBinding BindClip(const SkinnedModelData& model, const AnimationClipData& clip);

struct SampleOptions {
    bool loop = true;
    String root_key = "hips";   // the only bone whose position is taken from the clip
    bool lock_root_xz = true;   // keep the character in place (movement comes from the game)
};

// Overwrites the animated nodes of `pose` (start from the bind pose)
void SampleClip(const SkinnedModelData& model, const AnimationClipData& clip, const ClipBinding& binding, float32 time,
                SkeletonPose& pose, const SampleOptions& options = {});

void BlendPoses(const SkeletonPose& a, const SkeletonPose& b, float32 t, SkeletonPose& out);

void ComputeGlobals(const SkinnedModelData& model, const SkeletonPose& pose, Array<Math::Matrix4>& globals);

// Bounds of the skinned vertices in the given pose
AABB ComputeSkinnedBounds(const SkinnedModelData& model, const Array<Math::Matrix4>& globals);

/**
 * GPU instance: one dynamic mesh per part. Update once per frame (between BeginFrame and the
 * draw calls), then Draw with the model's world transform.
 */
class SkinnedMesh {
public:
    bool Create(SceneRenderer& renderer, const SkinnedModelData& model, const String& name);
    void Release(SceneRenderer& renderer);
    void Update(SceneRenderer& renderer, const SkinnedModelData& model, const Array<Math::Matrix4>& globals);
    // `tint` multiplies the material colours (team colours)
    void Draw(SceneRenderer& renderer, const Math::Matrix4& transform, const Math::Vector4& tint = Math::Vector4(1, 1, 1, 1)) const;
    bool IsValid() const { return !meshes_.empty(); }
    // Replaces the albedo texture of every part (skins); invalid handle restores the original
    void SetAlbedoOverride(TextureHandle texture) { albedo_override_ = texture; }

private:
    Array<MeshHandle> meshes_;
    Array<Material> materials_;
    Array<TextureHandle> textures_;
    Array<Math::Matrix4> skin_;
    Array<Vertex> scratch_;
    TextureHandle albedo_override_;
};

} // namespace DRX::Renderer

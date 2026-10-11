#pragma once

/**
 * DRX Engine - Model loading (OBJ, FBX, glTF via Assimp)
 * Static models: the node hierarchy is flattened into one mesh per material.
 */

#include "Core/Public/CoreTypes.h"
#include "Renderer/Public/RenderTypes.h"
#include "Core/Public/Math/Quaternion.h"

namespace DRX::Renderer {

class SceneRenderer;

struct ModelLoadOptions {
    float32 scale = 1.0f;
    bool flip_uvs = true;              // most formats store V upwards
    bool generate_smooth_normals = true;
};

struct ModelTexture {
    uint32 width = 0;
    uint32 height = 0;
    Array<uint8> rgba;                 // decoded pixels (embedded or loaded from disk)
};

struct ModelPart {
    String name;
    MeshData mesh;
    Material material;
    int32 albedo_texture = -1;         // index into ModelData::textures
};

struct ModelData {
    Array<ModelPart> parts;
    Array<ModelTexture> textures;
    AABB bounds;
};

/**
 * GPU instance of a model, created by UploadModel
 */
struct Model {
    Array<MeshHandle> meshes;
    Array<Material> materials;
    Array<TextureHandle> textures;
    AABB bounds;

    bool IsValid() const { return !meshes.empty(); }
};

// CPU side: parse the file and decode its textures
bool LoadModelData(const String& path, ModelData& out, const ModelLoadOptions& options = {});

// GPU side: upload meshes and textures; returns false if nothing could be uploaded
bool UploadModel(SceneRenderer& renderer, const ModelData& data, Model& out);
void ReleaseModel(SceneRenderer& renderer, Model& model);

// Submits every part of the model with the given transform
void DrawModel(SceneRenderer& renderer, const Model& model, const Math::Matrix4& transform);

// ============================================================================
// Skinned models and skeletal animation clips
// ============================================================================

/**
 * Bone names are matched between models and clips by key: the part after the last ':' without a
 * trailing "_<digits>", lower case ("mixamorig:LeftHand" and "swat:LeftHand_017" -> "lefthand").
 * Clips made for one rig (e.g. Mixamo) therefore play on every model with that rig.
 */
String BoneKey(const String& name);

struct SkinNode {
    String name;
    String key;
    int32 parent = -1;                 // parents come before children
    Math::Vector3 position;            // bind pose, local to the parent
    Math::Quaternion rotation;
    Math::Vector3 scale = Math::Vector3::One;
};

struct SkinBone {
    int32 node = -1;
    Math::Matrix4 offset;              // mesh space -> bone space (inverse bind)
};

struct SkinnedVertex {
    Math::Vector3 position;
    Math::Vector3 normal = Math::Vector3::Up;
    Math::Vector2 tex_coord;
    uint16 bones[4] = {0, 0, 0, 0};
    float32 weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct SkinnedPart {
    String name;
    Array<SkinnedVertex> vertices;
    Array<uint32> indices;
    Material material;
    int32 albedo_texture = -1;
};

struct SkinnedModelData {
    Array<SkinNode> nodes;
    Array<SkinBone> bones;
    Array<SkinnedPart> parts;
    Array<ModelTexture> textures;
    AABB bounds;                       // bind pose

    int32 FindNode(const String& key) const;
};

struct AnimationChannel {
    String key;                        // BoneKey of the animated node
    Array<float32> position_times;
    Array<Math::Vector3> positions;
    Array<float32> rotation_times;
    Array<Math::Quaternion> rotations;
};

struct AnimationClipData {
    String name;
    float32 duration = 0.0f;           // seconds
    Array<AnimationChannel> channels;
    Array<SkinNode> skeleton;          // rest pose of the rig the clip was made for (retargeting)
};

bool LoadSkinnedModelData(const String& path, SkinnedModelData& out, const ModelLoadOptions& options = {});
// Every animation in the file (FBX from Mixamo, glTF, ...)
bool LoadAnimationClips(const String& path, Array<AnimationClipData>& out);

} // namespace DRX::Renderer

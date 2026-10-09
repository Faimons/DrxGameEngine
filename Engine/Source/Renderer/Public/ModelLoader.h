#pragma once

/**
 * DRX Engine - Model loading (OBJ, FBX, glTF via Assimp)
 * Static models: the node hierarchy is flattened into one mesh per material.
 */

#include "Core/Public/CoreTypes.h"
#include "Renderer/Public/RenderTypes.h"

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

} // namespace DRX::Renderer

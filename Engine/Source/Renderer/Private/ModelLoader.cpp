/**
 * DRX Engine - Model loading implementation
 */

#include "Renderer/Public/ModelLoader.h"
#include "Renderer/Public/SceneRenderer.h"
#include "Core/Public/Log.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <stb_image.h>

#include <algorithm>
#include <cstring>

namespace DRX::Renderer {

namespace {

Math::Matrix4 ToMatrix4(const aiMatrix4x4& m) {
    // Assimp is row-major, the engine is column-major (m[col][row])
    Math::Matrix4 r;
    r[0][0] = m.a1; r[1][0] = m.a2; r[2][0] = m.a3; r[3][0] = m.a4;
    r[0][1] = m.b1; r[1][1] = m.b2; r[2][1] = m.b3; r[3][1] = m.b4;
    r[0][2] = m.c1; r[1][2] = m.c2; r[2][2] = m.c3; r[3][2] = m.c4;
    r[0][3] = m.d1; r[1][3] = m.d2; r[2][3] = m.d3; r[3][3] = m.d4;
    return r;
}

String DirectoryOf(const String& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == String::npos ? String() : path.substr(0, slash + 1);
}

bool DecodeImage(const uint8* bytes, size_t size, ModelTexture& out) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes, static_cast<int>(size), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) return false;
    out.width = static_cast<uint32>(w);
    out.height = static_cast<uint32>(h);
    out.rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return true;
}

bool LoadTexture(const aiScene* scene, const String& model_dir, const aiString& reference, ModelTexture& out) {
    // Embedded texture ("*0" or matching file name in glb/fbx)
    if (const aiTexture* embedded = scene->GetEmbeddedTexture(reference.C_Str())) {
        if (embedded->mHeight == 0) {
            return DecodeImage(reinterpret_cast<const uint8*>(embedded->pcData), embedded->mWidth, out);
        }
        out.width = embedded->mWidth;
        out.height = embedded->mHeight;
        out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
        for (size_t i = 0; i < static_cast<size_t>(out.width) * out.height; ++i) {
            const aiTexel& t = embedded->pcData[i];
            out.rgba[i * 4 + 0] = t.r;
            out.rgba[i * 4 + 1] = t.g;
            out.rgba[i * 4 + 2] = t.b;
            out.rgba[i * 4 + 3] = t.a;
        }
        return true;
    }

    const String path = model_dir + reference.C_Str();
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        DRX_LOG_WARNING("Model texture not found: {}", path);
        return false;
    }
    out.width = static_cast<uint32>(w);
    out.height = static_cast<uint32>(h);
    out.rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return true;
}

} // namespace

bool LoadModelData(const String& path, ModelData& out, const ModelLoadOptions& options) {
    out = ModelData{};

    unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
                         aiProcess_ValidateDataStructure;
    if (options.flip_uvs) flags |= aiProcess_FlipUVs;
    if (options.generate_smooth_normals) flags |= aiProcess_GenSmoothNormals;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        DRX_LOG_ERROR("Failed to load model {}: {}", path, importer.GetErrorString());
        return false;
    }

    const String model_dir = DirectoryOf(path);

    // One part per material; texture paths are resolved once
    HashMap<uint32, size_t> part_for_material;
    HashMap<String, int32> texture_for_path;

    auto part_index = [&](uint32 material_index) -> size_t {
        auto it = part_for_material.find(material_index);
        if (it != part_for_material.end()) return it->second;

        ModelPart part;
        if (material_index < scene->mNumMaterials) {
            const aiMaterial* material = scene->mMaterials[material_index];
            part.name = material->GetName().C_Str();

            aiColor4D diffuse(1.0f, 1.0f, 1.0f, 1.0f);
            if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS ||
                material->Get(AI_MATKEY_BASE_COLOR, diffuse) == AI_SUCCESS) {
                part.material.color = Math::Vector4(diffuse.r, diffuse.g, diffuse.b, diffuse.a);
            }
            float opacity = 1.0f;
            if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
                part.material.color.w *= opacity;
            }
            float roughness = 0.0f;
            if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
                part.material.roughness = roughness;
            }
            float metallic = 0.0f;
            if (material->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
                part.material.metallic = metallic;
            }
            aiColor3D emissive(0.0f, 0.0f, 0.0f);
            if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS &&
                (emissive.r > 0.0f || emissive.g > 0.0f || emissive.b > 0.0f)) {
                part.material.emissive = Math::Vector3(emissive.r, emissive.g, emissive.b);
                part.material.emissive_strength = 1.0f;
            }
            int two_sided = 0;
            if (material->Get(AI_MATKEY_TWOSIDED, two_sided) == AI_SUCCESS) {
                part.material.double_sided = two_sided != 0;
            }

            aiString texture_path;
            if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &texture_path) == AI_SUCCESS ||
                material->GetTexture(aiTextureType_DIFFUSE, 0, &texture_path) == AI_SUCCESS) {
                const String key = texture_path.C_Str();
                auto tex_it = texture_for_path.find(key);
                if (tex_it != texture_for_path.end()) {
                    part.albedo_texture = tex_it->second;
                } else {
                    ModelTexture texture;
                    if (LoadTexture(scene, model_dir, texture_path, texture)) {
                        part.albedo_texture = static_cast<int32>(out.textures.size());
                        out.textures.push_back(std::move(texture));
                    }
                    texture_for_path[key] = part.albedo_texture;
                }
            }
        }

        out.parts.push_back(std::move(part));
        part_for_material[material_index] = out.parts.size() - 1;
        return out.parts.size() - 1;
    };

    const Math::Matrix4 root_scale = Math::Matrix4::Scale(options.scale);

    // Flatten the hierarchy
    struct Pending { const aiNode* node; Math::Matrix4 parent; };
    Array<Pending> stack{{scene->mRootNode, root_scale}};
    while (!stack.empty()) {
        const Pending current = stack.back();
        stack.pop_back();

        const Math::Matrix4 world = current.parent * ToMatrix4(current.node->mTransformation);
        const Math::Matrix4 normal_matrix = world.Inverse().Transposed();

        for (uint32 m = 0; m < current.node->mNumMeshes; ++m) {
            const aiMesh* mesh = scene->mMeshes[current.node->mMeshes[m]];
            if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) continue;

            ModelPart& part = out.parts[part_index(mesh->mMaterialIndex)];
            const auto base = static_cast<uint32>(part.mesh.vertices.size());

            for (uint32 v = 0; v < mesh->mNumVertices; ++v) {
                Vertex vertex;
                vertex.position = world.TransformPoint({mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z});
                if (mesh->HasNormals()) {
                    vertex.normal = normal_matrix.TransformVector(
                        {mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z}).Normalized();
                }
                if (mesh->HasVertexColors(0)) {
                    vertex.color = {mesh->mColors[0][v].r, mesh->mColors[0][v].g, mesh->mColors[0][v].b};
                }
                if (mesh->HasTextureCoords(0)) {
                    vertex.tex_coord = {mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y};
                }
                part.mesh.vertices.push_back(vertex);
                out.bounds.Expand(vertex.position);
            }

            for (uint32 f = 0; f < mesh->mNumFaces; ++f) {
                const aiFace& face = mesh->mFaces[f];
                if (face.mNumIndices != 3) continue;
                part.mesh.indices.insert(part.mesh.indices.end(),
                                         {base + face.mIndices[0], base + face.mIndices[1], base + face.mIndices[2]});
            }
        }

        for (uint32 c = 0; c < current.node->mNumChildren; ++c) {
            stack.push_back({current.node->mChildren[c], world});
        }
    }

    out.parts.erase(std::remove_if(out.parts.begin(), out.parts.end(),
                                   [](const ModelPart& p) { return p.mesh.IsEmpty(); }),
                    out.parts.end());

    if (out.parts.empty()) {
        DRX_LOG_ERROR("Model {} contains no triangle meshes", path);
        return false;
    }

    DRX_LOG_INFO("Loaded model {} ({} parts, {} textures)", path, out.parts.size(), out.textures.size());
    return true;
}

bool UploadModel(SceneRenderer& renderer, const ModelData& data, Model& out) {
    ReleaseModel(renderer, out);

    for (const auto& texture : data.textures) {
        out.textures.push_back(texture.rgba.empty()
            ? TextureHandle()
            : renderer.CreateTexture(texture.width, texture.height, texture.rgba.data(), true, true, "model_texture"));
    }

    for (const auto& part : data.parts) {
        const MeshHandle mesh = renderer.CreateMesh(part.mesh, part.name);
        if (!mesh.IsValid()) continue;

        Material material = part.material;
        if (part.albedo_texture >= 0 && static_cast<size_t>(part.albedo_texture) < out.textures.size()) {
            material.albedo = out.textures[static_cast<size_t>(part.albedo_texture)];
        }
        out.meshes.push_back(mesh);
        out.materials.push_back(material);
    }

    out.bounds = data.bounds;
    return out.IsValid();
}

void ReleaseModel(SceneRenderer& renderer, Model& model) {
    for (MeshHandle mesh : model.meshes) renderer.DestroyMesh(mesh);
    for (TextureHandle texture : model.textures) renderer.DestroyTexture(texture);
    model = Model{};
}

void DrawModel(SceneRenderer& renderer, const Model& model, const Math::Matrix4& transform) {
    for (size_t i = 0; i < model.meshes.size(); ++i) {
        renderer.DrawMesh(model.meshes[i], transform, model.materials[i]);
    }
}

} // namespace DRX::Renderer

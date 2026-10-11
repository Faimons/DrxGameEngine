/**
 * DRX Engine - Model loading implementation
 */

#include "Renderer/Public/ModelLoader.h"
#include "Renderer/Public/SceneRenderer.h"
#include "Renderer/Public/SkinnedModel.h"
#include "Core/Public/Log.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

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

// Material colour factors and the albedo texture (loaded once per path)
void ReadMaterial(const aiScene* scene, const String& model_dir, uint32 material_index, String& name, Material& out,
                  int32& albedo_texture, Array<ModelTexture>& textures, HashMap<String, int32>& texture_for_path) {
    if (material_index >= scene->mNumMaterials) return;
    const aiMaterial* material = scene->mMaterials[material_index];
    name = material->GetName().C_Str();

    aiColor4D diffuse(1.0f, 1.0f, 1.0f, 1.0f);
    if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS ||
        material->Get(AI_MATKEY_BASE_COLOR, diffuse) == AI_SUCCESS) {
        out.color = Math::Vector4(diffuse.r, diffuse.g, diffuse.b, diffuse.a);
    }
    float opacity = 1.0f;
    if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) out.color.w *= opacity;
    float roughness = 0.0f;
    if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) out.roughness = roughness;
    float metallic = 0.0f;
    if (material->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) out.metallic = metallic;
    aiColor3D emissive(0.0f, 0.0f, 0.0f);
    if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS &&
        (emissive.r > 0.0f || emissive.g > 0.0f || emissive.b > 0.0f)) {
        out.emissive = Math::Vector3(emissive.r, emissive.g, emissive.b);
        out.emissive_strength = 1.0f;
    }
    int two_sided = 0;
    if (material->Get(AI_MATKEY_TWOSIDED, two_sided) == AI_SUCCESS) out.double_sided = two_sided != 0;

    aiString texture_path;
    if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &texture_path) == AI_SUCCESS ||
        material->GetTexture(aiTextureType_DIFFUSE, 0, &texture_path) == AI_SUCCESS) {
        const String key = texture_path.C_Str();
        auto it = texture_for_path.find(key);
        if (it != texture_for_path.end()) {
            albedo_texture = it->second;
            return;
        }
        ModelTexture texture;
        if (LoadTexture(scene, model_dir, texture_path, texture)) {
            albedo_texture = static_cast<int32>(textures.size());
            textures.push_back(std::move(texture));
        }
        texture_for_path[key] = albedo_texture;
    }
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
        ReadMaterial(scene, model_dir, material_index, part.name, part.material, part.albedo_texture, out.textures,
                     texture_for_path);
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


// ============================================================================
// Skinned models and animation clips
// ============================================================================

String BoneKey(const String& name) {
    String key = name;
    const auto colon = key.find_last_of(':');
    if (colon != String::npos) key = key.substr(colon + 1);
    // Exporters append "_<number>" to make names unique ("Hips_01")
    const auto underscore = key.find_last_of('_');
    if (underscore != String::npos && underscore + 1 < key.size() &&
        std::all_of(key.begin() + static_cast<std::ptrdiff_t>(underscore) + 1, key.end(),
                    [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
        key = key.substr(0, underscore);
    }
    std::transform(key.begin(), key.end(), key.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return key;
}

int32 SkinnedModelData::FindNode(const String& key) const {
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].key == key) return static_cast<int32>(i);
    }
    return -1;
}

namespace {

void Decompose(const aiMatrix4x4& m, Math::Vector3& position, Math::Quaternion& rotation, Math::Vector3& scale) {
    aiVector3D s, p;
    aiQuaternion q;
    m.Decompose(s, q, p);
    position = {p.x, p.y, p.z};
    rotation = Math::Quaternion(q.x, q.y, q.z, q.w);
    scale = {s.x, s.y, s.z};
}

unsigned int ImportFlags(bool flip_uvs) {
    unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
                         aiProcess_ValidateDataStructure | aiProcess_LimitBoneWeights;
    if (flip_uvs) flags |= aiProcess_FlipUVs;
    return flags;
}

void ConfigureImporter(Assimp::Importer& importer) {
    // Fold FBX pivot helpers into the bones so node names match between files
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
}

} // namespace

bool LoadSkinnedModelData(const String& path, SkinnedModelData& out, const ModelLoadOptions& options) {
    out = SkinnedModelData{};
    Assimp::Importer importer;
    ConfigureImporter(importer);
    unsigned int flags = ImportFlags(options.flip_uvs);
    if (options.generate_smooth_normals) flags |= aiProcess_GenSmoothNormals;
    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        DRX_LOG_ERROR("Failed to load skinned model {}: {}", path, importer.GetErrorString());
        return false;
    }

    // Node hierarchy, parents first
    std::map<String, int32> node_by_name;
    struct Pending {
        const aiNode* node;
        int32 parent;
    };
    Array<Pending> queue{{scene->mRootNode, -1}};
    for (size_t i = 0; i < queue.size(); ++i) {
        const aiNode* node = queue[i].node;
        SkinNode skin_node;
        skin_node.name = node->mName.C_Str();
        skin_node.key = BoneKey(skin_node.name);
        skin_node.parent = queue[i].parent;
        Decompose(node->mTransformation, skin_node.position, skin_node.rotation, skin_node.scale);
        if (skin_node.parent < 0) skin_node.scale = skin_node.scale * options.scale;
        const auto index = static_cast<int32>(out.nodes.size());
        out.nodes.push_back(skin_node);
        node_by_name.emplace(skin_node.name, index);
        for (uint32 c = 0; c < node->mNumChildren; ++c) queue.push_back({node->mChildren[c], index});
    }
    const String model_dir = DirectoryOf(path);
    HashMap<String, int32> texture_for_path;
    std::map<String, uint16> bone_by_name;
    for (uint32 m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) || !mesh->HasBones()) continue;

        SkinnedPart part;
        ReadMaterial(scene, model_dir, mesh->mMaterialIndex, part.name, part.material, part.albedo_texture, out.textures,
                     texture_for_path);
        part.vertices.resize(mesh->mNumVertices);
        for (uint32 v = 0; v < mesh->mNumVertices; ++v) {
            SkinnedVertex& vertex = part.vertices[v];
            vertex.position = {mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z};
            if (mesh->HasNormals()) vertex.normal = {mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z};
            if (mesh->HasTextureCoords(0)) vertex.tex_coord = {mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y};
        }
        for (uint32 b = 0; b < mesh->mNumBones; ++b) {
            const aiBone* bone = mesh->mBones[b];
            const String name = bone->mName.C_Str();
            const auto node = node_by_name.find(name);
            if (node == node_by_name.end()) continue;
            auto known = bone_by_name.find(name);
            if (known == bone_by_name.end()) {
                if (out.bones.size() >= 65535) continue;
                SkinBone skin_bone;
                skin_bone.node = node->second;
                skin_bone.offset = ToMatrix4(bone->mOffsetMatrix);
                known = bone_by_name.emplace(name, static_cast<uint16>(out.bones.size())).first;
                out.bones.push_back(skin_bone);
            }
            for (uint32 w = 0; w < bone->mNumWeights; ++w) {
                const aiVertexWeight& weight = bone->mWeights[w];
                if (weight.mVertexId >= part.vertices.size() || weight.mWeight <= 0.0f) continue;
                SkinnedVertex& vertex = part.vertices[weight.mVertexId];
                // Keep the four strongest influences
                int slot = 0;
                for (int k = 1; k < 4; ++k) {
                    if (vertex.weights[k] < vertex.weights[slot]) slot = k;
                }
                if (weight.mWeight > vertex.weights[slot]) {
                    vertex.weights[slot] = weight.mWeight;
                    vertex.bones[slot] = known->second;
                }
            }
        }
        for (SkinnedVertex& vertex : part.vertices) {
            const float32 sum = vertex.weights[0] + vertex.weights[1] + vertex.weights[2] + vertex.weights[3];
            if (sum > 0.0f) {
                for (float32& w : vertex.weights) w /= sum;
            }
        }
        for (uint32 f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue;
            part.indices.insert(part.indices.end(), {face.mIndices[0], face.mIndices[1], face.mIndices[2]});
        }
        if (!part.indices.empty()) out.parts.push_back(std::move(part));
    }

    if (out.parts.empty() || out.bones.empty()) {
        DRX_LOG_ERROR("Model {} has no skinned meshes", path);
        return false;
    }
    SkeletonPose pose;
    BindPose(out, pose);
    Array<Math::Matrix4> globals;
    ComputeGlobals(out, pose, globals);
    out.bounds = ComputeSkinnedBounds(out, globals);
    DRX_LOG_INFO("Loaded skinned model {} ({} parts, {} bones, {} nodes, {} textures)", path, out.parts.size(),
                 out.bones.size(), out.nodes.size(), out.textures.size());
    return true;
}

bool LoadAnimationClips(const String& path, Array<AnimationClipData>& out) {
    out.clear();
    Assimp::Importer importer;
    ConfigureImporter(importer);
    const aiScene* scene = importer.ReadFile(path, aiProcess_ValidateDataStructure);
    if (!scene || !scene->HasAnimations()) {
        DRX_LOG_ERROR("No animation in {}: {}", path, importer.GetErrorString());
        return false;
    }
    // Rest pose of the clip's own rig, parents first
    Array<SkinNode> skeleton;
    {
        struct Pending {
            const aiNode* node;
            int32 parent;
        };
        Array<Pending> queue{{scene->mRootNode, -1}};
        for (size_t i = 0; i < queue.size(); ++i) {
            SkinNode node;
            node.name = queue[i].node->mName.C_Str();
            node.key = BoneKey(node.name);
            node.parent = queue[i].parent;
            Decompose(queue[i].node->mTransformation, node.position, node.rotation, node.scale);
            const auto index = static_cast<int32>(skeleton.size());
            skeleton.push_back(node);
            for (uint32 c = 0; c < queue[i].node->mNumChildren; ++c) queue.push_back({queue[i].node->mChildren[c], index});
        }
    }
    for (uint32 a = 0; a < scene->mNumAnimations; ++a) {
        const aiAnimation* animation = scene->mAnimations[a];
        const double ticks = animation->mTicksPerSecond > 0.0 ? animation->mTicksPerSecond : 25.0;
        AnimationClipData clip;
        clip.name = animation->mName.C_Str();
        clip.duration = static_cast<float32>(animation->mDuration / ticks);
        for (uint32 c = 0; c < animation->mNumChannels; ++c) {
            const aiNodeAnim* channel = animation->mChannels[c];
            AnimationChannel track;
            track.key = BoneKey(channel->mNodeName.C_Str());
            for (uint32 k = 0; k < channel->mNumPositionKeys; ++k) {
                const aiVectorKey& key = channel->mPositionKeys[k];
                track.position_times.push_back(static_cast<float32>(key.mTime / ticks));
                track.positions.push_back({key.mValue.x, key.mValue.y, key.mValue.z});
            }
            for (uint32 k = 0; k < channel->mNumRotationKeys; ++k) {
                const aiQuatKey& key = channel->mRotationKeys[k];
                track.rotation_times.push_back(static_cast<float32>(key.mTime / ticks));
                track.rotations.push_back(Math::Quaternion(key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w));
            }
            clip.channels.push_back(std::move(track));
        }
        clip.skeleton = skeleton;
        out.push_back(std::move(clip));
    }
    return true;
}

} // namespace DRX::Renderer

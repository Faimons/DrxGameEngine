/**
 * DRX Engine - Scene Serialization Implementation
 */

#include "Scene/Public/SceneSerialization.h"
#include <fstream>
#include <sstream>

namespace DRX::Scene {

// ============================================================================
// SceneSerializer
// ============================================================================

SceneSerializer::SceneSerializer() {
    RegisterDefaultSerializers();
}

SceneSerializer& SceneSerializer::Instance() {
    static SceneSerializer instance;
    return instance;
}

void SceneSerializer::RegisterSerializer(const String& component_type,
                                          SharedPtr<IComponentSerializer> serializer) {
    serializers_[component_type] = serializer;
}

bool SceneSerializer::SaveScene(const SceneData& scene, const String& path) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();

    // Scene metadata
    rapidjson::Value metadata(rapidjson::kObjectType);
    metadata.AddMember("name", rapidjson::Value(scene.name.c_str(), allocator), allocator);
    metadata.AddMember("version", rapidjson::Value(scene.version.c_str(), allocator), allocator);
    doc.AddMember("metadata", metadata, allocator);

    // Entities
    rapidjson::Value entities_array(rapidjson::kArrayType);

    for (const auto& entity : scene.entities) {
        rapidjson::Value entity_obj(rapidjson::kObjectType);

        entity_obj.AddMember("id", entity.id, allocator);
        entity_obj.AddMember("name", rapidjson::Value(entity.name.c_str(), allocator), allocator);
        entity_obj.AddMember("enabled", entity.enabled, allocator);
        entity_obj.AddMember("parent_id", entity.parent_id, allocator);

        // Tags
        rapidjson::Value tags_array(rapidjson::kArrayType);
        for (const auto& tag : entity.tags) {
            tags_array.PushBack(rapidjson::Value(tag.c_str(), allocator), allocator);
        }
        entity_obj.AddMember("tags", tags_array, allocator);

        // Transform
        rapidjson::Value transform_obj(rapidjson::kObjectType);
        transform_obj.AddMember("position", SerializeVector3(entity.transform.position, allocator), allocator);
        transform_obj.AddMember("rotation", SerializeQuaternion(entity.transform.rotation, allocator), allocator);
        transform_obj.AddMember("scale", SerializeVector3(entity.transform.scale, allocator), allocator);
        entity_obj.AddMember("transform", transform_obj, allocator);

        // Components
        rapidjson::Value components_array(rapidjson::kArrayType);
        for (const auto& component : entity.components) {
            rapidjson::Value comp_obj(rapidjson::kObjectType);
            comp_obj.AddMember("type", rapidjson::Value(component.type.c_str(), allocator), allocator);
            comp_obj.AddMember("enabled", component.enabled, allocator);

            // Serialize component data
            auto it = serializers_.find(component.type);
            if (it != serializers_.end()) {
                rapidjson::Value data_obj(rapidjson::kObjectType);
                it->second->Serialize(component.data, data_obj, allocator);
                comp_obj.AddMember("data", data_obj, allocator);
            }

            components_array.PushBack(comp_obj, allocator);
        }
        entity_obj.AddMember("components", components_array, allocator);

        entities_array.PushBack(entity_obj, allocator);
    }

    doc.AddMember("entities", entities_array, allocator);

    // Write to file
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << buffer.GetString();
    return true;
}

bool SceneSerializer::LoadScene(SceneData& scene, const String& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    String json_str = buffer.str();

    rapidjson::Document doc;
    doc.Parse(json_str.c_str());

    if (doc.HasParseError()) {
        return false;
    }

    // Metadata
    if (doc.HasMember("metadata")) {
        const auto& metadata = doc["metadata"];
        if (metadata.HasMember("name")) scene.name = metadata["name"].GetString();
        if (metadata.HasMember("version")) scene.version = metadata["version"].GetString();
    }

    // Entities
    scene.entities.clear();
    if (doc.HasMember("entities") && doc["entities"].IsArray()) {
        for (const auto& entity_val : doc["entities"].GetArray()) {
            EntityData entity;

            if (entity_val.HasMember("id")) entity.id = entity_val["id"].GetUint64();
            if (entity_val.HasMember("name")) entity.name = entity_val["name"].GetString();
            if (entity_val.HasMember("enabled")) entity.enabled = entity_val["enabled"].GetBool();
            if (entity_val.HasMember("parent_id")) entity.parent_id = entity_val["parent_id"].GetUint64();

            // Tags
            if (entity_val.HasMember("tags") && entity_val["tags"].IsArray()) {
                for (const auto& tag : entity_val["tags"].GetArray()) {
                    entity.tags.push_back(tag.GetString());
                }
            }

            // Transform
            if (entity_val.HasMember("transform")) {
                const auto& transform = entity_val["transform"];
                if (transform.HasMember("position")) {
                    entity.transform.position = DeserializeVector3(transform["position"]);
                }
                if (transform.HasMember("rotation")) {
                    entity.transform.rotation = DeserializeQuaternion(transform["rotation"]);
                }
                if (transform.HasMember("scale")) {
                    entity.transform.scale = DeserializeVector3(transform["scale"]);
                }
            }

            // Components
            if (entity_val.HasMember("components") && entity_val["components"].IsArray()) {
                for (const auto& comp_val : entity_val["components"].GetArray()) {
                    ComponentData component;

                    if (comp_val.HasMember("type")) component.type = comp_val["type"].GetString();
                    if (comp_val.HasMember("enabled")) component.enabled = comp_val["enabled"].GetBool();

                    // Deserialize component data
                    if (comp_val.HasMember("data")) {
                        auto it = serializers_.find(component.type);
                        if (it != serializers_.end()) {
                            it->second->Deserialize(comp_val["data"], component.data);
                        }
                    }

                    entity.components.push_back(component);
                }
            }

            scene.entities.push_back(entity);
        }
    }

    return true;
}

rapidjson::Value SceneSerializer::SerializeVector3(const Math::Vector3& v,
                                                    rapidjson::Document::AllocatorType& allocator) {
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("x", v.x, allocator);
    obj.AddMember("y", v.y, allocator);
    obj.AddMember("z", v.z, allocator);
    return obj;
}

rapidjson::Value SceneSerializer::SerializeQuaternion(const Math::Quaternion& q,
                                                       rapidjson::Document::AllocatorType& allocator) {
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("x", q.x, allocator);
    obj.AddMember("y", q.y, allocator);
    obj.AddMember("z", q.z, allocator);
    obj.AddMember("w", q.w, allocator);
    return obj;
}

rapidjson::Value SceneSerializer::SerializeVector4(const Math::Vector4& v,
                                                    rapidjson::Document::AllocatorType& allocator) {
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("x", v.x, allocator);
    obj.AddMember("y", v.y, allocator);
    obj.AddMember("z", v.z, allocator);
    obj.AddMember("w", v.w, allocator);
    return obj;
}

Math::Vector3 SceneSerializer::DeserializeVector3(const rapidjson::Value& value) {
    Math::Vector3 v;
    if (value.HasMember("x")) v.x = value["x"].GetFloat();
    if (value.HasMember("y")) v.y = value["y"].GetFloat();
    if (value.HasMember("z")) v.z = value["z"].GetFloat();
    return v;
}

Math::Quaternion SceneSerializer::DeserializeQuaternion(const rapidjson::Value& value) {
    Math::Quaternion q;
    if (value.HasMember("x")) q.x = value["x"].GetFloat();
    if (value.HasMember("y")) q.y = value["y"].GetFloat();
    if (value.HasMember("z")) q.z = value["z"].GetFloat();
    if (value.HasMember("w")) q.w = value["w"].GetFloat();
    return q;
}

Math::Vector4 SceneSerializer::DeserializeVector4(const rapidjson::Value& value) {
    Math::Vector4 v;
    if (value.HasMember("x")) v.x = value["x"].GetFloat();
    if (value.HasMember("y")) v.y = value["y"].GetFloat();
    if (value.HasMember("z")) v.z = value["z"].GetFloat();
    if (value.HasMember("w")) v.w = value["w"].GetFloat();
    return v;
}

void SceneSerializer::RegisterDefaultSerializers() {
    // Register built-in component serializers
    // These would serialize common components like MeshRenderer, Camera, Light, etc.
}

// ============================================================================
// PrefabManager
// ============================================================================

PrefabManager& PrefabManager::Instance() {
    static PrefabManager instance;
    return instance;
}

bool PrefabManager::SavePrefab(const EntityData& entity, const String& path) {
    SceneData prefab_scene;
    prefab_scene.name = entity.name;
    prefab_scene.version = "1.0";
    prefab_scene.entities.push_back(entity);

    return SceneSerializer::Instance().SaveScene(prefab_scene, path);
}

bool PrefabManager::LoadPrefab(const String& path, EntityData& out_entity) {
    SceneData prefab_scene;
    if (!SceneSerializer::Instance().LoadScene(prefab_scene, path)) {
        return false;
    }

    if (prefab_scene.entities.empty()) {
        return false;
    }

    out_entity = prefab_scene.entities[0];
    return true;
}

SharedPtr<PrefabAsset> PrefabManager::GetPrefab(const String& path) {
    auto it = prefabs_.find(path);
    if (it != prefabs_.end()) {
        return it->second;
    }

    auto prefab = std::make_shared<PrefabAsset>();
    prefab->path = path;

    if (LoadPrefab(path, prefab->root_entity)) {
        prefabs_[path] = prefab;
        return prefab;
    }

    return nullptr;
}

EntityData PrefabManager::InstantiatePrefab(const String& path) {
    auto prefab = GetPrefab(path);
    if (!prefab) {
        return EntityData{};
    }

    EntityData instance = prefab->root_entity;

    // Generate new unique IDs for the instance
    instance.id = GenerateEntityID();
    for (auto& child_id : prefab->root_entity.children_ids) {
        // Would recursively update child IDs
    }

    return instance;
}

void PrefabManager::UnloadPrefab(const String& path) {
    prefabs_.erase(path);
}

uint64 PrefabManager::GenerateEntityID() {
    static uint64 next_id = 1;
    return next_id++;
}

// ============================================================================
// SceneManager
// ============================================================================

SceneManager& SceneManager::Instance() {
    static SceneManager instance;
    return instance;
}

bool SceneManager::Initialize() {
    return true;
}

void SceneManager::Shutdown() {
    scenes_.clear();
    active_scene_ = nullptr;
}

bool SceneManager::LoadScene(const String& path) {
    auto scene = std::make_shared<SceneData>();
    if (!SceneSerializer::Instance().LoadScene(*scene, path)) {
        return false;
    }

    scenes_[path] = scene;
    return true;
}

bool SceneManager::SaveScene(const String& path) {
    if (!active_scene_) {
        return false;
    }

    return SceneSerializer::Instance().SaveScene(*active_scene_, path);
}

bool SceneManager::SaveSceneAs(const String& path) {
    return SaveScene(path);
}

void SceneManager::NewScene(const String& name) {
    auto scene = std::make_shared<SceneData>();
    scene->name = name;
    scene->version = "1.0";

    String path = name + ".drxscene";
    scenes_[path] = scene;
    active_scene_ = scene.get();
}

void SceneManager::SetActiveScene(const String& path) {
    auto it = scenes_.find(path);
    if (it != scenes_.end()) {
        active_scene_ = it->second.get();
    }
}

SceneData* SceneManager::GetActiveScene() {
    return active_scene_;
}

SceneData* SceneManager::GetScene(const String& path) {
    auto it = scenes_.find(path);
    return it != scenes_.end() ? it->second.get() : nullptr;
}

void SceneManager::UnloadScene(const String& path) {
    auto it = scenes_.find(path);
    if (it != scenes_.end()) {
        if (active_scene_ == it->second.get()) {
            active_scene_ = nullptr;
        }
        scenes_.erase(it);
    }
}

EntityData* SceneManager::CreateEntity(const String& name) {
    if (!active_scene_) return nullptr;

    EntityData entity;
    entity.id = PrefabManager::Instance().GenerateEntityID();
    entity.name = name;
    entity.enabled = true;
    entity.parent_id = 0;
    entity.transform.position = Math::Vector3::Zero;
    entity.transform.rotation = Math::Quaternion::Identity;
    entity.transform.scale = Math::Vector3::One;

    active_scene_->entities.push_back(entity);
    return &active_scene_->entities.back();
}

void SceneManager::DestroyEntity(uint64 entity_id) {
    if (!active_scene_) return;

    active_scene_->entities.erase(
        std::remove_if(active_scene_->entities.begin(), active_scene_->entities.end(),
            [entity_id](const EntityData& e) { return e.id == entity_id; }),
        active_scene_->entities.end()
    );
}

EntityData* SceneManager::FindEntity(uint64 entity_id) {
    if (!active_scene_) return nullptr;

    for (auto& entity : active_scene_->entities) {
        if (entity.id == entity_id) {
            return &entity;
        }
    }
    return nullptr;
}

EntityData* SceneManager::FindEntityByName(const String& name) {
    if (!active_scene_) return nullptr;

    for (auto& entity : active_scene_->entities) {
        if (entity.name == name) {
            return &entity;
        }
    }
    return nullptr;
}

Array<EntityData*> SceneManager::FindEntitiesByTag(const String& tag) {
    Array<EntityData*> result;
    if (!active_scene_) return result;

    for (auto& entity : active_scene_->entities) {
        for (const auto& t : entity.tags) {
            if (t == tag) {
                result.push_back(&entity);
                break;
            }
        }
    }
    return result;
}

} // namespace DRX::Scene

/**
 * DRX Engine - Scene Serialization Implementation
 */

// Define assert before RapidJSON includes
#ifndef assert
#define assert(x) ((void)0)
#endif

#include <fstream>
#include <sstream>

#pragma warning(push)
#pragma warning(disable: 4005)  // Macro redefinition
#pragma warning(disable: 4702)  // Unreachable code in rapidjson

#include <rapidjson/writer.h>
#include <rapidjson/prettywriter.h>

#pragma warning(pop)

#include "Scene/Public/SceneSerialization.h"
#include "Scene/Public/Entity.h"

namespace DRX::Scene {

// ============================================================================
// Static member initialization
// ============================================================================

HashMap<String, UniquePtr<IComponentSerializer>> SceneSerializer::component_serializers_;

// ============================================================================
// JSON Utilities
// ============================================================================

namespace JsonUtil {

void WriteVector3(rapidjson::Value& parent, const char* name,
                  const Math::Vector3& v, rapidjson::Document::AllocatorType& alloc) {
    rapidjson::Value vec(rapidjson::kArrayType);
    vec.PushBack(v.x, alloc);
    vec.PushBack(v.y, alloc);
    vec.PushBack(v.z, alloc);
    parent.AddMember(rapidjson::Value(name, alloc), vec, alloc);
}

void WriteVector4(rapidjson::Value& parent, const char* name,
                  const Math::Vector4& v, rapidjson::Document::AllocatorType& alloc) {
    rapidjson::Value vec(rapidjson::kArrayType);
    vec.PushBack(v.x, alloc);
    vec.PushBack(v.y, alloc);
    vec.PushBack(v.z, alloc);
    vec.PushBack(v.w, alloc);
    parent.AddMember(rapidjson::Value(name, alloc), vec, alloc);
}

void WriteQuaternion(rapidjson::Value& parent, const char* name,
                     const Math::Quaternion& q, rapidjson::Document::AllocatorType& alloc) {
    rapidjson::Value quat(rapidjson::kArrayType);
    quat.PushBack(q.x, alloc);
    quat.PushBack(q.y, alloc);
    quat.PushBack(q.z, alloc);
    quat.PushBack(q.w, alloc);
    parent.AddMember(rapidjson::Value(name, alloc), quat, alloc);
}

void WriteTransform(rapidjson::Value& parent, const char* name,
                    const Math::Transform& t, rapidjson::Document::AllocatorType& alloc) {
    rapidjson::Value transform(rapidjson::kObjectType);
    WriteVector3(transform, "position", t.position, alloc);
    WriteQuaternion(transform, "rotation", t.rotation, alloc);
    WriteVector3(transform, "scale", t.scale, alloc);
    parent.AddMember(rapidjson::Value(name, alloc), transform, alloc);
}

void WriteString(rapidjson::Value& parent, const char* name,
                 const String& s, rapidjson::Document::AllocatorType& alloc) {
    parent.AddMember(rapidjson::Value(name, alloc),
                     rapidjson::Value(s.c_str(), alloc), alloc);
}

Math::Vector3 ReadVector3(const rapidjson::Value& value, const Math::Vector3& default_val) {
    if (!value.IsArray() || value.Size() < 3) return default_val;
    return {value[0].GetFloat(), value[1].GetFloat(), value[2].GetFloat()};
}

Math::Vector4 ReadVector4(const rapidjson::Value& value, const Math::Vector4& default_val) {
    if (!value.IsArray() || value.Size() < 4) return default_val;
    return {value[0].GetFloat(), value[1].GetFloat(), value[2].GetFloat(), value[3].GetFloat()};
}

Math::Quaternion ReadQuaternion(const rapidjson::Value& value) {
    if (!value.IsArray() || value.Size() < 4) return {};
    return {value[0].GetFloat(), value[1].GetFloat(), value[2].GetFloat(), value[3].GetFloat()};
}

Math::Transform ReadTransform(const rapidjson::Value& value) {
    Math::Transform t;
    if (value.HasMember("position")) t.position = ReadVector3(value["position"]);
    if (value.HasMember("rotation")) t.rotation = ReadQuaternion(value["rotation"]);
    if (value.HasMember("scale")) t.scale = ReadVector3(value["scale"]);
    return t;
}

String ReadString(const rapidjson::Value& value, const char* name, const String& default_val) {
    if (!value.HasMember(name) || !value[name].IsString()) return default_val;
    return value[name].GetString();
}

float32 ReadFloat(const rapidjson::Value& value, const char* name, float32 default_val) {
    if (!value.HasMember(name) || !value[name].IsNumber()) return default_val;
    return value[name].GetFloat();
}

int32 ReadInt(const rapidjson::Value& value, const char* name, int32 default_val) {
    if (!value.HasMember(name) || !value[name].IsInt()) return default_val;
    return value[name].GetInt();
}

bool ReadBool(const rapidjson::Value& value, const char* name, bool default_val) {
    if (!value.HasMember(name) || !value[name].IsBool()) return default_val;
    return value[name].GetBool();
}

} // namespace JsonUtil

// ============================================================================
// SceneSerializer
// ============================================================================

SceneSerializer::SceneSerializer(Scene* scene) : scene_(scene) {
}

bool SceneSerializer::SaveToFile(const String& path, const SerializationContext& ctx) {
    String json = SaveToString(ctx);
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << json;
    return true;
}

String SceneSerializer::SaveToString(const SerializationContext& ctx) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    // Metadata
    rapidjson::Value metadata(rapidjson::kObjectType);
    metadata.AddMember("version", ctx.version, alloc);
    doc.AddMember("metadata", metadata, alloc);

    // Entities
    rapidjson::Value entities(rapidjson::kArrayType);
    if (scene_) {
        // Serialize entities from scene
    }
    doc.AddMember("entities", entities, alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

Array<uint8> SceneSerializer::SaveToBinary(const SerializationContext& ctx) {
    Array<uint8> data;
    // Binary serialization implementation
    return data;
}

bool SceneSerializer::LoadFromFile(const String& path, const SerializationContext& ctx) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    return LoadFromString(buffer.str(), ctx);
}

bool SceneSerializer::LoadFromString(const String& json, const SerializationContext& ctx) {
    rapidjson::Document doc;
    doc.Parse(json.c_str());

    if (doc.HasParseError()) return false;
    if (!doc.HasMember("entities")) return false;

    // Load entities
    return true;
}

bool SceneSerializer::LoadFromBinary(const Array<uint8>& data, const SerializationContext& ctx) {
    return true;
}

void SceneSerializer::RegisterComponentSerializer(UniquePtr<IComponentSerializer> serializer) {
    if (!serializer) return;
    component_serializers_[serializer->GetTypeName()] = std::move(serializer);
}

IComponentSerializer* SceneSerializer::GetComponentSerializer(const String& type_name) {
    auto it = component_serializers_.find(type_name);
    if (it != component_serializers_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void SceneSerializer::SerializeEntity(rapidjson::Value& entities_array, Entity* entity,
                                      rapidjson::Document::AllocatorType& alloc,
                                      const SerializationContext& ctx) {
    if (!entity) return;

    rapidjson::Value entity_obj(rapidjson::kObjectType);
    // Serialize entity properties
    entities_array.PushBack(entity_obj, alloc);
}

Entity* SceneSerializer::DeserializeEntity(const rapidjson::Value& entity_value,
                                           const SerializationContext& ctx) {
    return nullptr;
}

void SceneSerializer::SerializeComponent(rapidjson::Value& components_array,
                                         const String& type_name, void* component,
                                         rapidjson::Document::AllocatorType& alloc,
                                         const SerializationContext& ctx) {
    if (!component) return;
    auto* serializer = GetComponentSerializer(type_name);
    if (serializer) {
        rapidjson::Value comp(rapidjson::kObjectType);
        comp.AddMember("type", rapidjson::Value(type_name.c_str(), alloc), alloc);
        serializer->Serialize(comp, alloc, component, ctx);
        components_array.PushBack(comp, alloc);
    }
}

// ============================================================================
// Built-in Component Serializers
// ============================================================================

void TransformSerializer::Serialize(rapidjson::Value& value,
                                    rapidjson::Document::AllocatorType& allocator,
                                    void* component,
                                    const SerializationContext& ctx) const {
    auto* transform_comp = static_cast<TransformComponent*>(component);
    if (!transform_comp) return;

    JsonUtil::WriteVector3(value, "position", transform_comp->transform.position, allocator);
    JsonUtil::WriteQuaternion(value, "rotation", transform_comp->transform.rotation, allocator);
    JsonUtil::WriteVector3(value, "scale", transform_comp->transform.scale, allocator);
}

void TransformSerializer::Deserialize(const rapidjson::Value& value,
                                      void* component,
                                      const SerializationContext& ctx) const {
    auto* transform_comp = static_cast<TransformComponent*>(component);
    if (!transform_comp) return;

    if (value.HasMember("position")) {
        transform_comp->transform.position = JsonUtil::ReadVector3(value["position"]);
    }
    if (value.HasMember("rotation")) {
        transform_comp->transform.rotation = JsonUtil::ReadQuaternion(value["rotation"]);
    }
    if (value.HasMember("scale")) {
        transform_comp->transform.scale = JsonUtil::ReadVector3(value["scale"]);
    }
}

void MeshRendererSerializer::Serialize(rapidjson::Value& value,
                                       rapidjson::Document::AllocatorType& allocator,
                                       void* component,
                                       const SerializationContext& ctx) const {
}

void MeshRendererSerializer::Deserialize(const rapidjson::Value& value,
                                         void* component,
                                         const SerializationContext& ctx) const {
}

void CameraSerializer::Serialize(rapidjson::Value& value,
                                rapidjson::Document::AllocatorType& allocator,
                                void* component,
                                const SerializationContext& ctx) const {
}

void CameraSerializer::Deserialize(const rapidjson::Value& value,
                                  void* component,
                                  const SerializationContext& ctx) const {
}

void LightSerializer::Serialize(rapidjson::Value& value,
                               rapidjson::Document::AllocatorType& allocator,
                               void* component,
                               const SerializationContext& ctx) const {
}

void LightSerializer::Deserialize(const rapidjson::Value& value,
                                 void* component,
                                 const SerializationContext& ctx) const {
}

void RigidBodySerializer::Serialize(rapidjson::Value& value,
                                   rapidjson::Document::AllocatorType& allocator,
                                   void* component,
                                   const SerializationContext& ctx) const {
}

void RigidBodySerializer::Deserialize(const rapidjson::Value& value,
                                     void* component,
                                     const SerializationContext& ctx) const {
}

void ScriptComponentSerializer::Serialize(rapidjson::Value& value,
                                          rapidjson::Document::AllocatorType& allocator,
                                          void* component,
                                          const SerializationContext& ctx) const {
}

void ScriptComponentSerializer::Deserialize(const rapidjson::Value& value,
                                           void* component,
                                           const SerializationContext& ctx) const {
}

void AudioSourceSerializer::Serialize(rapidjson::Value& value,
                                     rapidjson::Document::AllocatorType& allocator,
                                     void* component,
                                     const SerializationContext& ctx) const {
}

void AudioSourceSerializer::Deserialize(const rapidjson::Value& value,
                                       void* component,
                                       const SerializationContext& ctx) const {
}

// ============================================================================
// Prefab
// ============================================================================

bool Prefab::LoadFromFile(const String& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    serialized_data_ = buffer.str();
    path_ = path;
    return true;
}

bool Prefab::SaveToFile(const String& path) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << serialized_data_;
    return true;
}

bool Prefab::LoadFromEntity(Entity* entity) {
    // Serialize entity to prefab data
    return true;
}

Entity* Prefab::Instantiate(Scene* scene, const Math::Transform& transform) {
    if (!scene) return nullptr;
    // Create entity from prefab data
    return nullptr;
}

// ============================================================================
// PrefabManager
// ============================================================================

PrefabManager& PrefabManager::Instance() {
    static PrefabManager instance;
    return instance;
}

bool PrefabManager::Initialize() {
    return true;
}

void PrefabManager::Shutdown() {
    UnloadAll();
}

SharedPtr<Prefab> PrefabManager::Load(const String& path) {
    auto it = prefabs_.find(path);
    if (it != prefabs_.end()) {
        return it->second;
    }

    auto prefab = std::make_shared<Prefab>();
    if (prefab->LoadFromFile(path)) {
        prefab->SetName(path);
        prefabs_[path] = prefab;
        return prefab;
    }

    return nullptr;
}

SharedPtr<Prefab> PrefabManager::Get(const String& path) {
    auto it = prefabs_.find(path);
    if (it != prefabs_.end()) {
        return it->second;
    }
    return nullptr;
}

SharedPtr<Prefab> PrefabManager::Create(const String& name, Entity* source_entity) {
    auto prefab = std::make_shared<Prefab>();
    prefab->SetName(name);
    prefab->LoadFromEntity(source_entity);
    return prefab;
}

void PrefabManager::Unload(const String& path) {
    prefabs_.erase(path);
}

void PrefabManager::UnloadAll() {
    prefabs_.clear();
}

Entity* PrefabManager::Instantiate(const String& path, Scene* scene,
                                  const Math::Transform& transform) {
    auto prefab = Load(path);
    if (!prefab) return nullptr;
    return prefab->Instantiate(scene, transform);
}

} // namespace DRX::Scene

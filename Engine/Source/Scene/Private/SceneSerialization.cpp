/**
 * DRX Engine - Scene Serialization Implementation
 */

#include "Scene/Public/SceneSerialization.h"
#include "Core/Public/Log.h"

#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <fstream>
#include <sstream>

namespace DRX::Scene {

// ============================================================================
// JSON utilities
// ============================================================================

namespace JsonUtil {

namespace {

const rapidjson::Value* Member(const rapidjson::Value& parent, const char* name) {
    if (!parent.IsObject()) return nullptr;
    const auto it = parent.FindMember(name);
    return it != parent.MemberEnd() ? &it->value : nullptr;
}

template<size_t N>
bool ReadFloats(const rapidjson::Value& parent, const char* name, float32 (&out)[N]) {
    const rapidjson::Value* v = Member(parent, name);
    if (!v || !v->IsArray() || v->Size() != N) return false;
    for (rapidjson::SizeType i = 0; i < N; ++i) {
        if (!(*v)[i].IsNumber()) return false;
    }
    for (rapidjson::SizeType i = 0; i < N; ++i) {
        out[i] = (*v)[i].GetFloat();
    }
    return true;
}

void WriteFloats(rapidjson::Value& parent, const char* name, std::initializer_list<float32> values,
                 JsonAllocator& alloc) {
    rapidjson::Value array(rapidjson::kArrayType);
    for (float32 f : values) array.PushBack(f, alloc);
    parent.AddMember(rapidjson::Value(name, alloc), array, alloc);
}

} // namespace

void WriteVector3(rapidjson::Value& parent, const char* name, const Math::Vector3& v, JsonAllocator& alloc) {
    WriteFloats(parent, name, {v.x, v.y, v.z}, alloc);
}

void WriteVector4(rapidjson::Value& parent, const char* name, const Math::Vector4& v, JsonAllocator& alloc) {
    WriteFloats(parent, name, {v.x, v.y, v.z, v.w}, alloc);
}

void WriteQuaternion(rapidjson::Value& parent, const char* name, const Math::Quaternion& q, JsonAllocator& alloc) {
    WriteFloats(parent, name, {q.x, q.y, q.z, q.w}, alloc);
}

void WriteString(rapidjson::Value& parent, const char* name, const String& s, JsonAllocator& alloc) {
    parent.AddMember(rapidjson::Value(name, alloc),
                     rapidjson::Value(s.c_str(), static_cast<rapidjson::SizeType>(s.size()), alloc), alloc);
}

void WriteFloat(rapidjson::Value& parent, const char* name, float32 v, JsonAllocator& alloc) {
    parent.AddMember(rapidjson::Value(name, alloc), rapidjson::Value(static_cast<double>(v)), alloc);
}

void WriteBool(rapidjson::Value& parent, const char* name, bool v, JsonAllocator& alloc) {
    parent.AddMember(rapidjson::Value(name, alloc), rapidjson::Value(v), alloc);
}

Math::Vector3 ReadVector3(const rapidjson::Value& parent, const char* name, const Math::Vector3& default_val) {
    float32 f[3];
    return ReadFloats(parent, name, f) ? Math::Vector3(f[0], f[1], f[2]) : default_val;
}

Math::Vector4 ReadVector4(const rapidjson::Value& parent, const char* name, const Math::Vector4& default_val) {
    float32 f[4];
    return ReadFloats(parent, name, f) ? Math::Vector4(f[0], f[1], f[2], f[3]) : default_val;
}

Math::Quaternion ReadQuaternion(const rapidjson::Value& parent, const char* name,
                                const Math::Quaternion& default_val) {
    float32 f[4];
    if (!ReadFloats(parent, name, f)) return default_val;
    const Math::Quaternion q(f[0], f[1], f[2], f[3]);
    const float32 length_sq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return length_sq > 1e-12f ? q.Normalized() : default_val;
}

String ReadString(const rapidjson::Value& parent, const char* name, const String& default_val) {
    const rapidjson::Value* v = Member(parent, name);
    return v && v->IsString() ? String(v->GetString(), v->GetStringLength()) : default_val;
}

float32 ReadFloat(const rapidjson::Value& parent, const char* name, float32 default_val) {
    const rapidjson::Value* v = Member(parent, name);
    return v && v->IsNumber() ? v->GetFloat() : default_val;
}

int32 ReadInt(const rapidjson::Value& parent, const char* name, int32 default_val) {
    const rapidjson::Value* v = Member(parent, name);
    return v && v->IsInt() ? v->GetInt() : default_val;
}

bool ReadBool(const rapidjson::Value& parent, const char* name, bool default_val) {
    const rapidjson::Value* v = Member(parent, name);
    return v && v->IsBool() ? v->GetBool() : default_val;
}

} // namespace JsonUtil

// ============================================================================
// Component registry
// ============================================================================

namespace {

template<typename E>
const char* EnumName(E value, std::initializer_list<std::pair<E, const char*>> names) {
    for (const auto& [v, n] : names) {
        if (v == value) return n;
    }
    return names.begin()->second;
}

template<typename E>
E EnumValue(const String& text, E fallback, std::initializer_list<std::pair<E, const char*>> names) {
    for (const auto& [v, n] : names) {
        if (text == n) return v;
    }
    return fallback;
}

const std::initializer_list<std::pair<LightComponent::Type, const char*>> kLightTypes = {
    {LightComponent::Type::Directional, "directional"},
    {LightComponent::Type::Point, "point"},
    {LightComponent::Type::Spot, "spot"},
};

const std::initializer_list<std::pair<RigidBodyComponent::BodyType, const char*>> kBodyTypes = {
    {RigidBodyComponent::BodyType::Static, "static"},
    {RigidBodyComponent::BodyType::Dynamic, "dynamic"},
    {RigidBodyComponent::BodyType::Kinematic, "kinematic"},
};

const std::initializer_list<std::pair<ColliderComponent::Shape, const char*>> kShapes = {
    {ColliderComponent::Shape::Box, "box"},
    {ColliderComponent::Shape::Sphere, "sphere"},
    {ColliderComponent::Shape::Capsule, "capsule"},
    {ColliderComponent::Shape::Mesh, "mesh"},
};

} // namespace

ComponentRegistry& ComponentRegistry::Instance() {
    static ComponentRegistry instance;
    return instance;
}

ComponentRegistry::ComponentRegistry() {
    RegisterBuiltins();
}

void ComponentRegistry::Add(Entry entry) {
    for (Entry& existing : entries_) {
        if (existing.name == entry.name) {
            existing = std::move(entry);
            return;
        }
    }
    entries_.push_back(std::move(entry));
}

const ComponentRegistry::Entry* ComponentRegistry::Find(const String& name) const {
    for (const Entry& entry : entries_) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

void ComponentRegistry::RegisterBuiltins() {
    using namespace JsonUtil;

    Register<NameComponent>(
        "Name",
        [](const NameComponent& c, rapidjson::Value& v, JsonAllocator& a) { WriteString(v, "name", c.name, a); },
        [](NameComponent& c, const rapidjson::Value& v) { c.name = ReadString(v, "name", c.name); });

    Register<TransformComponent>(
        "Transform",
        [](const TransformComponent& c, rapidjson::Value& v, JsonAllocator& a) {
            WriteVector3(v, "position", c.transform.position, a);
            WriteQuaternion(v, "rotation", c.transform.rotation, a);
            WriteVector3(v, "scale", c.transform.scale, a);
        },
        [](TransformComponent& c, const rapidjson::Value& v) {
            c.transform.position = ReadVector3(v, "position", c.transform.position);
            c.transform.rotation = ReadQuaternion(v, "rotation", c.transform.rotation);
            c.transform.scale = ReadVector3(v, "scale", c.transform.scale);
        });

    Register<MeshComponent>(
        "Mesh",
        [](const MeshComponent& c, rapidjson::Value& v, JsonAllocator& a) {
            WriteString(v, "source", c.source, a);
            WriteBool(v, "cast_shadow", c.cast_shadow, a);
            WriteBool(v, "receive_shadow", c.receive_shadow, a);
        },
        [](MeshComponent& c, const rapidjson::Value& v) {
            c.source = ReadString(v, "source", c.source);
            c.cast_shadow = ReadBool(v, "cast_shadow", c.cast_shadow);
            c.receive_shadow = ReadBool(v, "receive_shadow", c.receive_shadow);
        });

    Register<CameraComponent>(
        "Camera",
        [](const CameraComponent& c, rapidjson::Value& v, JsonAllocator& a) {
            WriteFloat(v, "fov", c.fov, a);
            WriteFloat(v, "near", c.near_plane, a);
            WriteFloat(v, "far", c.far_plane, a);
            WriteBool(v, "primary", c.is_primary, a);
        },
        [](CameraComponent& c, const rapidjson::Value& v) {
            c.fov = ReadFloat(v, "fov", c.fov);
            c.near_plane = ReadFloat(v, "near", c.near_plane);
            c.far_plane = ReadFloat(v, "far", c.far_plane);
            c.is_primary = ReadBool(v, "primary", c.is_primary);
        });

    Register<LightComponent>(
        "Light",
        [](const LightComponent& c, rapidjson::Value& v, JsonAllocator& a) {
            WriteString(v, "type", EnumName(c.type, kLightTypes), a);
            WriteVector3(v, "color", c.color, a);
            WriteFloat(v, "intensity", c.intensity, a);
            WriteFloat(v, "range", c.range, a);
            WriteFloat(v, "spot_angle", c.spot_angle, a);
            WriteBool(v, "cast_shadows", c.cast_shadows, a);
        },
        [](LightComponent& c, const rapidjson::Value& v) {
            c.type = EnumValue(ReadString(v, "type"), c.type, kLightTypes);
            c.color = ReadVector3(v, "color", c.color);
            c.intensity = ReadFloat(v, "intensity", c.intensity);
            c.range = ReadFloat(v, "range", c.range);
            c.spot_angle = ReadFloat(v, "spot_angle", c.spot_angle);
            c.cast_shadows = ReadBool(v, "cast_shadows", c.cast_shadows);
        });

    Register<RigidBodyComponent>(
        "RigidBody",
        [](const RigidBodyComponent& c, rapidjson::Value& v, JsonAllocator& a) {
            WriteString(v, "type", EnumName(c.body_type, kBodyTypes), a);
            WriteFloat(v, "mass", c.mass, a);
            WriteFloat(v, "friction", c.friction, a);
            WriteFloat(v, "restitution", c.restitution, a);
            WriteBool(v, "gravity", c.use_gravity, a);
        },
        [](RigidBodyComponent& c, const rapidjson::Value& v) {
            c.body_type = EnumValue(ReadString(v, "type"), c.body_type, kBodyTypes);
            c.mass = ReadFloat(v, "mass", c.mass);
            c.friction = ReadFloat(v, "friction", c.friction);
            c.restitution = ReadFloat(v, "restitution", c.restitution);
            c.use_gravity = ReadBool(v, "gravity", c.use_gravity);
        });

    Register<ColliderComponent>(
        "Collider",
        [](const ColliderComponent& c, rapidjson::Value& v, JsonAllocator& a) {
            WriteString(v, "shape", EnumName(c.shape, kShapes), a);
            WriteVector3(v, "size", c.size, a);
            WriteFloat(v, "radius", c.radius, a);
            WriteFloat(v, "height", c.height, a);
            WriteBool(v, "trigger", c.is_trigger, a);
        },
        [](ColliderComponent& c, const rapidjson::Value& v) {
            c.shape = EnumValue(ReadString(v, "shape"), c.shape, kShapes);
            c.size = ReadVector3(v, "size", c.size);
            c.radius = ReadFloat(v, "radius", c.radius);
            c.height = ReadFloat(v, "height", c.height);
            c.is_trigger = ReadBool(v, "trigger", c.is_trigger);
        });

    Register<AudioSourceComponent>(
        "AudioSource",
        [](const AudioSourceComponent& c, rapidjson::Value& v, JsonAllocator& a) {
            WriteString(v, "clip", c.audio_clip, a);
            WriteFloat(v, "volume", c.volume, a);
            WriteFloat(v, "pitch", c.pitch, a);
            WriteBool(v, "loop", c.loop, a);
            WriteBool(v, "play_on_start", c.play_on_start, a);
            WriteBool(v, "spatial", c.spatial, a);
            WriteFloat(v, "min_distance", c.min_distance, a);
            WriteFloat(v, "max_distance", c.max_distance, a);
        },
        [](AudioSourceComponent& c, const rapidjson::Value& v) {
            c.audio_clip = ReadString(v, "clip", c.audio_clip);
            c.volume = ReadFloat(v, "volume", c.volume);
            c.pitch = ReadFloat(v, "pitch", c.pitch);
            c.loop = ReadBool(v, "loop", c.loop);
            c.play_on_start = ReadBool(v, "play_on_start", c.play_on_start);
            c.spatial = ReadBool(v, "spatial", c.spatial);
            c.min_distance = ReadFloat(v, "min_distance", c.min_distance);
            c.max_distance = ReadFloat(v, "max_distance", c.max_distance);
        });

    Register<ScriptComponent>(
        "Script",
        [](const ScriptComponent& c, rapidjson::Value& v, JsonAllocator& a) {
            WriteString(v, "path", c.script_path, a);
            rapidjson::Value properties(rapidjson::kObjectType);
            for (const auto& [key, value] : c.properties) {
                rapidjson::Value name(key.c_str(), a);
                if (const bool* b = std::any_cast<bool>(&value)) properties.AddMember(name, *b, a);
                else if (const int32* i = std::any_cast<int32>(&value)) properties.AddMember(name, *i, a);
                else if (const float32* f = std::any_cast<float32>(&value)) properties.AddMember(name, static_cast<double>(*f), a);
                else if (const float64* d = std::any_cast<float64>(&value)) properties.AddMember(name, *d, a);
                else if (const String* s = std::any_cast<String>(&value)) properties.AddMember(name, rapidjson::Value(s->c_str(), a), a);
                else DRX_LOG_WARNING("Script property '{}' has a type that cannot be saved", key);
            }
            v.AddMember("properties", properties, a);
        },
        [](ScriptComponent& c, const rapidjson::Value& v) {
            c.script_path = ReadString(v, "path", c.script_path);
            const auto it = v.FindMember("properties");
            if (it == v.MemberEnd() || !it->value.IsObject()) return;
            for (auto p = it->value.MemberBegin(); p != it->value.MemberEnd(); ++p) {
                const String key(p->name.GetString(), p->name.GetStringLength());
                if (p->value.IsBool()) c.properties[key] = p->value.GetBool();
                else if (p->value.IsInt()) c.properties[key] = static_cast<int32>(p->value.GetInt());
                else if (p->value.IsNumber()) c.properties[key] = static_cast<float32>(p->value.GetDouble());
                else if (p->value.IsString()) c.properties[key] = String(p->value.GetString(), p->value.GetStringLength());
            }
        });
}

// ============================================================================
// SceneSerializer
// ============================================================================

namespace {

bool ReadFile(const String& path, String& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

bool WriteFile(const String& path, const String& text) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << text << '\n';
    return static_cast<bool>(file);
}

constexpr unsigned kParseFlags = rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag;

} // namespace

void SceneSerializer::WriteEntity(Scene& scene, EntityHandle handle, rapidjson::Value& out, JsonAllocator& alloc) {
    out.SetObject();
    for (const auto& entry : ComponentRegistry::Instance().GetEntries()) {
        if (!entry.has(scene, handle)) continue;
        rapidjson::Value component(rapidjson::kObjectType);
        entry.write(scene, handle, component, alloc);
        out.AddMember(rapidjson::Value(entry.name.c_str(), alloc), component, alloc);
    }
}

Entity SceneSerializer::ReadEntity(Scene& scene, const rapidjson::Value& value) {
    if (!value.IsObject()) return {};

    const ComponentRegistry& registry = ComponentRegistry::Instance();
    String name = "Entity";
    const auto name_member = value.FindMember("Name");
    if (name_member != value.MemberEnd()) name = JsonUtil::ReadString(name_member->value, "name", name);

    Entity entity = scene.CreateEntity(name);
    for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it) {
        const String name(it->name.GetString(), it->name.GetStringLength());
        const ComponentRegistry::Entry* entry = registry.Find(name);
        if (!entry) {
            DRX_LOG_WARNING("Unknown component '{}' skipped", name);
            continue;
        }
        if (!it->value.IsObject()) {
            DRX_LOG_WARNING("Component '{}' must be an object", name);
            continue;
        }
        entry->read(scene, entity.GetHandle(), it->value);
    }
    return entity;
}

String SceneSerializer::SaveToString(bool pretty) const {
    rapidjson::Document doc(rapidjson::kObjectType);
    auto& alloc = doc.GetAllocator();
    doc.AddMember("version", kVersion, alloc);
    JsonUtil::WriteString(doc, "name", scene_.GetName(), alloc);

    rapidjson::Value entities(rapidjson::kArrayType);
    for (const Entity& entity : scene_.GetAllEntities()) {
        rapidjson::Value entity_value;
        WriteEntity(scene_, entity.GetHandle(), entity_value, alloc);
        entities.PushBack(entity_value, alloc);
    }
    doc.AddMember("entities", entities, alloc);

    rapidjson::StringBuffer buffer;
    if (pretty) {
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
    } else {
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
    }
    return String(buffer.GetString(), buffer.GetSize());
}

bool SceneSerializer::SaveToFile(const String& path) const {
    if (!WriteFile(path, SaveToString(true))) {
        DRX_LOG_ERROR("Cannot write scene file {}", path);
        return false;
    }
    return true;
}

bool SceneSerializer::LoadFromFile(const String& path) {
    String json;
    if (!ReadFile(path, json)) {
        DRX_LOG_ERROR("Scene file not found: {}", path);
        return false;
    }
    return LoadFromString(json, path);
}

bool SceneSerializer::LoadFromString(const String& json, const String& source_name) {
    rapidjson::Document doc;
    doc.Parse<kParseFlags>(json.c_str(), json.size());
    if (doc.HasParseError()) {
        DRX_LOG_ERROR("Scene {}: {} at offset {}", source_name, rapidjson::GetParseError_En(doc.GetParseError()),
                      doc.GetErrorOffset());
        return false;
    }
    if (!doc.IsObject()) {
        DRX_LOG_ERROR("Scene {}: root must be an object", source_name);
        return false;
    }
    const int32 version = JsonUtil::ReadInt(doc, "version", kVersion);
    if (version > kVersion) {
        DRX_LOG_ERROR("Scene {}: version {} is newer than supported ({})", source_name, version, kVersion);
        return false;
    }
    const auto entities = doc.FindMember("entities");
    if (entities == doc.MemberEnd() || !entities->value.IsArray()) {
        DRX_LOG_ERROR("Scene {}: missing 'entities' array", source_name);
        return false;
    }
    for (const auto& entity : entities->value.GetArray()) {
        if (!entity.IsObject()) {
            DRX_LOG_ERROR("Scene {}: every entity must be an object", source_name);
            return false;
        }
    }

    const String name = JsonUtil::ReadString(doc, "name");
    if (!name.empty()) scene_.SetName(name);
    for (const auto& entity : entities->value.GetArray()) {
        ReadEntity(scene_, entity);
    }
    return true;
}

// ============================================================================
// Prefab
// ============================================================================

bool Prefab::LoadFromFile(const String& path) {
    String json;
    if (!ReadFile(path, json)) {
        DRX_LOG_ERROR("Prefab file not found: {}", path);
        return false;
    }
    return LoadFromString(json);
}

bool Prefab::LoadFromString(const String& json) {
    rapidjson::Document doc;
    doc.Parse<kParseFlags>(json.c_str(), json.size());
    if (doc.HasParseError() || !doc.IsObject()) {
        DRX_LOG_ERROR("Prefab is not a valid JSON object");
        return false;
    }
    json_ = json;
    return true;
}

bool Prefab::SaveToFile(const String& path) const {
    return IsValid() && WriteFile(path, json_);
}

void Prefab::CaptureEntity(Entity entity) {
    json_.clear();
    if (!entity.IsValid()) return;
    rapidjson::Document doc;
    SceneSerializer::WriteEntity(*entity.GetScene(), entity.GetHandle(), doc, doc.GetAllocator());
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    json_.assign(buffer.GetString(), buffer.GetSize());
}

Entity Prefab::Instantiate(Scene& scene) const {
    if (!IsValid()) return {};
    rapidjson::Document doc;
    doc.Parse<kParseFlags>(json_.c_str(), json_.size());
    return SceneSerializer::ReadEntity(scene, doc);
}

Entity Prefab::Instantiate(Scene& scene, const Math::Transform& transform) const {
    Entity entity = Instantiate(scene);
    if (entity.IsValid()) entity.GetTransform().transform = transform;
    return entity;
}

// ============================================================================
// PrefabManager
// ============================================================================

PrefabManager& PrefabManager::Instance() {
    static PrefabManager instance;
    return instance;
}

SharedPtr<Prefab> PrefabManager::Load(const String& path) {
    if (auto existing = Get(path)) return existing;
    auto prefab = MakeShared<Prefab>();
    if (!prefab->LoadFromFile(path)) return nullptr;
    prefabs_[path] = prefab;
    return prefab;
}

SharedPtr<Prefab> PrefabManager::Get(const String& path) const {
    const auto it = prefabs_.find(path);
    return it != prefabs_.end() ? it->second : nullptr;
}

void PrefabManager::Unload(const String& path) {
    prefabs_.erase(path);
}

void PrefabManager::UnloadAll() {
    prefabs_.clear();
}

Entity PrefabManager::Instantiate(const String& path, Scene& scene, const Math::Transform& transform) {
    auto prefab = Load(path);
    return prefab ? prefab->Instantiate(scene, transform) : Entity{};
}

} // namespace DRX::Scene

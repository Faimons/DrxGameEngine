#pragma once

/**
 * DRX Engine - Scene Serialization
 * Scenes and prefabs as JSON. Components are registered with a name and a pair of
 * read/write functions; the built-in components are registered automatically.
 *
 *   { "version": 1, "name": "Arena",
 *     "entities": [ { "Name": { "name": "Wall" }, "Transform": { "position": [0, 2, 0] }, ... } ] }
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Vector4.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Transform.h"
#include "Scene/Public/Entity.h"

#include <rapidjson/document.h>

#include <type_traits>

namespace DRX::Scene {

using JsonAllocator = rapidjson::Document::AllocatorType;

namespace JsonUtil {
void WriteVector3(rapidjson::Value& parent, const char* name, const Math::Vector3& v, JsonAllocator& alloc);
void WriteVector4(rapidjson::Value& parent, const char* name, const Math::Vector4& v, JsonAllocator& alloc);
void WriteQuaternion(rapidjson::Value& parent, const char* name, const Math::Quaternion& q, JsonAllocator& alloc);
void WriteString(rapidjson::Value& parent, const char* name, const String& s, JsonAllocator& alloc);
void WriteFloat(rapidjson::Value& parent, const char* name, float32 v, JsonAllocator& alloc);
void WriteBool(rapidjson::Value& parent, const char* name, bool v, JsonAllocator& alloc);

// Readers return the default when the member is missing or has the wrong type
Math::Vector3 ReadVector3(const rapidjson::Value& parent, const char* name, const Math::Vector3& default_val = {});
Math::Vector4 ReadVector4(const rapidjson::Value& parent, const char* name, const Math::Vector4& default_val = {});
Math::Quaternion ReadQuaternion(const rapidjson::Value& parent, const char* name,
                                const Math::Quaternion& default_val = {});
String ReadString(const rapidjson::Value& parent, const char* name, const String& default_val = {});
float32 ReadFloat(const rapidjson::Value& parent, const char* name, float32 default_val = 0.0f);
int32 ReadInt(const rapidjson::Value& parent, const char* name, int32 default_val = 0);
bool ReadBool(const rapidjson::Value& parent, const char* name, bool default_val = false);
} // namespace JsonUtil

/**
 * Registry of serializable component types
 */
class ComponentRegistry {
public:
    template<typename T>
    using WriteFn = Function<void(const T&, rapidjson::Value&, JsonAllocator&)>;
    template<typename T>
    using ReadFn = Function<void(T&, const rapidjson::Value&)>;

    static ComponentRegistry& Instance();

    // Registers (or replaces) a component type under a JSON key
    template<typename T>
    void Register(const String& name, WriteFn<T> write, ReadFn<T> read);

    struct Entry {
        String name;
        Function<bool(Scene&, EntityHandle)> has;
        Function<void(Scene&, EntityHandle, rapidjson::Value&, JsonAllocator&)> write;
        Function<void(Scene&, EntityHandle, const rapidjson::Value&)> read;
    };
    const Array<Entry>& GetEntries() const { return entries_; }
    const Entry* Find(const String& name) const;

private:
    ComponentRegistry();
    void RegisterBuiltins();
    void Add(Entry entry);

    Array<Entry> entries_;
};

class SceneSerializer {
public:
    static constexpr int32 kVersion = 1;

    explicit SceneSerializer(Scene& scene) : scene_(scene) {}

    bool SaveToFile(const String& path) const;
    String SaveToString(bool pretty = true) const;

    // Adds the file's entities to the scene; nothing is added when the file is invalid
    bool LoadFromFile(const String& path);
    bool LoadFromString(const String& json, const String& source_name = "<memory>");

    // Single entity as a JSON object (used by prefabs)
    static void WriteEntity(Scene& scene, EntityHandle handle, rapidjson::Value& out, JsonAllocator& alloc);
    static Entity ReadEntity(Scene& scene, const rapidjson::Value& value);

private:
    Scene& scene_;
};

/**
 * Prefab - one entity stored as JSON, instantiated any number of times
 */
class Prefab {
public:
    bool LoadFromFile(const String& path);
    bool LoadFromString(const String& json);
    bool SaveToFile(const String& path) const;
    void CaptureEntity(Entity entity);

    // Creates a copy; the transform replaces the stored one
    Entity Instantiate(Scene& scene, const Math::Transform& transform) const;
    Entity Instantiate(Scene& scene) const;

    bool IsValid() const { return !json_.empty(); }
    const String& GetJson() const { return json_; }

private:
    String json_;
};

class PrefabManager {
public:
    static PrefabManager& Instance();

    SharedPtr<Prefab> Load(const String& path);
    SharedPtr<Prefab> Get(const String& path) const;
    void Unload(const String& path);
    void UnloadAll();
    Entity Instantiate(const String& path, Scene& scene, const Math::Transform& transform);

private:
    PrefabManager() = default;
    HashMap<String, SharedPtr<Prefab>> prefabs_;
};

// ============================================================================
// Template implementation
// ============================================================================

template<typename T>
void ComponentRegistry::Register(const String& name, WriteFn<T> write, ReadFn<T> read) {
    Entry entry;
    entry.name = name;
    entry.has = [](Scene& scene, EntityHandle handle) { return scene.HasComponent<T>(handle); };
    entry.write = [write = std::move(write)](Scene& scene, EntityHandle handle, rapidjson::Value& out,
                                             JsonAllocator& alloc) {
        if (const T* component = scene.GetComponent<T>(handle)) {
            write(*component, out, alloc);
            if constexpr (std::is_base_of_v<Component, T>) {
                if (!component->enabled) JsonUtil::WriteBool(out, "enabled", false, alloc);
            }
        }
    };
    entry.read = [read = std::move(read)](Scene& scene, EntityHandle handle, const rapidjson::Value& value) {
        T* component = scene.GetComponent<T>(handle);
        if (!component) {
            scene.AddComponent<T>(handle, T{});
            component = scene.GetComponent<T>(handle);
        }
        read(*component, value);
        if constexpr (std::is_base_of_v<Component, T>) {
            component->enabled = JsonUtil::ReadBool(value, "enabled", true);
        }
    };
    Add(std::move(entry));
}

} // namespace DRX::Scene

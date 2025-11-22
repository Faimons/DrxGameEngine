/**
 * DRX Engine - Scene Serialization
 * Save/Load scenes to JSON/Binary format
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Transform.h"

#include <rapidjson/document.h>

namespace DRX::Scene {

// Forward declarations
class Scene;
class Entity;

/**
 * Serialization format
 */
enum class SerializationFormat : uint32 {
    JSON,       // Human-readable JSON
    Binary,     // Compact binary format
    YAML        // YAML (future)
};

/**
 * Serialization context
 */
struct SerializationContext {
    SerializationFormat format = SerializationFormat::JSON;
    bool pretty_print = true;
    bool include_defaults = false;
    bool compress = false;
    uint32 version = 1;
    String asset_root;

    // Entity ID mapping for references
    HashMap<uint64, uint64> id_remap;
};

/**
 * Component serializer interface
 */
class IComponentSerializer {
public:
    virtual ~IComponentSerializer() = default;

    virtual const char* GetTypeName() const = 0;
    virtual void Serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator,
                           void* component, const SerializationContext& ctx) const = 0;
    virtual void Deserialize(const rapidjson::Value& value, void* component,
                             const SerializationContext& ctx) const = 0;
};

/**
 * JSON Utilities
 */
namespace JsonUtil {
    // Write helpers
    void WriteVector3(rapidjson::Value& parent, const char* name,
                      const Math::Vector3& v, rapidjson::Document::AllocatorType& alloc);
    void WriteVector4(rapidjson::Value& parent, const char* name,
                      const Math::Vector4& v, rapidjson::Document::AllocatorType& alloc);
    void WriteQuaternion(rapidjson::Value& parent, const char* name,
                         const Math::Quaternion& q, rapidjson::Document::AllocatorType& alloc);
    void WriteTransform(rapidjson::Value& parent, const char* name,
                        const Math::Transform& t, rapidjson::Document::AllocatorType& alloc);
    void WriteString(rapidjson::Value& parent, const char* name,
                     const String& s, rapidjson::Document::AllocatorType& alloc);

    // Read helpers
    Math::Vector3 ReadVector3(const rapidjson::Value& value, const Math::Vector3& default_val = {});
    Math::Vector4 ReadVector4(const rapidjson::Value& value, const Math::Vector4& default_val = {});
    Math::Quaternion ReadQuaternion(const rapidjson::Value& value);
    Math::Transform ReadTransform(const rapidjson::Value& value);
    String ReadString(const rapidjson::Value& value, const char* name, const String& default_val = "");
    float32 ReadFloat(const rapidjson::Value& value, const char* name, float32 default_val = 0.0f);
    int32 ReadInt(const rapidjson::Value& value, const char* name, int32 default_val = 0);
    bool ReadBool(const rapidjson::Value& value, const char* name, bool default_val = false);
}

/**
 * Scene Serializer
 */
class SceneSerializer {
public:
    SceneSerializer(Scene* scene);
    ~SceneSerializer() = default;

    // Serialize
    bool SaveToFile(const String& path, const SerializationContext& ctx = {});
    String SaveToString(const SerializationContext& ctx = {});
    Array<uint8> SaveToBinary(const SerializationContext& ctx = {});

    // Deserialize
    bool LoadFromFile(const String& path, const SerializationContext& ctx = {});
    bool LoadFromString(const String& json, const SerializationContext& ctx = {});
    bool LoadFromBinary(const Array<uint8>& data, const SerializationContext& ctx = {});

    // Component serializers
    static void RegisterComponentSerializer(UniquePtr<IComponentSerializer> serializer);
    static IComponentSerializer* GetComponentSerializer(const String& type_name);

private:
    void SerializeEntity(rapidjson::Value& entities_array, Entity* entity,
                         rapidjson::Document::AllocatorType& alloc, const SerializationContext& ctx);
    Entity* DeserializeEntity(const rapidjson::Value& entity_value, const SerializationContext& ctx);

    void SerializeComponent(rapidjson::Value& components_array, const String& type_name,
                            void* component, rapidjson::Document::AllocatorType& alloc,
                            const SerializationContext& ctx);

    Scene* scene_ = nullptr;
    static HashMap<String, UniquePtr<IComponentSerializer>> component_serializers_;
};

/**
 * Prefab - reusable entity template
 */
class Prefab {
public:
    Prefab() = default;
    ~Prefab() = default;

    bool LoadFromFile(const String& path);
    bool SaveToFile(const String& path);

    bool LoadFromEntity(Entity* entity);
    Entity* Instantiate(Scene* scene, const Math::Transform& transform = {});

    const String& GetName() const { return name_; }
    void SetName(const String& name) { name_ = name; }

    const String& GetPath() const { return path_; }

private:
    String name_;
    String path_;
    String serialized_data_;
};

/**
 * Prefab Manager
 */
class PrefabManager {
public:
    static PrefabManager& Instance();

    bool Initialize();
    void Shutdown();

    SharedPtr<Prefab> Load(const String& path);
    SharedPtr<Prefab> Get(const String& path);
    SharedPtr<Prefab> Create(const String& name, Entity* source_entity);

    void Unload(const String& path);
    void UnloadAll();

    Entity* Instantiate(const String& path, Scene* scene, const Math::Transform& transform = {});

private:
    PrefabManager() = default;

    HashMap<String, SharedPtr<Prefab>> prefabs_;
};

/**
 * Built-in component serializers
 */

// Transform Component
class TransformSerializer : public IComponentSerializer {
public:
    const char* GetTypeName() const override { return "Transform"; }
    void Serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator,
                   void* component, const SerializationContext& ctx) const override;
    void Deserialize(const rapidjson::Value& value, void* component,
                     const SerializationContext& ctx) const override;
};

// MeshRenderer Component
class MeshRendererSerializer : public IComponentSerializer {
public:
    const char* GetTypeName() const override { return "MeshRenderer"; }
    void Serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator,
                   void* component, const SerializationContext& ctx) const override;
    void Deserialize(const rapidjson::Value& value, void* component,
                     const SerializationContext& ctx) const override;
};

// Camera Component
class CameraSerializer : public IComponentSerializer {
public:
    const char* GetTypeName() const override { return "Camera"; }
    void Serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator,
                   void* component, const SerializationContext& ctx) const override;
    void Deserialize(const rapidjson::Value& value, void* component,
                     const SerializationContext& ctx) const override;
};

// Light Component
class LightSerializer : public IComponentSerializer {
public:
    const char* GetTypeName() const override { return "Light"; }
    void Serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator,
                   void* component, const SerializationContext& ctx) const override;
    void Deserialize(const rapidjson::Value& value, void* component,
                     const SerializationContext& ctx) const override;
};

// RigidBody Component
class RigidBodySerializer : public IComponentSerializer {
public:
    const char* GetTypeName() const override { return "RigidBody"; }
    void Serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator,
                   void* component, const SerializationContext& ctx) const override;
    void Deserialize(const rapidjson::Value& value, void* component,
                     const SerializationContext& ctx) const override;
};

// Script Component
class ScriptComponentSerializer : public IComponentSerializer {
public:
    const char* GetTypeName() const override { return "Script"; }
    void Serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator,
                   void* component, const SerializationContext& ctx) const override;
    void Deserialize(const rapidjson::Value& value, void* component,
                     const SerializationContext& ctx) const override;
};

// AudioSource Component
class AudioSourceSerializer : public IComponentSerializer {
public:
    const char* GetTypeName() const override { return "AudioSource"; }
    void Serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator,
                   void* component, const SerializationContext& ctx) const override;
    void Deserialize(const rapidjson::Value& value, void* component,
                     const SerializationContext& ctx) const override;
};

} // namespace DRX::Scene

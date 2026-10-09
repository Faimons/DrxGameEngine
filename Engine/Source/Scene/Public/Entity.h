#pragma once

/**
 * DRX Engine - Entity Component System
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Platform.h"
#include "Core/Public/Assert.h"
#include "Core/Public/Math/Transform.h"

#include <typeindex>
#include <unordered_map>
#include <any>

namespace DRX::Scene {

// ============================================================================
// Component Base
// ============================================================================

struct Component {
    virtual ~Component() = default;
    bool enabled = true;
};

// ============================================================================
// Common Components
// ============================================================================

struct TransformComponent : public Component {
    Math::Transform transform;

    TransformComponent() = default;
    TransformComponent(const Math::Vector3& pos) : transform(pos) {}
};

struct NameComponent : public Component {
    String name;

    NameComponent() = default;
    NameComponent(const String& n) : name(n) {}
};

struct MeshComponent : public Component {
    String source;          // asset path or procedural id; resolved to `mesh` by the game
    MeshHandle mesh;
    MaterialHandle material;
    bool cast_shadow = true;
    bool receive_shadow = true;
};

struct CameraComponent : public Component {
    float32 fov = 60.0f;
    float32 near_plane = 0.1f;
    float32 far_plane = 1000.0f;
    bool is_primary = false;
};

struct LightComponent : public Component {
    enum class Type { Directional, Point, Spot };

    Type type = Type::Point;
    Math::Vector3 color = {1.0f, 1.0f, 1.0f};
    float32 intensity = 1.0f;
    float32 range = 10.0f;
    float32 spot_angle = 45.0f;
    bool cast_shadows = true;
};

struct RigidBodyComponent : public Component {
    enum class BodyType { Static, Dynamic, Kinematic };

    BodyType body_type = BodyType::Dynamic;
    float32 mass = 1.0f;
    float32 friction = 0.5f;
    float32 restitution = 0.0f;
    bool use_gravity = true;

    Math::Vector3 velocity;
    Math::Vector3 angular_velocity;
};

struct ColliderComponent : public Component {
    enum class Shape { Box, Sphere, Capsule, Mesh };

    Shape shape = Shape::Box;
    Math::Vector3 size = {1.0f, 1.0f, 1.0f};
    float32 radius = 0.5f;
    float32 height = 2.0f;
    bool is_trigger = false;
};

struct ScriptComponent : public Component {
    String script_path;
    HashMap<String, std::any> properties;
};

struct AudioSourceComponent : public Component {
    String audio_clip;
    float32 volume = 1.0f;
    float32 pitch = 1.0f;
    bool loop = false;
    bool play_on_start = false;
    bool spatial = true;
    float32 min_distance = 1.0f;
    float32 max_distance = 500.0f;
};

// ============================================================================
// Entity Class
// ============================================================================

class Scene;

class DRX_API Entity {
public:
    Entity() = default;
    Entity(EntityHandle handle, Scene* scene);

    bool IsValid() const;
    EntityHandle GetHandle() const { return handle_; }
    Scene* GetScene() const { return scene_; }

    template<typename T, typename... Args>
    T& AddComponent(Args&&... args) {
        return AddComponentInternal<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    T& GetComponent();

    template<typename T>
    const T& GetComponent() const;

    template<typename T>
    bool HasComponent() const;

    template<typename T>
    void RemoveComponent();

    // Convenience accessors
    TransformComponent& GetTransform();
    const String& GetName() const;
    void SetName(const String& name);

    void Destroy();

    bool operator==(const Entity& other) const {
        return handle_ == other.handle_;
    }

    bool operator!=(const Entity& other) const {
        return !(*this == other);
    }

private:
    template<typename T, typename... Args>
    T& AddComponentInternal(Args&&... args);

    EntityHandle handle_;
    Scene* scene_ = nullptr;
};

// ============================================================================
// Scene Class
// ============================================================================

class DRX_API Scene {
public:
    Scene(const String& name = "Untitled");
    ~Scene();

    Entity CreateEntity(const String& name = "Entity");
    void DestroyEntity(Entity entity);

    Entity FindEntityByName(const String& name);
    Array<Entity> GetAllEntities();

    template<typename T>
    Array<Entity> GetEntitiesWithComponent();

    void Update(float32 delta_time);

    const String& GetName() const { return name_; }
    void SetName(const String& name) { name_ = name; }

    // Internal component storage
    template<typename T>
    void AddComponent(EntityHandle handle, T component);

    template<typename T>
    T* GetComponent(EntityHandle handle);

    template<typename T>
    bool HasComponent(EntityHandle handle) const;

    template<typename T>
    void RemoveComponent(EntityHandle handle);

private:
    String name_;
    uint32 next_entity_id_ = 0;

    Array<EntityHandle> entities_;
    HashMap<EntityHandle, HashMap<std::type_index, SharedPtr<Component>>> components_;
};

// ============================================================================
// Template Implementations
// ============================================================================

template<typename T, typename... Args>
T& Entity::AddComponentInternal(Args&&... args) {
    auto component = MakeShared<T>(std::forward<Args>(args)...);
    scene_->AddComponent<T>(handle_, *component);
    return *static_cast<T*>(scene_->GetComponent<T>(handle_));
}

template<typename T>
T& Entity::GetComponent() {
    auto* comp = scene_->GetComponent<T>(handle_);
    DRX_ASSERT(comp != nullptr);
    return *comp;
}

template<typename T>
const T& Entity::GetComponent() const {
    auto* comp = scene_->GetComponent<T>(handle_);
    DRX_ASSERT(comp != nullptr);
    return *comp;
}

template<typename T>
bool Entity::HasComponent() const {
    return scene_->HasComponent<T>(handle_);
}

template<typename T>
void Entity::RemoveComponent() {
    scene_->RemoveComponent<T>(handle_);
}

template<typename T>
void Scene::AddComponent(EntityHandle handle, T component) {
    auto ptr = MakeShared<T>(std::move(component));
    components_[handle][std::type_index(typeid(T))] = ptr;
}

template<typename T>
T* Scene::GetComponent(EntityHandle handle) {
    auto entity_it = components_.find(handle);
    if (entity_it == components_.end()) return nullptr;

    auto comp_it = entity_it->second.find(std::type_index(typeid(T)));
    if (comp_it == entity_it->second.end()) return nullptr;

    return static_cast<T*>(comp_it->second.get());
}

template<typename T>
bool Scene::HasComponent(EntityHandle handle) const {
    auto entity_it = components_.find(handle);
    if (entity_it == components_.end()) return false;
    return entity_it->second.count(std::type_index(typeid(T))) > 0;
}

template<typename T>
void Scene::RemoveComponent(EntityHandle handle) {
    auto entity_it = components_.find(handle);
    if (entity_it != components_.end()) {
        entity_it->second.erase(std::type_index(typeid(T)));
    }
}

template<typename T>
Array<Entity> Scene::GetEntitiesWithComponent() {
    Array<Entity> result;
    for (auto& handle : entities_) {
        if (HasComponent<T>(handle)) {
            result.push_back(Entity(handle, this));
        }
    }
    return result;
}

} // namespace DRX::Scene

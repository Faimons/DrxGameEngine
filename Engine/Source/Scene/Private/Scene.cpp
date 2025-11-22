/**
 * DRX Engine - Scene Implementation
 */

#include "Scene/Public/Entity.h"
#include "Core/Public/Log.h"

namespace DRX::Scene {

// ============================================================================
// Entity Implementation
// ============================================================================

Entity::Entity(EntityHandle handle, Scene* scene)
    : handle_(handle)
    , scene_(scene)
{}

bool Entity::IsValid() const {
    return handle_.IsValid() && scene_ != nullptr;
}

TransformComponent& Entity::GetTransform() {
    if (!HasComponent<TransformComponent>()) {
        AddComponent<TransformComponent>();
    }
    return GetComponent<TransformComponent>();
}

const String& Entity::GetName() const {
    static String empty;
    if (HasComponent<NameComponent>()) {
        return GetComponent<NameComponent>().name;
    }
    return empty;
}

void Entity::SetName(const String& name) {
    if (!HasComponent<NameComponent>()) {
        AddComponent<NameComponent>(name);
    } else {
        GetComponent<NameComponent>().name = name;
    }
}

void Entity::Destroy() {
    if (scene_) {
        scene_->DestroyEntity(*this);
    }
}

// ============================================================================
// Scene Implementation
// ============================================================================

Scene::Scene(const String& name)
    : name_(name)
{
    DRX_LOG_INFO("Scene created: %s", name.c_str());
}

Scene::~Scene() {
    entities_.clear();
    components_.clear();
    DRX_LOG_INFO("Scene destroyed: %s", name_.c_str());
}

Entity Scene::CreateEntity(const String& name) {
    EntityHandle handle(next_entity_id_++);
    entities_.push_back(handle);

    Entity entity(handle, this);
    entity.AddComponent<TransformComponent>();
    entity.AddComponent<NameComponent>(name);

    DRX_LOG_DEBUG("Entity created: %s (id: %u)", name.c_str(), handle.GetId());
    return entity;
}

void Scene::DestroyEntity(Entity entity) {
    if (!entity.IsValid()) return;

    EntityHandle handle = entity.GetHandle();

    // Remove components
    components_.erase(handle);

    // Remove from entity list
    auto it = std::find(entities_.begin(), entities_.end(), handle);
    if (it != entities_.end()) {
        entities_.erase(it);
    }

    DRX_LOG_DEBUG("Entity destroyed: %u", handle.GetId());
}

Entity Scene::FindEntityByName(const String& name) {
    for (auto& handle : entities_) {
        auto* name_comp = GetComponent<NameComponent>(handle);
        if (name_comp && name_comp->name == name) {
            return Entity(handle, this);
        }
    }
    return Entity();
}

Array<Entity> Scene::GetAllEntities() {
    Array<Entity> result;
    for (auto& handle : entities_) {
        result.push_back(Entity(handle, this));
    }
    return result;
}

void Scene::Update(float32 delta_time) {
    DRX_UNUSED(delta_time);

    // Update systems here
    // Physics update
    // Script update
    // Animation update
    // etc.
}

} // namespace DRX::Scene

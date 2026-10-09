/**
 * DRX Engine - Physics World Implementation (Bullet)
 */

#include "Physics/Public/PhysicsWorld.h"
#include "Core/Public/Log.h"

#include <btBulletDynamicsCommon.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace DRX::Physics {

namespace {

// btCollisionObject::getUserIndex() tags which wrapper owns the object
constexpr int kTagRigidBody = 1;
constexpr int kTagCharacter = 2;

btVector3 ToBullet(const Math::Vector3& v) { return btVector3(v.x, v.y, v.z); }
Math::Vector3 FromBullet(const btVector3& v) { return Math::Vector3(v.x(), v.y(), v.z()); }
btQuaternion ToBullet(const Math::Quaternion& q) { return btQuaternion(q.x, q.y, q.z, q.w); }
Math::Quaternion FromBullet(const btQuaternion& q) { return Math::Quaternion(q.x(), q.y(), q.z(), q.w()); }

btTransform MakeTransform(const Math::Vector3& position, const Math::Quaternion& rotation = {}) {
    btTransform t;
    t.setIdentity();
    t.setOrigin(ToBullet(position));
    t.setRotation(ToBullet(rotation));
    return t;
}

bool IsTriggerObject(const btCollisionObject* object) {
    return (object->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE) != 0;
}

RigidBody* BodyOf(const btCollisionObject* object) {
    return object && object->getUserIndex() == kTagRigidBody ? static_cast<RigidBody*>(object->getUserPointer())
                                                              : nullptr;
}

CharacterController* CharacterOf(const btCollisionObject* object) {
    return object && object->getUserIndex() == kTagCharacter
               ? static_cast<CharacterController*>(object->getUserPointer())
               : nullptr;
}

void* UserDataOf(const btCollisionObject* object) {
    if (RigidBody* body = BodyOf(object)) return body->GetUserData();
    if (CharacterController* character = CharacterOf(object)) return character->GetUserData();
    return nullptr;
}

// Shared filtering for all queries
bool PassesFilter(const btCollisionObject* object, int object_group, const QueryFilter& filter) {
    if ((object_group & static_cast<int>(filter.mask)) == 0) return false;
    if (!filter.include_triggers && IsTriggerObject(object)) return false;
    if (filter.ignore_body && BodyOf(object) == filter.ignore_body) return false;
    if (filter.ignore_character && CharacterOf(object) == filter.ignore_character) return false;
    return true;
}

void FillResult(RaycastResult& result, const btCollisionObject* object) {
    result.body = BodyOf(object);
    result.character = CharacterOf(object);
    result.body_user_data = UserDataOf(object);
}

struct FilteredClosestRay : btCollisionWorld::ClosestRayResultCallback {
    const QueryFilter& filter;
    FilteredClosestRay(const btVector3& from, const btVector3& to, const QueryFilter& f)
        : ClosestRayResultCallback(from, to), filter(f) {}
    bool needsCollision(btBroadphaseProxy* proxy) const override {
        return PassesFilter(static_cast<const btCollisionObject*>(proxy->m_clientObject),
                            proxy->m_collisionFilterGroup, filter);
    }
};

struct FilteredAllRays : btCollisionWorld::AllHitsRayResultCallback {
    const QueryFilter& filter;
    FilteredAllRays(const btVector3& from, const btVector3& to, const QueryFilter& f)
        : AllHitsRayResultCallback(from, to), filter(f) {}
    bool needsCollision(btBroadphaseProxy* proxy) const override {
        return PassesFilter(static_cast<const btCollisionObject*>(proxy->m_clientObject),
                            proxy->m_collisionFilterGroup, filter);
    }
};

struct FilteredConvex : btCollisionWorld::ClosestConvexResultCallback {
    const QueryFilter& filter;
    btVector3 direction;
    bool ignore_receding = false;   // skip surfaces the sweep moves away from (touching contacts)

    FilteredConvex(const btVector3& from, const btVector3& to, const QueryFilter& f)
        : ClosestConvexResultCallback(from, to), filter(f), direction(to - from) {}

    bool needsCollision(btBroadphaseProxy* proxy) const override {
        return PassesFilter(static_cast<const btCollisionObject*>(proxy->m_clientObject),
                            proxy->m_collisionFilterGroup, filter);
    }

    btScalar addSingleResult(btCollisionWorld::LocalConvexResult& result, bool normal_in_world_space) override {
        if (ignore_receding) {
            const btVector3 normal = normal_in_world_space
                                         ? result.m_hitNormalLocal
                                         : result.m_hitCollisionObject->getWorldTransform().getBasis() *
                                               result.m_hitNormalLocal;
            if (normal.dot(direction) >= 0) return m_closestHitFraction;
        }
        return ClosestConvexResultCallback::addSingleResult(result, normal_in_world_space);
    }
};

struct OverlapCollector : btCollisionWorld::ContactResultCallback {
    const btCollisionObject* probe;
    const QueryFilter& filter;
    Array<const btCollisionObject*> hits;

    OverlapCollector(const btCollisionObject* p, const QueryFilter& f) : probe(p), filter(f) {}

    bool needsCollision(btBroadphaseProxy* proxy) const override {
        return PassesFilter(static_cast<const btCollisionObject*>(proxy->m_clientObject),
                            proxy->m_collisionFilterGroup, filter);
    }

    btScalar addSingleResult(btManifoldPoint& point, const btCollisionObjectWrapper* a, int, int,
                             const btCollisionObjectWrapper* b, int, int) override {
        if (point.getDistance() > 0) return 0;
        const btCollisionObject* other =
            a->getCollisionObject() == probe ? b->getCollisionObject() : a->getCollisionObject();
        if (std::find(hits.begin(), hits.end(), other) == hits.end()) hits.push_back(other);
        return 0;
    }
};

// Skips narrow phase between objects that can never respond to each other (static/kinematic pairs),
// but keeps such pairs when one of them is a trigger so overlaps are still reported.
class DrxDispatcher : public btCollisionDispatcher {
public:
    using btCollisionDispatcher::btCollisionDispatcher;
    bool needsCollision(const btCollisionObject* a, const btCollisionObject* b) override {
        if (a->isStaticOrKinematicObject() && b->isStaticOrKinematicObject() && !IsTriggerObject(a) &&
            !IsTriggerObject(b)) {
            return false;
        }
        if (IsTriggerObject(a) && IsTriggerObject(b)) return false;
        if (a->isStaticObject() && b->isStaticObject()) return false;   // static triggers ignore static geometry
        if (!a->isActive() && !b->isActive() && !IsTriggerObject(a) && !IsTriggerObject(b)) return false;
        return a->checkCollideWith(b) && b->checkCollideWith(a);
    }
};

} // namespace

// ============================================================================
// RigidBody
// ============================================================================

RigidBody::~RigidBody() {
    Destroy();
}

bool RigidBody::Create(btDiscreteDynamicsWorld* world, const RigidBodyDesc& desc) {
    if (!world || body_) return false;

    const CollisionShapeDesc& s = desc.shape;
    switch (s.type) {
        case CollisionShapeType::Box:
            shape_ = new btBoxShape(ToBullet(s.half_extents));
            break;
        case CollisionShapeType::Sphere:
            shape_ = new btSphereShape(s.radius);
            break;
        case CollisionShapeType::Capsule:
            shape_ = new btCapsuleShape(s.radius, std::max(0.0f, s.height - 2.0f * s.radius));
            break;
        case CollisionShapeType::Cylinder:
            shape_ = new btCylinderShape(btVector3(s.radius, s.height * 0.5f, s.radius));
            break;
        case CollisionShapeType::Cone:
            shape_ = new btConeShape(s.radius, s.height);
            break;
        case CollisionShapeType::ConvexHull: {
            if (s.vertices.size() < 4) {
                DRX_LOG_ERROR("Convex hull needs at least 4 vertices");
                return false;
            }
            auto* hull = new btConvexHullShape();
            for (const auto& v : s.vertices) hull->addPoint(ToBullet(v), false);
            hull->recalcLocalAabb();
            hull->optimizeConvexHull();
            shape_ = hull;
            break;
        }
        case CollisionShapeType::TriangleMesh: {
            if (desc.type != RigidBodyType::Static) {
                DRX_LOG_ERROR("Triangle mesh collision is only supported for static bodies");
                return false;
            }
            if (s.indices.size() < 3 || s.indices.size() % 3 != 0) {
                DRX_LOG_ERROR("Triangle mesh needs a multiple of 3 indices");
                return false;
            }
            triangle_mesh_ = new btTriangleMesh(true, false);
            for (size_t i = 0; i < s.indices.size(); i += 3) {
                const uint32 a = s.indices[i], b = s.indices[i + 1], c = s.indices[i + 2];
                if (a >= s.vertices.size() || b >= s.vertices.size() || c >= s.vertices.size()) {
                    DRX_LOG_ERROR("Triangle mesh index out of range");
                    delete triangle_mesh_;
                    triangle_mesh_ = nullptr;
                    return false;
                }
                triangle_mesh_->addTriangle(ToBullet(s.vertices[a]), ToBullet(s.vertices[b]), ToBullet(s.vertices[c]));
            }
            shape_ = new btBvhTriangleMeshShape(triangle_mesh_, true);
            break;
        }
    }

    world_ = world;
    type_ = desc.type;
    layer_ = desc.layer;
    is_trigger_ = desc.is_trigger;
    user_data_ = desc.user_data;

    const float32 mass = desc.type == RigidBodyType::Dynamic ? std::max(desc.mass, 0.001f) : 0.0f;
    btVector3 inertia(0, 0, 0);
    if (mass > 0.0f) shape_->calculateLocalInertia(mass, inertia);

    motion_state_ = new btDefaultMotionState(MakeTransform(desc.initial_transform.position,
                                                           desc.initial_transform.rotation));
    btRigidBody::btRigidBodyConstructionInfo info(mass, motion_state_, shape_, inertia);
    info.m_friction = desc.friction;
    info.m_restitution = desc.restitution;
    info.m_linearDamping = desc.linear_damping;
    info.m_angularDamping = desc.angular_damping;

    body_ = new btRigidBody(info);
    body_->setUserPointer(this);
    body_->setUserIndex(kTagRigidBody);

    int flags = body_->getCollisionFlags();
    if (desc.type == RigidBodyType::Kinematic) {
        flags |= btCollisionObject::CF_KINEMATIC_OBJECT;
        body_->setActivationState(DISABLE_DEACTIVATION);
    } else if (desc.type == RigidBodyType::Static) {
        flags |= btCollisionObject::CF_STATIC_OBJECT;
    }
    if (desc.is_trigger) flags |= btCollisionObject::CF_NO_CONTACT_RESPONSE;
    body_->setCollisionFlags(flags);

    world_->addRigidBody(body_, static_cast<int>(desc.layer), static_cast<int>(desc.mask));
    return true;
}

void RigidBody::Destroy() {
    if (world_ && body_) world_->removeRigidBody(body_);
    delete body_;
    delete motion_state_;
    delete shape_;
    delete triangle_mesh_;
    body_ = nullptr;
    motion_state_ = nullptr;
    shape_ = nullptr;
    triangle_mesh_ = nullptr;
    world_ = nullptr;
}

Math::Transform RigidBody::GetTransform() const {
    Math::Transform result;
    if (!body_) return result;
    const btTransform& t = body_->getWorldTransform();
    result.position = FromBullet(t.getOrigin());
    result.rotation = FromBullet(t.getRotation());
    return result;
}

void RigidBody::SetTransform(const Math::Transform& transform) {
    if (!body_) return;
    const btTransform t = MakeTransform(transform.position, transform.rotation);
    body_->setWorldTransform(t);
    body_->setInterpolationWorldTransform(t);
    motion_state_->setWorldTransform(t);
    if (world_) world_->updateSingleAabb(body_);
    if (type_ == RigidBodyType::Dynamic) body_->activate();
}

Math::Vector3 RigidBody::GetPosition() const {
    return body_ ? FromBullet(body_->getWorldTransform().getOrigin()) : Math::Vector3();
}

void RigidBody::SetPosition(const Math::Vector3& position) {
    Math::Transform t = GetTransform();
    t.position = position;
    SetTransform(t);
}

Math::Quaternion RigidBody::GetRotation() const {
    return body_ ? FromBullet(body_->getWorldTransform().getRotation()) : Math::Quaternion();
}

void RigidBody::SetRotation(const Math::Quaternion& rotation) {
    Math::Transform t = GetTransform();
    t.rotation = rotation;
    SetTransform(t);
}

Math::Vector3 RigidBody::GetLinearVelocity() const {
    return body_ ? FromBullet(body_->getLinearVelocity()) : Math::Vector3();
}

void RigidBody::SetLinearVelocity(const Math::Vector3& velocity) {
    if (!body_) return;
    body_->activate();
    body_->setLinearVelocity(ToBullet(velocity));
}

Math::Vector3 RigidBody::GetAngularVelocity() const {
    return body_ ? FromBullet(body_->getAngularVelocity()) : Math::Vector3();
}

void RigidBody::SetAngularVelocity(const Math::Vector3& velocity) {
    if (!body_) return;
    body_->activate();
    body_->setAngularVelocity(ToBullet(velocity));
}

void RigidBody::ApplyForce(const Math::Vector3& force) {
    if (!body_) return;
    body_->activate();
    body_->applyCentralForce(ToBullet(force));
}

void RigidBody::ApplyForceAtPoint(const Math::Vector3& force, const Math::Vector3& point) {
    if (!body_) return;
    body_->activate();
    body_->applyForce(ToBullet(force), ToBullet(point - GetPosition()));
}

void RigidBody::ApplyImpulse(const Math::Vector3& impulse) {
    if (!body_) return;
    body_->activate();
    body_->applyCentralImpulse(ToBullet(impulse));
}

void RigidBody::ApplyImpulseAtPoint(const Math::Vector3& impulse, const Math::Vector3& point) {
    if (!body_) return;
    body_->activate();
    body_->applyImpulse(ToBullet(impulse), ToBullet(point - GetPosition()));
}

void RigidBody::ApplyTorque(const Math::Vector3& torque) {
    if (!body_) return;
    body_->activate();
    body_->applyTorque(ToBullet(torque));
}

void RigidBody::ApplyTorqueImpulse(const Math::Vector3& torque) {
    if (!body_) return;
    body_->activate();
    body_->applyTorqueImpulse(ToBullet(torque));
}

float32 RigidBody::GetMass() const {
    if (!body_) return 0.0f;
    const float32 inv_mass = body_->getInvMass();
    return inv_mass > 0.0f ? 1.0f / inv_mass : 0.0f;
}

void RigidBody::SetMass(float32 mass) {
    if (!body_ || type_ != RigidBodyType::Dynamic) return;
    mass = std::max(mass, 0.001f);
    btVector3 inertia(0, 0, 0);
    shape_->calculateLocalInertia(mass, inertia);
    body_->setMassProps(mass, inertia);
    body_->updateInertiaTensor();
}

void RigidBody::SetFriction(float32 friction) {
    if (body_) body_->setFriction(friction);
}

void RigidBody::SetRestitution(float32 restitution) {
    if (body_) body_->setRestitution(restitution);
}

void RigidBody::SetDamping(float32 linear, float32 angular) {
    if (body_) body_->setDamping(linear, angular);
}

void RigidBody::SetActive(bool active) {
    if (!body_ || type_ != RigidBodyType::Dynamic) return;
    if (active) body_->activate(true);
    else body_->setActivationState(WANTS_DEACTIVATION);
}

bool RigidBody::IsActive() const {
    return body_ && body_->isActive();
}

void RigidBody::SetGravityEnabled(bool enabled) {
    if (!body_ || !world_) return;
    body_->setGravity(enabled ? world_->getGravity() : btVector3(0, 0, 0));
    body_->setFlags(enabled ? body_->getFlags() & ~BT_DISABLE_WORLD_GRAVITY : body_->getFlags() | BT_DISABLE_WORLD_GRAVITY);
}

// ============================================================================
// CharacterController
// ============================================================================

CharacterController::~CharacterController() {
    Destroy();
}

bool CharacterController::Create(btDiscreteDynamicsWorld* world, const CharacterSettings& settings,
                                 const Math::Vector3& feet_position) {
    if (!world || object_) return false;
    if (settings.radius <= 0.0f || settings.height < settings.radius * 2.0f) {
        DRX_LOG_ERROR("Character capsule needs radius > 0 and height >= 2 * radius");
        return false;
    }

    world_ = world;
    settings_ = settings;
    min_ground_normal_y_ = std::cos(settings.max_slope_degrees * 3.14159265f / 180.0f);
    shape_ = new btCapsuleShape(settings.radius, settings.height - 2.0f * settings.radius);

    object_ = new btCollisionObject();
    object_->setCollisionShape(shape_);
    object_->setCollisionFlags(btCollisionObject::CF_KINEMATIC_OBJECT);
    object_->setActivationState(DISABLE_DEACTIVATION);
    object_->setUserPointer(this);
    object_->setUserIndex(kTagCharacter);

    state_ = {};
    state_.position = feet_position;
    object_->setWorldTransform(MakeTransform(GetCenter()));
    // Triggers must see the character even though they never block it
    world_->addCollisionObject(object_, static_cast<int>(settings.layer),
                               static_cast<int>(settings.mask | CollisionLayer::Trigger));
    return true;
}

void CharacterController::Destroy() {
    if (world_ && object_) world_->removeCollisionObject(object_);
    delete object_;
    delete shape_;
    object_ = nullptr;
    shape_ = nullptr;
    world_ = nullptr;
}

Math::Vector3 CharacterController::GetCenter() const {
    return state_.position + Math::Vector3(0.0f, settings_.height * 0.5f, 0.0f);
}

void CharacterController::SyncObject() {
    if (!object_) return;
    object_->setWorldTransform(MakeTransform(GetCenter()));
    world_->updateSingleAabb(object_);
}

void CharacterController::SetState(const CharacterState& state) {
    state_ = state;
    SyncObject();
}

void CharacterController::SetPosition(const Math::Vector3& feet_position) {
    state_.position = feet_position;
    SyncObject();
}

bool CharacterController::IsWalkable(const Math::Vector3& normal) const {
    return normal.y >= min_ground_normal_y_;
}

CharacterController::SweepHit CharacterController::Sweep(const Math::Vector3& from_feet,
                                                        const Math::Vector3& to_feet) const {
    SweepHit result;
    const Math::Vector3 offset(0.0f, settings_.height * 0.5f, 0.0f);
    const btTransform from = MakeTransform(from_feet + offset);
    const btTransform to = MakeTransform(to_feet + offset);

    QueryFilter filter;
    filter.mask = settings_.mask;
    filter.ignore_character = this;
    FilteredConvex callback(from.getOrigin(), to.getOrigin(), filter);
    callback.ignore_receding = true;
    world_->convexSweepTest(shape_, from, to, callback, 0.0f);

    if (callback.hasHit()) {
        result.hit = true;
        result.fraction = callback.m_closestHitFraction;
        result.normal = FromBullet(callback.m_hitNormalWorld).Normalized();
        result.point = FromBullet(callback.m_hitPointWorld);
    }
    return result;
}

Math::Vector3 CharacterController::SlideMove(const Math::Vector3& start, const Math::Vector3& displacement,
                                             Math::Vector3* velocity) const {
    constexpr int kMaxIterations = 4;
    Math::Vector3 position = start;
    Math::Vector3 remaining = displacement;
    Math::Vector3 planes[kMaxIterations];
    int plane_count = 0;
    const bool horizontal_move = std::abs(displacement.y) < 1e-6f;

    auto clip = [](const Math::Vector3& v, const Math::Vector3& n) {
        const float32 into = Math::Vector3::Dot(v, n);
        return into < 0.0f ? v - n * into : v;
    };

    for (int iteration = 0; iteration < kMaxIterations; ++iteration) {
        const float32 length = remaining.Length();
        if (length < 1e-6f) break;

        const SweepHit hit = Sweep(position, position + remaining);
        if (!hit.hit) {
            position += remaining;
            break;
        }

        const Math::Vector3 direction = remaining / length;
        const float32 travel = std::max(0.0f, hit.fraction * length - settings_.skin_width);
        position += direction * travel;
        remaining = direction * (length - travel);

        // Steep slopes behave like walls for walking so they cannot be climbed
        Math::Vector3 normal = hit.normal;
        if (horizontal_move && !IsWalkable(normal) && normal.y > 0.0f) {
            normal.y = 0.0f;
            normal = normal.LengthSquared() > 1e-8f ? normal.Normalized() : hit.normal;
        }
        planes[plane_count++] = normal;

        remaining = clip(remaining, normal);
        if (velocity) *velocity = clip(*velocity, normal);

        // Moving into a crease between two planes: slide along their intersection
        for (int i = 0; i < plane_count - 1; ++i) {
            if (Math::Vector3::Dot(remaining, planes[i]) < -1e-5f) {
                Math::Vector3 crease = Math::Vector3::Cross(planes[i], normal);
                if (crease.LengthSquared() < 1e-8f) {
                    remaining = Math::Vector3::Zero;
                } else {
                    crease = crease.Normalized();
                    remaining = crease * Math::Vector3::Dot(remaining, crease);
                    if (velocity) *velocity = crease * Math::Vector3::Dot(*velocity, crease);
                }
                break;
            }
        }

        if (Math::Vector3::Dot(remaining, displacement) <= 0.0f) break;   // never move backwards
    }
    return position;
}

bool CharacterController::ProbeGround(Math::Vector3& feet, float32 distance, Math::Vector3& normal,
                                      float32* contact_height) const {
    const float32 length = distance + settings_.skin_width;
    const SweepHit hit = Sweep(feet, feet - Math::Vector3(0.0f, length, 0.0f));
    if (!hit.hit) return false;

    Math::Vector3 ground_normal = hit.normal;
    if (!IsWalkable(ground_normal)) {
        // Touching an edge (stairs, ledges): the contact normal is tilted, so check the surface right below it
        QueryFilter filter;
        filter.mask = settings_.mask;
        filter.ignore_character = this;
        const Math::Vector3 from = hit.point + Math::Vector3(0.0f, 0.05f, 0.0f);
        const Math::Vector3 to = hit.point - Math::Vector3(0.0f, 0.05f, 0.0f);
        FilteredClosestRay ray(ToBullet(from), ToBullet(to), filter);
        world_->rayTest(ToBullet(from), ToBullet(to), ray);
        if (!ray.hasHit()) return false;
        ground_normal = FromBullet(ray.m_hitNormalWorld).Normalized();
        if (!IsWalkable(ground_normal)) return false;
    }

    const float32 drop = std::max(0.0f, hit.fraction * length - settings_.skin_width);
    feet.y -= drop;
    normal = ground_normal;
    if (contact_height) *contact_height = hit.point.y;
    return true;
}

float32 CharacterController::MoveAndSlide(const Math::Vector3& displacement) {
    if (!object_) return 0.0f;
    const Math::Vector3 before = state_.position;
    state_.position = SlideMove(state_.position, displacement, nullptr);
    SyncObject();
    return Math::Vector3::Distance(before, state_.position);
}

void CharacterController::Move(const Math::Vector3& wish_velocity, float32 jump_speed, float32 dt) {
    if (!object_ || dt <= 0.0f) return;

    ResolvePenetration();

    Math::Vector3 velocity(wish_velocity.x, state_.velocity.y, wish_velocity.z);
    bool jumped = false;
    if (state_.grounded && jump_speed > 0.0f) {
        velocity.y = jump_speed;
        jumped = true;
    } else if (state_.grounded) {
        velocity.y = 0.0f;
    } else {
        velocity.y -= settings_.gravity * dt;
    }

    Math::Vector3 position = state_.position;
    Math::Vector3 ground_normal = Math::Vector3::Up;
    bool grounded = false;

    if (state_.grounded && !jumped) {
        const Math::Vector3 horizontal(velocity.x * dt, 0.0f, velocity.z * dt);

        // Plain slide along the ground
        Math::Vector3 walked = SlideMove(position, horizontal, nullptr);

        // Stepped variant: up, across, down; used when it gets further (stairs, ledges)
        if (settings_.step_height > 0.0f && horizontal.LengthSquared() > 1e-10f) {
            const Math::Vector3 up(0.0f, settings_.step_height, 0.0f);
            const SweepHit up_hit = Sweep(position, position + up);
            const float32 raised =
                up_hit.hit ? std::max(0.0f, up_hit.fraction * settings_.step_height - settings_.skin_width)
                           : settings_.step_height;
            Math::Vector3 stepped = SlideMove(position + Math::Vector3(0.0f, raised, 0.0f), horizontal, nullptr);
            Math::Vector3 step_normal;
            float32 contact_height = 0.0f;
            if (ProbeGround(stepped, raised, step_normal, &contact_height) &&
                contact_height - position.y <= settings_.step_height + settings_.skin_width) {
                auto flat_distance = [&](const Math::Vector3& p) {
                    const float32 dx = p.x - position.x, dz = p.z - position.z;
                    return dx * dx + dz * dz;
                };
                if (flat_distance(stepped) > flat_distance(walked) + 1e-6f) walked = stepped;
            }
        }
        position = walked;
        grounded = ProbeGround(position, settings_.ground_snap_distance, ground_normal);
        if (grounded) velocity.y = 0.0f;
    } else {
        position = SlideMove(position, velocity * dt, &velocity);
        if (velocity.y <= 0.0f) {
            grounded = ProbeGround(position, settings_.skin_width * 2.0f, ground_normal);
            if (grounded) velocity.y = 0.0f;
        }
    }

    state_.position = position;
    state_.velocity = velocity;
    state_.grounded = grounded;
    state_.ground_normal = grounded ? ground_normal : Math::Vector3::Up;
    SyncObject();
}

bool CharacterController::ResolvePenetration() {
    if (!object_) return true;

    struct PenetrationCollector : btCollisionWorld::ContactResultCallback {
        const btCollisionObject* self;
        const QueryFilter& filter;
        float32 deepest = 0.0f;
        btVector3 push{0, 0, 0};

        PenetrationCollector(const btCollisionObject* s, const QueryFilter& f) : self(s), filter(f) {}

        bool needsCollision(btBroadphaseProxy* proxy) const override {
            return PassesFilter(static_cast<const btCollisionObject*>(proxy->m_clientObject),
                                proxy->m_collisionFilterGroup, filter);
        }

        btScalar addSingleResult(btManifoldPoint& point, const btCollisionObjectWrapper* a, int, int,
                                 const btCollisionObjectWrapper*, int, int) override {
            const float32 depth = -point.getDistance();
            if (depth > deepest) {
                deepest = depth;
                // m_normalWorldOnB points from B towards A
                push = a->getCollisionObject() == self ? point.m_normalWorldOnB : -point.m_normalWorldOnB;
            }
            return 0;
        }
    };

    QueryFilter filter;
    filter.mask = settings_.mask;
    filter.ignore_character = this;

    constexpr int kMaxIterations = 8;
    float32 remaining_depth = 0.0f;
    for (int iteration = 0; iteration < kMaxIterations; ++iteration) {
        SyncObject();
        PenetrationCollector collector(object_, filter);
        world_->contactTest(object_, collector);
        remaining_depth = collector.deepest;
        if (remaining_depth <= 1e-4f) break;
        state_.position += FromBullet(collector.push) * (remaining_depth + 1e-4f);
    }
    SyncObject();

    crushed_ = remaining_depth > settings_.crush_tolerance;
    return !crushed_;
}

// ============================================================================
// PhysicsWorld
// ============================================================================

PhysicsWorld::~PhysicsWorld() {
    Shutdown();
}

bool PhysicsWorld::Initialize(const Math::Vector3& gravity) {
    if (dynamics_world_) return true;

    collision_config_ = new btDefaultCollisionConfiguration();
    dispatcher_ = new DrxDispatcher(collision_config_);
    broadphase_ = new btDbvtBroadphase();
    solver_ = new btSequentialImpulseConstraintSolver();
    dynamics_world_ = new btDiscreteDynamicsWorld(dispatcher_, broadphase_, solver_, collision_config_);
    dynamics_world_->setGravity(ToBullet(gravity));

    DRX_LOG_INFO("Physics world initialized");
    return true;
}

void PhysicsWorld::Shutdown() {
    if (!dynamics_world_) return;

    active_pairs_.clear();
    for (auto& controller : character_controllers_) controller->Destroy();
    for (auto& body : rigid_bodies_) body->Destroy();
    character_controllers_.clear();
    rigid_bodies_.clear();

    delete dynamics_world_;
    delete solver_;
    delete broadphase_;
    delete dispatcher_;
    delete collision_config_;
    dynamics_world_ = nullptr;
    solver_ = nullptr;
    broadphase_ = nullptr;
    dispatcher_ = nullptr;
    collision_config_ = nullptr;

    DRX_LOG_INFO("Physics world shutdown");
}

void PhysicsWorld::Step(float32 delta_time) {
    if (!dynamics_world_ || delta_time <= 0.0f) return;
    dynamics_world_->stepSimulation(delta_time, 0);   // exactly one step of delta_time
    ProcessCollisions();
}

SharedPtr<RigidBody> PhysicsWorld::CreateRigidBody(const RigidBodyDesc& desc) {
    if (!dynamics_world_) return nullptr;
    auto body = MakeShared<RigidBody>();
    if (!body->Create(dynamics_world_, desc)) return nullptr;
    rigid_bodies_.push_back(body);
    return body;
}

void PhysicsWorld::DestroyRigidBody(const SharedPtr<RigidBody>& body) {
    if (!body) return;
    const btCollisionObject* object = body->GetBulletBody();
    for (auto it = active_pairs_.begin(); it != active_pairs_.end();) {
        it = (it->first == object || it->second == object) ? active_pairs_.erase(it) : std::next(it);
    }
    body->Destroy();
    rigid_bodies_.erase(std::remove(rigid_bodies_.begin(), rigid_bodies_.end(), body), rigid_bodies_.end());
}

SharedPtr<CharacterController> PhysicsWorld::CreateCharacterController(const CharacterSettings& settings,
                                                                       const Math::Vector3& feet_position) {
    if (!dynamics_world_) return nullptr;
    auto controller = MakeShared<CharacterController>();
    if (!controller->Create(dynamics_world_, settings, feet_position)) return nullptr;
    character_controllers_.push_back(controller);
    return controller;
}

void PhysicsWorld::DestroyCharacterController(const SharedPtr<CharacterController>& controller) {
    if (!controller) return;
    const btCollisionObject* object = controller->GetCollisionObject();
    for (auto it = active_pairs_.begin(); it != active_pairs_.end();) {
        it = (it->first == object || it->second == object) ? active_pairs_.erase(it) : std::next(it);
    }
    controller->Destroy();
    character_controllers_.erase(std::remove(character_controllers_.begin(), character_controllers_.end(), controller),
                                 character_controllers_.end());
}

RaycastResult PhysicsWorld::Raycast(const Math::Vector3& from, const Math::Vector3& to,
                                    const QueryFilter& filter) const {
    RaycastResult result;
    if (!dynamics_world_) return result;

    FilteredClosestRay callback(ToBullet(from), ToBullet(to), filter);
    dynamics_world_->rayTest(ToBullet(from), ToBullet(to), callback);
    if (callback.hasHit()) {
        result.hit = true;
        result.point = FromBullet(callback.m_hitPointWorld);
        result.normal = FromBullet(callback.m_hitNormalWorld).Normalized();
        result.fraction = callback.m_closestHitFraction;
        result.distance = result.fraction * Math::Vector3::Distance(from, to);
        FillResult(result, callback.m_collisionObject);
    }
    return result;
}

Array<RaycastResult> PhysicsWorld::RaycastAll(const Math::Vector3& from, const Math::Vector3& to,
                                              const QueryFilter& filter) const {
    Array<RaycastResult> results;
    if (!dynamics_world_) return results;

    FilteredAllRays callback(ToBullet(from), ToBullet(to), filter);
    dynamics_world_->rayTest(ToBullet(from), ToBullet(to), callback);

    const float32 length = Math::Vector3::Distance(from, to);
    for (int i = 0; i < callback.m_collisionObjects.size(); ++i) {
        RaycastResult result;
        result.hit = true;
        result.point = FromBullet(callback.m_hitPointWorld[i]);
        result.normal = FromBullet(callback.m_hitNormalWorld[i]).Normalized();
        result.fraction = callback.m_hitFractions[i];
        result.distance = result.fraction * length;
        FillResult(result, callback.m_collisionObjects[i]);
        results.push_back(result);
    }
    std::sort(results.begin(), results.end(),
              [](const RaycastResult& a, const RaycastResult& b) { return a.fraction < b.fraction; });
    return results;
}

RaycastResult PhysicsWorld::SphereCast(const Math::Vector3& from, const Math::Vector3& to, float32 radius,
                                       const QueryFilter& filter) const {
    RaycastResult result;
    if (!dynamics_world_) return result;

    btSphereShape sphere(radius);
    FilteredConvex callback(ToBullet(from), ToBullet(to), filter);
    dynamics_world_->convexSweepTest(&sphere, MakeTransform(from), MakeTransform(to), callback, 0.0f);
    if (callback.hasHit()) {
        result.hit = true;
        result.point = FromBullet(callback.m_hitPointWorld);
        result.normal = FromBullet(callback.m_hitNormalWorld).Normalized();
        result.fraction = callback.m_closestHitFraction;
        result.distance = result.fraction * Math::Vector3::Distance(from, to);
        FillResult(result, callback.m_hitCollisionObject);
    }
    return result;
}

Array<void*> PhysicsWorld::Overlap(btCollisionShape& shape, const Math::Vector3& center,
                                   const Math::Quaternion& rotation, const QueryFilter& filter) const {
    Array<void*> results;
    if (!dynamics_world_) return results;

    btCollisionObject probe;
    probe.setCollisionShape(&shape);
    probe.setWorldTransform(MakeTransform(center, rotation));

    OverlapCollector collector(&probe, filter);
    dynamics_world_->contactTest(&probe, collector);
    for (const btCollisionObject* object : collector.hits) {
        results.push_back(UserDataOf(object));
    }
    return results;
}

Array<void*> PhysicsWorld::OverlapSphere(const Math::Vector3& center, float32 radius, const QueryFilter& filter) const {
    btSphereShape sphere(radius);
    return Overlap(sphere, center, {}, filter);
}

Array<void*> PhysicsWorld::OverlapBox(const Math::Vector3& center, const Math::Vector3& half_extents,
                                      const Math::Quaternion& rotation, const QueryFilter& filter) const {
    btBoxShape box(ToBullet(half_extents));
    return Overlap(box, center, rotation, filter);
}

void PhysicsWorld::SetGravity(const Math::Vector3& gravity) {
    if (dynamics_world_) dynamics_world_->setGravity(ToBullet(gravity));
}

Math::Vector3 PhysicsWorld::GetGravity() const {
    return dynamics_world_ ? FromBullet(dynamics_world_->getGravity()) : Math::Vector3(0, -9.81f, 0);
}

void PhysicsWorld::ProcessCollisions() {
    std::set<std::pair<const btCollisionObject*, const btCollisionObject*>> current;

    const int manifold_count = dispatcher_->getNumManifolds();
    for (int i = 0; i < manifold_count; ++i) {
        btPersistentManifold* manifold = dispatcher_->getManifoldByIndexInternal(i);
        const btCollisionObject* a = manifold->getBody0();
        const btCollisionObject* b = manifold->getBody1();

        const btManifoldPoint* touching = nullptr;
        for (int j = 0; j < manifold->getNumContacts(); ++j) {
            if (manifold->getContactPoint(j).getDistance() <= 0.0f) {
                touching = &manifold->getContactPoint(j);
                break;
            }
        }
        if (!touching) continue;

        if (std::less<const btCollisionObject*>()(b, a)) std::swap(a, b);
        const auto pair = std::make_pair(a, b);
        current.insert(pair);
        if (!collision_callback_) continue;

        const bool is_new = active_pairs_.count(pair) == 0;
        if (IsTriggerObject(a) || IsTriggerObject(b)) {
            if (is_new) {
                const btCollisionObject* trigger = IsTriggerObject(a) ? a : b;
                const btCollisionObject* other = trigger == a ? b : a;
                collision_callback_->OnTriggerEnter(UserDataOf(trigger), UserDataOf(other));
            }
            continue;
        }

        ContactPoint contact;
        const bool flipped = manifold->getBody0() != a;
        contact.position_world_on_a = FromBullet(flipped ? touching->getPositionWorldOnB() : touching->getPositionWorldOnA());
        contact.position_world_on_b = FromBullet(flipped ? touching->getPositionWorldOnA() : touching->getPositionWorldOnB());
        contact.normal_on_b = FromBullet(flipped ? -touching->m_normalWorldOnB : touching->m_normalWorldOnB);
        contact.distance = touching->getDistance();
        contact.impulse = touching->getAppliedImpulse();
        if (is_new) collision_callback_->OnCollisionBegin(UserDataOf(a), UserDataOf(b), contact);
        else collision_callback_->OnCollisionStay(UserDataOf(a), UserDataOf(b), contact);
    }

    if (collision_callback_) {
        for (const auto& pair : active_pairs_) {
            if (current.count(pair)) continue;
            const auto [a, b] = pair;
            if (IsTriggerObject(a) || IsTriggerObject(b)) {
                const btCollisionObject* trigger = IsTriggerObject(a) ? a : b;
                collision_callback_->OnTriggerExit(UserDataOf(trigger), UserDataOf(trigger == a ? b : a));
            } else {
                collision_callback_->OnCollisionEnd(UserDataOf(a), UserDataOf(b));
            }
        }
    }
    active_pairs_ = std::move(current);
}

} // namespace DRX::Physics

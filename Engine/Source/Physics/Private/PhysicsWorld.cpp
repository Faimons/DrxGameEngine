/**
 * DRX Engine - Physics World Implementation
 * Bullet Physics integration
 */

#include "Physics/Public/PhysicsWorld.h"
#include "Core/Public/Log.h"

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btShapeHull.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>

namespace DRX::Physics {

// Helper conversions
static btVector3 ToBullet(const Math::Vector3& v) {
    return btVector3(v.x, v.y, v.z);
}

static Math::Vector3 FromBullet(const btVector3& v) {
    return Math::Vector3{v.x(), v.y(), v.z()};
}

static btQuaternion ToBullet(const Math::Quaternion& q) {
    return btQuaternion(q.x, q.y, q.z, q.w);
}

static Math::Quaternion FromBullet(const btQuaternion& q) {
    return Math::Quaternion{q.x(), q.y(), q.z(), q.w()};
}

// ============================================================================
// RigidBody Implementation
// ============================================================================

RigidBody::~RigidBody() {
    Destroy();
}

bool RigidBody::Create(btDiscreteDynamicsWorld* world, const RigidBodyDesc& desc) {
    world_ = world;
    type_ = desc.type;
    user_data_ = desc.user_data;

    // Create collision shape
    switch (desc.shape.type) {
        case CollisionShapeType::Box:
            shape_ = new btBoxShape(ToBullet(desc.shape.half_extents));
            break;
        case CollisionShapeType::Sphere:
            shape_ = new btSphereShape(desc.shape.radius);
            break;
        case CollisionShapeType::Capsule:
            shape_ = new btCapsuleShape(desc.shape.radius, desc.shape.height);
            break;
        case CollisionShapeType::Cylinder:
            shape_ = new btCylinderShape(btVector3(desc.shape.radius, desc.shape.height * 0.5f, desc.shape.radius));
            break;
        case CollisionShapeType::Cone:
            shape_ = new btConeShape(desc.shape.radius, desc.shape.height);
            break;
        case CollisionShapeType::ConvexHull: {
            auto* hull = new btConvexHullShape();
            for (const auto& v : desc.shape.vertices) {
                hull->addPoint(ToBullet(v), false);
            }
            hull->recalcLocalAabb();
            shape_ = hull;
            break;
        }
        default:
            shape_ = new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
            break;
    }

    // Calculate local inertia
    btVector3 local_inertia(0, 0, 0);
    float mass = (desc.type == RigidBodyType::Static) ? 0.0f : desc.mass;
    if (mass > 0.0f) {
        shape_->calculateLocalInertia(mass, local_inertia);
    }

    // Create motion state
    btTransform start_transform;
    start_transform.setIdentity();
    start_transform.setOrigin(ToBullet(desc.initial_transform.position));
    start_transform.setRotation(ToBullet(desc.initial_transform.rotation));

    motion_state_ = new btDefaultMotionState(start_transform);

    // Create rigid body
    btRigidBody::btRigidBodyConstructionInfo rb_info(mass, motion_state_, shape_, local_inertia);
    rb_info.m_friction = desc.friction;
    rb_info.m_restitution = desc.restitution;
    rb_info.m_linearDamping = desc.linear_damping;
    rb_info.m_angularDamping = desc.angular_damping;

    body_ = new btRigidBody(rb_info);
    body_->setUserPointer(this);

    if (desc.type == RigidBodyType::Kinematic) {
        body_->setCollisionFlags(body_->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
        body_->setActivationState(DISABLE_DEACTIVATION);
    }

    world_->addRigidBody(body_, static_cast<int16>(desc.layer), static_cast<int16>(desc.mask));

    return true;
}

void RigidBody::Destroy() {
    if (world_ && body_) {
        world_->removeRigidBody(body_);
    }

    delete body_;
    delete motion_state_;
    delete shape_;

    body_ = nullptr;
    motion_state_ = nullptr;
    shape_ = nullptr;
    world_ = nullptr;
}

Math::Transform RigidBody::GetTransform() const {
    if (!body_) return Math::Transform{};

    btTransform trans;
    body_->getMotionState()->getWorldTransform(trans);

    Math::Transform result;
    result.position = FromBullet(trans.getOrigin());
    result.rotation = FromBullet(trans.getRotation());
    result.scale = Math::Vector3{1, 1, 1};
    return result;
}

void RigidBody::SetTransform(const Math::Transform& transform) {
    if (!body_) return;

    btTransform trans;
    trans.setOrigin(ToBullet(transform.position));
    trans.setRotation(ToBullet(transform.rotation));

    body_->setWorldTransform(trans);
    body_->getMotionState()->setWorldTransform(trans);
}

Math::Vector3 RigidBody::GetPosition() const {
    if (!body_) return Math::Vector3{};
    return FromBullet(body_->getWorldTransform().getOrigin());
}

void RigidBody::SetPosition(const Math::Vector3& position) {
    if (!body_) return;

    btTransform trans = body_->getWorldTransform();
    trans.setOrigin(ToBullet(position));
    body_->setWorldTransform(trans);
}

Math::Quaternion RigidBody::GetRotation() const {
    if (!body_) return Math::Quaternion{};
    return FromBullet(body_->getWorldTransform().getRotation());
}

void RigidBody::SetRotation(const Math::Quaternion& rotation) {
    if (!body_) return;

    btTransform trans = body_->getWorldTransform();
    trans.setRotation(ToBullet(rotation));
    body_->setWorldTransform(trans);
}

Math::Vector3 RigidBody::GetLinearVelocity() const {
    if (!body_) return Math::Vector3{};
    return FromBullet(body_->getLinearVelocity());
}

void RigidBody::SetLinearVelocity(const Math::Vector3& velocity) {
    if (!body_) return;
    body_->setLinearVelocity(ToBullet(velocity));
}

Math::Vector3 RigidBody::GetAngularVelocity() const {
    if (!body_) return Math::Vector3{};
    return FromBullet(body_->getAngularVelocity());
}

void RigidBody::SetAngularVelocity(const Math::Vector3& velocity) {
    if (!body_) return;
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
    body_->applyForce(ToBullet(force), ToBullet(point));
}

void RigidBody::ApplyImpulse(const Math::Vector3& impulse) {
    if (!body_) return;
    body_->activate();
    body_->applyCentralImpulse(ToBullet(impulse));
}

void RigidBody::ApplyImpulseAtPoint(const Math::Vector3& impulse, const Math::Vector3& point) {
    if (!body_) return;
    body_->activate();
    body_->applyImpulse(ToBullet(impulse), ToBullet(point));
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
    float inv_mass = body_->getInvMass();
    return inv_mass > 0.0f ? 1.0f / inv_mass : 0.0f;
}

void RigidBody::SetMass(float32 mass) {
    if (!body_ || !shape_) return;

    btVector3 inertia(0, 0, 0);
    if (mass > 0.0f) {
        shape_->calculateLocalInertia(mass, inertia);
    }
    body_->setMassProps(mass, inertia);
}

void RigidBody::SetFriction(float32 friction) {
    if (!body_) return;
    body_->setFriction(friction);
}

void RigidBody::SetRestitution(float32 restitution) {
    if (!body_) return;
    body_->setRestitution(restitution);
}

void RigidBody::SetDamping(float32 linear, float32 angular) {
    if (!body_) return;
    body_->setDamping(linear, angular);
}

void RigidBody::SetKinematic(bool kinematic) {
    if (!body_) return;

    if (kinematic) {
        body_->setCollisionFlags(body_->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
        body_->setActivationState(DISABLE_DEACTIVATION);
    } else {
        body_->setCollisionFlags(body_->getCollisionFlags() & ~btCollisionObject::CF_KINEMATIC_OBJECT);
        body_->setActivationState(ACTIVE_TAG);
    }
}

bool RigidBody::IsKinematic() const {
    if (!body_) return false;
    return body_->isKinematicObject();
}

void RigidBody::SetActive(bool active) {
    if (!body_) return;
    if (active) {
        body_->activate();
    } else {
        body_->setActivationState(WANTS_DEACTIVATION);
    }
}

bool RigidBody::IsActive() const {
    if (!body_) return false;
    return body_->isActive();
}

void RigidBody::SetGravityEnabled(bool enabled) {
    if (!body_) return;
    if (enabled) {
        body_->setGravity(world_->getGravity());
    } else {
        body_->setGravity(btVector3(0, 0, 0));
    }
}

// ============================================================================
// CharacterController Implementation
// ============================================================================

CharacterController::~CharacterController() {
    Destroy();
}

bool CharacterController::Create(btDiscreteDynamicsWorld* world, float32 radius, float32 height,
                                  const Math::Vector3& position) {
    world_ = world;

    shape_ = new btCapsuleShape(radius, height);

    btTransform start_transform;
    start_transform.setIdentity();
    start_transform.setOrigin(ToBullet(position));

    btVector3 inertia(0, 0, 0);
    auto* motion_state = new btDefaultMotionState(start_transform);

    btRigidBody::btRigidBodyConstructionInfo info(0.0f, motion_state, shape_, inertia);
    body_ = new btRigidBody(info);
    body_->setCollisionFlags(btCollisionObject::CF_KINEMATIC_OBJECT);
    body_->setActivationState(DISABLE_DEACTIVATION);

    world_->addRigidBody(body_);

    return true;
}

void CharacterController::Destroy() {
    if (world_ && body_) {
        world_->removeRigidBody(body_);
    }
    delete body_;
    delete shape_;
    body_ = nullptr;
    shape_ = nullptr;
    world_ = nullptr;
}

void CharacterController::Move(const Math::Vector3& displacement, float32 delta_time) {
    if (!body_) return;

    Math::Vector3 pos = GetPosition();

    // Apply gravity
    if (!is_grounded_) {
        velocity_.y -= gravity_ * delta_time;
    }

    Math::Vector3 movement = displacement + velocity_ * delta_time;
    pos = pos + movement;

    SetPosition(pos);
    UpdateGrounded();
}

void CharacterController::Jump(float32 force) {
    if (is_grounded_) {
        velocity_.y = force;
        is_grounded_ = false;
    }
}

Math::Vector3 CharacterController::GetPosition() const {
    if (!body_) return Math::Vector3{};
    return FromBullet(body_->getWorldTransform().getOrigin());
}

void CharacterController::SetPosition(const Math::Vector3& position) {
    if (!body_) return;

    btTransform trans = body_->getWorldTransform();
    trans.setOrigin(ToBullet(position));
    body_->setWorldTransform(trans);
}

void CharacterController::UpdateGrounded() {
    Math::Vector3 pos = GetPosition();
    Math::Vector3 from = pos;
    Math::Vector3 to = pos - Math::Vector3{0, step_height_ + 0.1f, 0};

    btCollisionWorld::ClosestRayResultCallback callback(ToBullet(from), ToBullet(to));
    world_->rayTest(ToBullet(from), ToBullet(to), callback);

    is_grounded_ = callback.hasHit();
    if (is_grounded_ && velocity_.y < 0) {
        velocity_.y = 0;
    }
}

// ============================================================================
// PhysicsWorld Implementation
// ============================================================================

PhysicsWorld::~PhysicsWorld() {
    Shutdown();
}

bool PhysicsWorld::Initialize(const Math::Vector3& gravity) {
    collision_config_ = new btDefaultCollisionConfiguration();
    dispatcher_ = new btCollisionDispatcher(collision_config_);
    broadphase_ = new btDbvtBroadphase();
    solver_ = new btSequentialImpulseConstraintSolver();

    dynamics_world_ = new btDiscreteDynamicsWorld(dispatcher_, broadphase_, solver_, collision_config_);
    dynamics_world_->setGravity(ToBullet(gravity));

    DRX_LOG_INFO("Physics world initialized");
    return true;
}

void PhysicsWorld::Shutdown() {
    rigid_bodies_.clear();
    character_controllers_.clear();

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

void PhysicsWorld::Step(float32 delta_time, int32 max_sub_steps) {
    if (!dynamics_world_) return;

    dynamics_world_->stepSimulation(delta_time, max_sub_steps);
    ProcessCollisions();
}

SharedPtr<RigidBody> PhysicsWorld::CreateRigidBody(const RigidBodyDesc& desc) {
    auto body = MakeShared<RigidBody>();
    if (!body->Create(dynamics_world_, desc)) {
        return nullptr;
    }
    rigid_bodies_.push_back(body);
    return body;
}

void PhysicsWorld::DestroyRigidBody(SharedPtr<RigidBody> body) {
    auto it = std::find(rigid_bodies_.begin(), rigid_bodies_.end(), body);
    if (it != rigid_bodies_.end()) {
        rigid_bodies_.erase(it);
    }
}

SharedPtr<CharacterController> PhysicsWorld::CreateCharacterController(float32 radius, float32 height,
                                                                        const Math::Vector3& position) {
    auto controller = MakeShared<CharacterController>();
    if (!controller->Create(dynamics_world_, radius, height, position)) {
        return nullptr;
    }
    character_controllers_.push_back(controller);
    return controller;
}

void PhysicsWorld::DestroyCharacterController(SharedPtr<CharacterController> controller) {
    auto it = std::find(character_controllers_.begin(), character_controllers_.end(), controller);
    if (it != character_controllers_.end()) {
        character_controllers_.erase(it);
    }
}

RaycastResult PhysicsWorld::Raycast(const Math::Vector3& from, const Math::Vector3& to,
                                     CollisionLayer mask) const {
    RaycastResult result;

    btCollisionWorld::ClosestRayResultCallback callback(ToBullet(from), ToBullet(to));
    callback.m_collisionFilterMask = static_cast<int16>(mask);

    dynamics_world_->rayTest(ToBullet(from), ToBullet(to), callback);

    if (callback.hasHit()) {
        result.hit = true;
        result.point = FromBullet(callback.m_hitPointWorld);
        result.normal = FromBullet(callback.m_hitNormalWorld);
        result.distance = callback.m_closestHitFraction * Math::Vector3::Distance(from, to);

        const btCollisionObject* obj = callback.m_collisionObject;
        if (obj && obj->getUserPointer()) {
            auto* rb = static_cast<RigidBody*>(obj->getUserPointer());
            result.body_user_data = rb->GetUserData();
        }
    }

    return result;
}

Array<RaycastResult> PhysicsWorld::RaycastAll(const Math::Vector3& from, const Math::Vector3& to,
                                               CollisionLayer mask) const {
    Array<RaycastResult> results;

    btCollisionWorld::AllHitsRayResultCallback callback(ToBullet(from), ToBullet(to));
    callback.m_collisionFilterMask = static_cast<int16>(mask);

    dynamics_world_->rayTest(ToBullet(from), ToBullet(to), callback);

    for (int i = 0; i < callback.m_hitPointWorld.size(); i++) {
        RaycastResult result;
        result.hit = true;
        result.point = FromBullet(callback.m_hitPointWorld[i]);
        result.normal = FromBullet(callback.m_hitNormalWorld[i]);
        result.distance = callback.m_hitFractions[i] * Math::Vector3::Distance(from, to);

        const btCollisionObject* obj = callback.m_collisionObjects[i];
        if (obj && obj->getUserPointer()) {
            auto* rb = static_cast<RigidBody*>(obj->getUserPointer());
            result.body_user_data = rb->GetUserData();
        }

        results.push_back(result);
    }

    return results;
}

RaycastResult PhysicsWorld::SphereCast(const Math::Vector3& from, const Math::Vector3& to,
                                        float32 radius, CollisionLayer mask) const {
    RaycastResult result;

    btSphereShape sphere(radius);
    btTransform from_trans, to_trans;
    from_trans.setIdentity();
    from_trans.setOrigin(ToBullet(from));
    to_trans.setIdentity();
    to_trans.setOrigin(ToBullet(to));

    btCollisionWorld::ClosestConvexResultCallback callback(ToBullet(from), ToBullet(to));
    callback.m_collisionFilterMask = static_cast<int16>(mask);

    dynamics_world_->convexSweepTest(&sphere, from_trans, to_trans, callback);

    if (callback.hasHit()) {
        result.hit = true;
        result.point = FromBullet(callback.m_hitPointWorld);
        result.normal = FromBullet(callback.m_hitNormalWorld);
        result.distance = callback.m_closestHitFraction * Math::Vector3::Distance(from, to);
    }

    return result;
}

Array<void*> PhysicsWorld::OverlapSphere(const Math::Vector3& center, float32 radius,
                                          CollisionLayer mask) const {
    Array<void*> results;

    btSphereShape sphere(radius);
    btTransform trans;
    trans.setIdentity();
    trans.setOrigin(ToBullet(center));

    struct ContactCallback : public btCollisionWorld::ContactResultCallback {
        Array<void*>& results;
        ContactCallback(Array<void*>& r) : results(r) {}

        btScalar addSingleResult(btManifoldPoint&, const btCollisionObjectWrapper* obj0,
                                 int, int, const btCollisionObjectWrapper*, int, int) override {
            if (obj0->getCollisionObject()->getUserPointer()) {
                auto* rb = static_cast<RigidBody*>(obj0->getCollisionObject()->getUserPointer());
                results.push_back(rb->GetUserData());
            }
            return 0;
        }
    } callback(results);

    callback.m_collisionFilterMask = static_cast<int16>(mask);

    btCollisionObject probe;
    probe.setCollisionShape(&sphere);
    probe.setWorldTransform(trans);

    dynamics_world_->contactTest(&probe, callback);

    return results;
}

Array<void*> PhysicsWorld::OverlapBox(const Math::Vector3& center, const Math::Vector3& half_extents,
                                       const Math::Quaternion& rotation, CollisionLayer mask) const {
    Array<void*> results;

    btBoxShape box(ToBullet(half_extents));
    btTransform trans;
    trans.setIdentity();
    trans.setOrigin(ToBullet(center));
    trans.setRotation(ToBullet(rotation));

    struct ContactCallback : public btCollisionWorld::ContactResultCallback {
        Array<void*>& results;
        ContactCallback(Array<void*>& r) : results(r) {}

        btScalar addSingleResult(btManifoldPoint&, const btCollisionObjectWrapper* obj0,
                                 int, int, const btCollisionObjectWrapper*, int, int) override {
            if (obj0->getCollisionObject()->getUserPointer()) {
                auto* rb = static_cast<RigidBody*>(obj0->getCollisionObject()->getUserPointer());
                results.push_back(rb->GetUserData());
            }
            return 0;
        }
    } callback(results);

    callback.m_collisionFilterMask = static_cast<int16>(mask);

    btCollisionObject probe;
    probe.setCollisionShape(&box);
    probe.setWorldTransform(trans);

    dynamics_world_->contactTest(&probe, callback);

    return results;
}

void PhysicsWorld::SetGravity(const Math::Vector3& gravity) {
    if (dynamics_world_) {
        dynamics_world_->setGravity(ToBullet(gravity));
    }
}

Math::Vector3 PhysicsWorld::GetGravity() const {
    if (dynamics_world_) {
        return FromBullet(dynamics_world_->getGravity());
    }
    return Math::Vector3{0, -9.81f, 0};
}

void PhysicsWorld::ProcessCollisions() {
    if (!collision_callback_) return;

    int num_manifolds = dynamics_world_->getDispatcher()->getNumManifolds();
    for (int i = 0; i < num_manifolds; i++) {
        btPersistentManifold* manifold = dynamics_world_->getDispatcher()->getManifoldByIndexInternal(i);
        const btCollisionObject* obj_a = manifold->getBody0();
        const btCollisionObject* obj_b = manifold->getBody1();

        void* user_a = obj_a->getUserPointer() ? static_cast<RigidBody*>(obj_a->getUserPointer())->GetUserData() : nullptr;
        void* user_b = obj_b->getUserPointer() ? static_cast<RigidBody*>(obj_b->getUserPointer())->GetUserData() : nullptr;

        int num_contacts = manifold->getNumContacts();
        for (int j = 0; j < num_contacts; j++) {
            btManifoldPoint& pt = manifold->getContactPoint(j);

            ContactPoint contact;
            contact.position_world_on_a = FromBullet(pt.getPositionWorldOnA());
            contact.position_world_on_b = FromBullet(pt.getPositionWorldOnB());
            contact.normal_on_b = FromBullet(pt.m_normalWorldOnB);
            contact.distance = pt.getDistance();
            contact.impulse = pt.getAppliedImpulse();

            collision_callback_->OnCollisionStay(user_a, user_b, contact);
        }
    }
}

} // namespace DRX::Physics

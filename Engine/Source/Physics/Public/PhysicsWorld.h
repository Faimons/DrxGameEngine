/**
 * DRX Engine - Physics System
 * Bullet Physics integration for rigid body dynamics
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Transform.h"

// Forward declare Bullet types to avoid header pollution
class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class btBroadphaseInterface;
class btSequentialImpulseConstraintSolver;
class btDiscreteDynamicsWorld;
class btCollisionShape;
class btRigidBody;
class btMotionState;

namespace DRX::Physics {

/**
 * Collision shape types
 */
enum class CollisionShapeType : uint32 {
    Box,
    Sphere,
    Capsule,
    Cylinder,
    Cone,
    ConvexHull,
    TriangleMesh,
    Compound
};

/**
 * Rigid body type
 */
enum class RigidBodyType : uint32 {
    Static,      // Mass = 0, doesn't move
    Dynamic,     // Fully simulated
    Kinematic    // Moved by user, affects dynamics
};

/**
 * Collision layer flags
 */
enum class CollisionLayer : uint16 {
    Default = 1 << 0,
    Static = 1 << 1,
    Dynamic = 1 << 2,
    Character = 1 << 3,
    Projectile = 1 << 4,
    Trigger = 1 << 5,
    All = 0xFFFF
};

/**
 * Collision shape descriptor
 */
struct CollisionShapeDesc {
    CollisionShapeType type = CollisionShapeType::Box;
    Math::Vector3 half_extents = {0.5f, 0.5f, 0.5f};  // For box
    float32 radius = 0.5f;                             // For sphere, capsule
    float32 height = 1.0f;                             // For capsule, cylinder, cone
    Array<Math::Vector3> vertices;                     // For convex hull
    Array<uint32> indices;                             // For triangle mesh
};

/**
 * Rigid body descriptor
 */
struct RigidBodyDesc {
    RigidBodyType type = RigidBodyType::Dynamic;
    CollisionShapeDesc shape;
    Math::Transform initial_transform;
    float32 mass = 1.0f;
    float32 friction = 0.5f;
    float32 restitution = 0.0f;
    float32 linear_damping = 0.0f;
    float32 angular_damping = 0.05f;
    CollisionLayer layer = CollisionLayer::Default;
    CollisionLayer mask = CollisionLayer::All;
    bool is_trigger = false;
    void* user_data = nullptr;
};

/**
 * Ray cast result
 */
struct RaycastResult {
    bool hit = false;
    Math::Vector3 point;
    Math::Vector3 normal;
    float32 distance = 0.0f;
    void* body_user_data = nullptr;
};

/**
 * Collision contact point
 */
struct ContactPoint {
    Math::Vector3 position_world_on_a;
    Math::Vector3 position_world_on_b;
    Math::Vector3 normal_on_b;
    float32 distance;
    float32 impulse;
};

/**
 * Collision callback interface
 */
class ICollisionCallback {
public:
    virtual ~ICollisionCallback() = default;
    virtual void OnCollisionBegin(void* body_a, void* body_b, const ContactPoint& contact) = 0;
    virtual void OnCollisionStay(void* body_a, void* body_b, const ContactPoint& contact) = 0;
    virtual void OnCollisionEnd(void* body_a, void* body_b) = 0;
    virtual void OnTriggerEnter(void* trigger, void* other) = 0;
    virtual void OnTriggerExit(void* trigger, void* other) = 0;
};

/**
 * Rigid Body wrapper
 */
class RigidBody {
public:
    RigidBody() = default;
    ~RigidBody();

    bool Create(btDiscreteDynamicsWorld* world, const RigidBodyDesc& desc);
    void Destroy();

    // Transform
    Math::Transform GetTransform() const;
    void SetTransform(const Math::Transform& transform);
    Math::Vector3 GetPosition() const;
    void SetPosition(const Math::Vector3& position);
    Math::Quaternion GetRotation() const;
    void SetRotation(const Math::Quaternion& rotation);

    // Velocity
    Math::Vector3 GetLinearVelocity() const;
    void SetLinearVelocity(const Math::Vector3& velocity);
    Math::Vector3 GetAngularVelocity() const;
    void SetAngularVelocity(const Math::Vector3& velocity);

    // Forces
    void ApplyForce(const Math::Vector3& force);
    void ApplyForceAtPoint(const Math::Vector3& force, const Math::Vector3& point);
    void ApplyImpulse(const Math::Vector3& impulse);
    void ApplyImpulseAtPoint(const Math::Vector3& impulse, const Math::Vector3& point);
    void ApplyTorque(const Math::Vector3& torque);
    void ApplyTorqueImpulse(const Math::Vector3& torque);

    // Properties
    float32 GetMass() const;
    void SetMass(float32 mass);
    void SetFriction(float32 friction);
    void SetRestitution(float32 restitution);
    void SetDamping(float32 linear, float32 angular);

    // State
    void SetKinematic(bool kinematic);
    bool IsKinematic() const;
    void SetActive(bool active);
    bool IsActive() const;
    void SetGravityEnabled(bool enabled);

    // User data
    void SetUserData(void* data) { user_data_ = data; }
    void* GetUserData() const { return user_data_; }

    btRigidBody* GetBulletBody() const { return body_; }

private:
    btDiscreteDynamicsWorld* world_ = nullptr;
    btRigidBody* body_ = nullptr;
    btCollisionShape* shape_ = nullptr;
    btMotionState* motion_state_ = nullptr;
    void* user_data_ = nullptr;
    RigidBodyType type_ = RigidBodyType::Dynamic;
};

/**
 * Character Controller
 * Simplified kinematic character physics
 */
class CharacterController {
public:
    CharacterController() = default;
    ~CharacterController();

    bool Create(btDiscreteDynamicsWorld* world, float32 radius, float32 height,
                const Math::Vector3& position);
    void Destroy();

    void Move(const Math::Vector3& displacement, float32 delta_time);
    void Jump(float32 force);

    Math::Vector3 GetPosition() const;
    void SetPosition(const Math::Vector3& position);

    bool IsGrounded() const { return is_grounded_; }
    Math::Vector3 GetVelocity() const { return velocity_; }

    void SetStepHeight(float32 height) { step_height_ = height; }
    void SetMaxSlope(float32 angle_degrees) { max_slope_ = angle_degrees; }
    void SetGravity(float32 gravity) { gravity_ = gravity; }

private:
    void UpdateGrounded();

    btDiscreteDynamicsWorld* world_ = nullptr;
    btRigidBody* body_ = nullptr;
    btCollisionShape* shape_ = nullptr;

    Math::Vector3 velocity_;
    float32 step_height_ = 0.35f;
    float32 max_slope_ = 45.0f;
    float32 gravity_ = 9.81f;
    bool is_grounded_ = false;
};

/**
 * Physics World
 * Main physics simulation container
 */
class PhysicsWorld {
public:
    PhysicsWorld() = default;
    ~PhysicsWorld();

    bool Initialize(const Math::Vector3& gravity = {0, -9.81f, 0});
    void Shutdown();

    // Simulation
    void Step(float32 delta_time, int32 max_sub_steps = 10);

    // Rigid bodies
    SharedPtr<RigidBody> CreateRigidBody(const RigidBodyDesc& desc);
    void DestroyRigidBody(SharedPtr<RigidBody> body);

    // Character controllers
    SharedPtr<CharacterController> CreateCharacterController(float32 radius, float32 height,
                                                               const Math::Vector3& position);
    void DestroyCharacterController(SharedPtr<CharacterController> controller);

    // Raycasting
    RaycastResult Raycast(const Math::Vector3& from, const Math::Vector3& to,
                          CollisionLayer mask = CollisionLayer::All) const;
    Array<RaycastResult> RaycastAll(const Math::Vector3& from, const Math::Vector3& to,
                                    CollisionLayer mask = CollisionLayer::All) const;

    // Sphere cast
    RaycastResult SphereCast(const Math::Vector3& from, const Math::Vector3& to,
                             float32 radius, CollisionLayer mask = CollisionLayer::All) const;

    // Overlap tests
    Array<void*> OverlapSphere(const Math::Vector3& center, float32 radius,
                               CollisionLayer mask = CollisionLayer::All) const;
    Array<void*> OverlapBox(const Math::Vector3& center, const Math::Vector3& half_extents,
                            const Math::Quaternion& rotation,
                            CollisionLayer mask = CollisionLayer::All) const;

    // Settings
    void SetGravity(const Math::Vector3& gravity);
    Math::Vector3 GetGravity() const;

    // Callbacks
    void SetCollisionCallback(ICollisionCallback* callback) { collision_callback_ = callback; }

    // Debug
    void EnableDebugDraw(bool enable) { debug_draw_enabled_ = enable; }
    bool IsDebugDrawEnabled() const { return debug_draw_enabled_; }

    btDiscreteDynamicsWorld* GetBulletWorld() const { return dynamics_world_; }

private:
    void ProcessCollisions();

    btDefaultCollisionConfiguration* collision_config_ = nullptr;
    btCollisionDispatcher* dispatcher_ = nullptr;
    btBroadphaseInterface* broadphase_ = nullptr;
    btSequentialImpulseConstraintSolver* solver_ = nullptr;
    btDiscreteDynamicsWorld* dynamics_world_ = nullptr;

    Array<SharedPtr<RigidBody>> rigid_bodies_;
    Array<SharedPtr<CharacterController>> character_controllers_;

    ICollisionCallback* collision_callback_ = nullptr;
    bool debug_draw_enabled_ = false;
};

} // namespace DRX::Physics

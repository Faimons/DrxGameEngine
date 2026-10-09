/**
 * DRX Engine - Physics System
 * Bullet-based rigid bodies, scene queries and a kinematic character controller.
 *
 * Conventions: Y is up, units are metres. Capsule `height` is the total height including
 * both caps. Step the world from a fixed-timestep update for deterministic results.
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Transform.h"

#include <set>
#include <utility>

// Forward declare Bullet types to avoid header pollution
class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class btBroadphaseInterface;
class btSequentialImpulseConstraintSolver;
class btDiscreteDynamicsWorld;
class btCollisionShape;
class btCollisionObject;
class btConvexShape;
class btRigidBody;
class btMotionState;
class btTriangleMesh;

namespace DRX::Physics {

class PhysicsWorld;
class RigidBody;
class CharacterController;

enum class CollisionShapeType : uint32 {
    Box,
    Sphere,
    Capsule,
    Cylinder,
    Cone,
    ConvexHull,
    TriangleMesh    // static bodies only
};

enum class RigidBodyType : uint32 {
    Static,      // never moves
    Dynamic,     // simulated
    Kinematic    // moved by code; pushes dynamic bodies and characters
};

// Bit flags; combine with | (e.g. CollisionLayer::Static | CollisionLayer::Dynamic)
enum class CollisionLayer : uint16 {
    None = 0,
    Default = 1 << 0,
    Static = 1 << 1,
    Dynamic = 1 << 2,
    Character = 1 << 3,
    Projectile = 1 << 4,
    Trigger = 1 << 5,
    Kinematic = 1 << 6,
    All = 0xFFFF
};

constexpr CollisionLayer operator|(CollisionLayer a, CollisionLayer b) {
    return static_cast<CollisionLayer>(static_cast<uint16>(a) | static_cast<uint16>(b));
}
constexpr CollisionLayer operator&(CollisionLayer a, CollisionLayer b) {
    return static_cast<CollisionLayer>(static_cast<uint16>(a) & static_cast<uint16>(b));
}
constexpr CollisionLayer operator~(CollisionLayer a) {
    return static_cast<CollisionLayer>(static_cast<uint16>(~static_cast<uint16>(a)));
}
constexpr bool HasAny(CollisionLayer a, CollisionLayer b) {
    return (static_cast<uint16>(a) & static_cast<uint16>(b)) != 0;
}

struct CollisionShapeDesc {
    CollisionShapeType type = CollisionShapeType::Box;
    Math::Vector3 half_extents = {0.5f, 0.5f, 0.5f};  // box
    float32 radius = 0.5f;                             // sphere, capsule, cylinder, cone
    float32 height = 1.0f;                             // capsule (total), cylinder, cone
    Array<Math::Vector3> vertices;                     // convex hull, triangle mesh
    Array<uint32> indices;                             // triangle mesh
};

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
    bool is_trigger = false;   // reports overlaps, no collision response
    void* user_data = nullptr;
};

struct RaycastResult {
    bool hit = false;
    Math::Vector3 point;
    Math::Vector3 normal;
    float32 distance = 0.0f;
    float32 fraction = 1.0f;
    RigidBody* body = nullptr;
    CharacterController* character = nullptr;
    void* body_user_data = nullptr;
};

struct QueryFilter {
    CollisionLayer mask = CollisionLayer::All;
    bool include_triggers = false;
    const RigidBody* ignore_body = nullptr;
    const CharacterController* ignore_character = nullptr;
};

struct ContactPoint {
    Math::Vector3 position_world_on_a;
    Math::Vector3 position_world_on_b;
    Math::Vector3 normal_on_b;
    float32 distance = 0.0f;
    float32 impulse = 0.0f;
};

/**
 * Collision events, reported after each step. Arguments are the bodies' user data.
 */
class ICollisionCallback {
public:
    virtual ~ICollisionCallback() = default;
    virtual void OnCollisionBegin(void* /*a*/, void* /*b*/, const ContactPoint& /*contact*/) {}
    virtual void OnCollisionStay(void* /*a*/, void* /*b*/, const ContactPoint& /*contact*/) {}
    virtual void OnCollisionEnd(void* /*a*/, void* /*b*/) {}
    virtual void OnTriggerEnter(void* /*trigger*/, void* /*other*/) {}
    virtual void OnTriggerExit(void* /*trigger*/, void* /*other*/) {}
};

class RigidBody {
public:
    RigidBody() = default;
    ~RigidBody();

    RigidBody(const RigidBody&) = delete;
    RigidBody& operator=(const RigidBody&) = delete;

    bool Create(btDiscreteDynamicsWorld* world, const RigidBodyDesc& desc);
    void Destroy();

    Math::Transform GetTransform() const;
    // Kinematic bodies: the new transform is reached during the next step (velocity is derived from it)
    void SetTransform(const Math::Transform& transform);
    Math::Vector3 GetPosition() const;
    void SetPosition(const Math::Vector3& position);
    Math::Quaternion GetRotation() const;
    void SetRotation(const Math::Quaternion& rotation);

    Math::Vector3 GetLinearVelocity() const;
    void SetLinearVelocity(const Math::Vector3& velocity);
    Math::Vector3 GetAngularVelocity() const;
    void SetAngularVelocity(const Math::Vector3& velocity);

    void ApplyForce(const Math::Vector3& force);
    void ApplyForceAtPoint(const Math::Vector3& force, const Math::Vector3& point);
    void ApplyImpulse(const Math::Vector3& impulse);
    void ApplyImpulseAtPoint(const Math::Vector3& impulse, const Math::Vector3& point);
    void ApplyTorque(const Math::Vector3& torque);
    void ApplyTorqueImpulse(const Math::Vector3& torque);

    float32 GetMass() const;
    void SetMass(float32 mass);
    void SetFriction(float32 friction);
    void SetRestitution(float32 restitution);
    void SetDamping(float32 linear, float32 angular);

    bool IsKinematic() const { return type_ == RigidBodyType::Kinematic; }
    bool IsStatic() const { return type_ == RigidBodyType::Static; }
    bool IsTrigger() const { return is_trigger_; }
    void SetActive(bool active);
    bool IsActive() const;
    void SetGravityEnabled(bool enabled);

    CollisionLayer GetLayer() const { return layer_; }
    void SetUserData(void* data) { user_data_ = data; }
    void* GetUserData() const { return user_data_; }
    btRigidBody* GetBulletBody() const { return body_; }

private:
    btDiscreteDynamicsWorld* world_ = nullptr;
    btRigidBody* body_ = nullptr;
    btCollisionShape* shape_ = nullptr;
    btMotionState* motion_state_ = nullptr;
    btTriangleMesh* triangle_mesh_ = nullptr;
    void* user_data_ = nullptr;
    RigidBodyType type_ = RigidBodyType::Dynamic;
    CollisionLayer layer_ = CollisionLayer::Default;
    bool is_trigger_ = false;
};

/**
 * Kinematic capsule character (collide-and-slide).
 * Deterministic for identical inputs and world state, so it can be re-simulated for
 * client-side prediction. Position is the bottom of the capsule (the feet).
 */
struct CharacterSettings {
    float32 radius = 0.35f;
    float32 height = 1.8f;
    float32 step_height = 0.35f;
    float32 max_slope_degrees = 46.0f;
    float32 gravity = 20.0f;              // m/s^2, downwards
    float32 skin_width = 0.01f;
    float32 ground_snap_distance = 0.3f;  // keeps contact when walking down slopes and stairs
    float32 crush_tolerance = 0.04f;      // remaining penetration that counts as crushed
    CollisionLayer layer = CollisionLayer::Character;
    CollisionLayer mask = CollisionLayer::All;   // what blocks the character (triggers never block)
    void* user_data = nullptr;
};

struct CharacterState {
    Math::Vector3 position;     // feet
    Math::Vector3 velocity;
    bool grounded = false;
    Math::Vector3 ground_normal = Math::Vector3::Up;
};

class CharacterController {
public:
    CharacterController() = default;
    ~CharacterController();

    CharacterController(const CharacterController&) = delete;
    CharacterController& operator=(const CharacterController&) = delete;

    bool Create(btDiscreteDynamicsWorld* world, const CharacterSettings& settings, const Math::Vector3& feet_position);
    void Destroy();

    /**
     * One simulation step. `wish_velocity` sets the horizontal velocity (y is ignored);
     * `jump_speed` > 0 jumps when grounded. Gravity, stepping, sliding and ground snapping are applied.
     */
    void Move(const Math::Vector3& wish_velocity, float32 jump_speed, float32 dt);

    // Moves by a displacement with collision but without gravity; returns the distance travelled
    float32 MoveAndSlide(const Math::Vector3& displacement);

    /**
     * Pushes the capsule out of overlapping geometry (e.g. a kinematic wall moved into it).
     * Returns false when it cannot be freed; IsCrushed() is then true until the next successful resolve.
     */
    bool ResolvePenetration();
    bool IsCrushed() const { return crushed_; }

    const CharacterState& GetState() const { return state_; }
    void SetState(const CharacterState& state);

    Math::Vector3 GetPosition() const { return state_.position; }
    void SetPosition(const Math::Vector3& feet_position);
    // Disabled characters are removed from collision and queries (e.g. dead players)
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled_; }
    Math::Vector3 GetVelocity() const { return state_.velocity; }
    void SetVelocity(const Math::Vector3& velocity) { state_.velocity = velocity; }
    bool IsGrounded() const { return state_.grounded; }
    Math::Vector3 GetCenter() const;

    const CharacterSettings& GetSettings() const { return settings_; }
    void* GetUserData() const { return settings_.user_data; }
    btCollisionObject* GetCollisionObject() const { return object_; }

private:
    struct SweepHit {
        bool hit = false;
        float32 fraction = 1.0f;
        Math::Vector3 normal;
        Math::Vector3 point;
    };

    SweepHit Sweep(const Math::Vector3& from_feet, const Math::Vector3& to_feet) const;
    Math::Vector3 SlideMove(const Math::Vector3& start, const Math::Vector3& displacement, Math::Vector3* velocity) const;
    // Snaps feet down onto walkable ground within `distance`; contact_height receives the touched point's y
    bool ProbeGround(Math::Vector3& feet, float32 distance, Math::Vector3& normal, float32* contact_height = nullptr) const;
    bool IsWalkable(const Math::Vector3& normal) const;
    void SyncObject();

    btDiscreteDynamicsWorld* world_ = nullptr;
    btCollisionObject* object_ = nullptr;
    btConvexShape* shape_ = nullptr;
    CharacterSettings settings_;
    CharacterState state_;
    float32 min_ground_normal_y_ = 0.7f;
    bool crushed_ = false;
    bool enabled_ = true;
};

class PhysicsWorld {
public:
    PhysicsWorld() = default;
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    bool Initialize(const Math::Vector3& gravity = {0, -9.81f, 0});
    void Shutdown();
    bool IsInitialized() const { return dynamics_world_ != nullptr; }

    // Advances exactly `delta_time` (call from a fixed-step update)
    void Step(float32 delta_time);

    SharedPtr<RigidBody> CreateRigidBody(const RigidBodyDesc& desc);
    void DestroyRigidBody(const SharedPtr<RigidBody>& body);

    SharedPtr<CharacterController> CreateCharacterController(const CharacterSettings& settings,
                                                             const Math::Vector3& feet_position);
    void DestroyCharacterController(const SharedPtr<CharacterController>& controller);

    // Queries (triggers are skipped unless the filter includes them)
    RaycastResult Raycast(const Math::Vector3& from, const Math::Vector3& to, const QueryFilter& filter = {}) const;
    Array<RaycastResult> RaycastAll(const Math::Vector3& from, const Math::Vector3& to,
                                    const QueryFilter& filter = {}) const;   // sorted by distance
    RaycastResult SphereCast(const Math::Vector3& from, const Math::Vector3& to, float32 radius,
                             const QueryFilter& filter = {}) const;
    Array<void*> OverlapSphere(const Math::Vector3& center, float32 radius, const QueryFilter& filter = {}) const;
    Array<void*> OverlapBox(const Math::Vector3& center, const Math::Vector3& half_extents,
                            const Math::Quaternion& rotation, const QueryFilter& filter = {}) const;

    void SetGravity(const Math::Vector3& gravity);
    Math::Vector3 GetGravity() const;

    void SetCollisionCallback(ICollisionCallback* callback) { collision_callback_ = callback; }

    uint32 GetRigidBodyCount() const { return static_cast<uint32>(rigid_bodies_.size()); }
    btDiscreteDynamicsWorld* GetBulletWorld() const { return dynamics_world_; }

private:
    void ProcessCollisions();
    Array<void*> Overlap(btCollisionShape& shape, const Math::Vector3& center, const Math::Quaternion& rotation,
                         const QueryFilter& filter) const;

    btDefaultCollisionConfiguration* collision_config_ = nullptr;
    btCollisionDispatcher* dispatcher_ = nullptr;
    btBroadphaseInterface* broadphase_ = nullptr;
    btSequentialImpulseConstraintSolver* solver_ = nullptr;
    btDiscreteDynamicsWorld* dynamics_world_ = nullptr;

    Array<SharedPtr<RigidBody>> rigid_bodies_;
    Array<SharedPtr<CharacterController>> character_controllers_;

    ICollisionCallback* collision_callback_ = nullptr;
    std::set<std::pair<const btCollisionObject*, const btCollisionObject*>> active_pairs_;
};

} // namespace DRX::Physics

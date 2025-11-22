/**
 * DRX Engine - Particle System
 * GPU-accelerated particle effects
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector2.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Vector4.h"
#include "Renderer/Public/Texture.h"

#include <random>

namespace DRX::Particles {

/**
 * Particle data (SOA layout for cache efficiency)
 */
struct ParticleData {
    Array<Math::Vector3> positions;
    Array<Math::Vector3> velocities;
    Array<Math::Vector4> colors;
    Array<float32> sizes;
    Array<float32> rotations;
    Array<float32> angular_velocities;
    Array<float32> lifetimes;
    Array<float32> ages;
    Array<uint32> texture_indices;

    uint32 count = 0;
    uint32 capacity = 0;

    void Resize(uint32 new_capacity);
    void Kill(uint32 index);
};

/**
 * Random range for particle properties
 */
template<typename T>
struct RandomRange {
    T min;
    T max;

    T Sample(std::mt19937& rng) const;
};

using FloatRange = RandomRange<float32>;
using Vector3Range = RandomRange<Math::Vector3>;
using ColorRange = RandomRange<Math::Vector4>;

/**
 * Particle emission shape
 */
enum class EmissionShape : uint32 {
    Point,
    Sphere,
    Hemisphere,
    Cone,
    Box,
    Circle,
    Edge,
    Mesh  // Emit from mesh surface
};

/**
 * Emission shape parameters
 */
struct EmissionShapeParams {
    EmissionShape shape = EmissionShape::Point;

    // Sphere/Hemisphere
    float32 radius = 1.0f;
    float32 radius_thickness = 0.0f;  // 0 = surface, 1 = volume

    // Cone
    float32 angle = 25.0f;
    float32 length = 5.0f;

    // Box
    Math::Vector3 box_size = {1.0f, 1.0f, 1.0f};

    // Circle
    float32 arc = 360.0f;  // degrees

    Math::Vector3 SamplePosition(std::mt19937& rng) const;
    Math::Vector3 SampleDirection(std::mt19937& rng, const Math::Vector3& position) const;
};

/**
 * Particle emitter module base
 */
class IParticleModule {
public:
    virtual ~IParticleModule() = default;

    virtual void Initialize(ParticleData& particles, uint32 start, uint32 count, std::mt19937& rng) {}
    virtual void Update(ParticleData& particles, float32 delta_time) {}

    bool enabled = true;
};

/**
 * Initial velocity module
 */
class VelocityModule : public IParticleModule {
public:
    Vector3Range initial_velocity = {{0, 1, 0}, {0, 5, 0}};
    float32 inherit_velocity = 0.0f;
    bool local_space = false;

    void Initialize(ParticleData& particles, uint32 start, uint32 count, std::mt19937& rng) override;
};

/**
 * Gravity/force module
 */
class ForceModule : public IParticleModule {
public:
    Math::Vector3 gravity = {0, -9.81f, 0};
    Math::Vector3 constant_force = {0, 0, 0};
    float32 drag = 0.0f;

    void Update(ParticleData& particles, float32 delta_time) override;
};

/**
 * Color over lifetime module
 */
class ColorOverLifetimeModule : public IParticleModule {
public:
    // Gradient points
    struct GradientPoint {
        float32 time;  // 0-1
        Math::Vector4 color;
    };
    Array<GradientPoint> gradient;

    void Update(ParticleData& particles, float32 delta_time) override;
    Math::Vector4 SampleGradient(float32 t) const;
};

/**
 * Size over lifetime module
 */
class SizeOverLifetimeModule : public IParticleModule {
public:
    // Curve points
    struct CurvePoint {
        float32 time;
        float32 value;
    };
    Array<CurvePoint> size_curve;
    float32 size_multiplier = 1.0f;

    void Update(ParticleData& particles, float32 delta_time) override;
    float32 SampleCurve(float32 t) const;
};

/**
 * Rotation over lifetime module
 */
class RotationOverLifetimeModule : public IParticleModule {
public:
    FloatRange angular_velocity = {0, 0};
    bool random_direction = true;

    void Initialize(ParticleData& particles, uint32 start, uint32 count, std::mt19937& rng) override;
    void Update(ParticleData& particles, float32 delta_time) override;
};

/**
 * Texture sheet animation module
 */
class TextureSheetModule : public IParticleModule {
public:
    uint32 tiles_x = 1;
    uint32 tiles_y = 1;
    float32 frame_rate = 30.0f;
    bool loop = true;
    bool random_start_frame = false;

    void Initialize(ParticleData& particles, uint32 start, uint32 count, std::mt19937& rng) override;
    void Update(ParticleData& particles, float32 delta_time) override;
};

/**
 * Collision module
 */
class CollisionModule : public IParticleModule {
public:
    enum class CollisionType { World, Planes };

    CollisionType type = CollisionType::World;
    float32 bounce = 0.5f;
    float32 lifetime_loss = 0.0f;
    float32 radius_scale = 1.0f;
    Array<Math::Vector4> planes;  // xyz: normal, w: distance

    void Update(ParticleData& particles, float32 delta_time) override;
};

/**
 * Noise module
 */
class NoiseModule : public IParticleModule {
public:
    float32 strength = 1.0f;
    float32 frequency = 1.0f;
    float32 scroll_speed = 0.0f;
    uint32 octaves = 1;

    void Update(ParticleData& particles, float32 delta_time) override;

private:
    float32 time_ = 0.0f;
};

/**
 * Sub-emitter trigger
 */
enum class SubEmitterTrigger : uint32 {
    Birth,
    Death,
    Collision,
    Manual
};

/**
 * Particle emitter
 */
class ParticleEmitter {
public:
    ParticleEmitter();
    ~ParticleEmitter();

    void Update(float32 delta_time);
    void Emit(uint32 count);
    void Clear();

    // Configuration
    void SetMaxParticles(uint32 max);
    void SetEmissionRate(float32 particles_per_second);
    void SetBurstCount(uint32 count, float32 interval = 0.0f);
    void SetDuration(float32 duration, bool looping = true);

    // Properties
    FloatRange lifetime = {1.0f, 2.0f};
    FloatRange start_size = {0.1f, 0.5f};
    ColorRange start_color = {{1, 1, 1, 1}, {1, 1, 1, 1}};
    FloatRange start_rotation = {0, 0};
    FloatRange start_speed = {1.0f, 5.0f};

    // Shape
    EmissionShapeParams emission_shape;

    // Rendering
    SharedPtr<Renderer::Texture> texture;
    enum class RenderMode { Billboard, StretchedBillboard, HorizontalBillboard, VerticalBillboard, Mesh };
    RenderMode render_mode = RenderMode::Billboard;
    enum class BlendMode { Additive, AlphaBlend, Multiply };
    BlendMode blend_mode = BlendMode::AlphaBlend;
    float32 stretch_factor = 0.0f;  // For stretched billboard

    // Modules
    template<typename T, typename... Args>
    T* AddModule(Args&&... args) {
        auto module = MakeUnique<T>(std::forward<Args>(args)...);
        T* ptr = module.get();
        modules_.push_back(std::move(module));
        return ptr;
    }

    template<typename T>
    T* GetModule() {
        for (auto& module : modules_) {
            if (auto* m = dynamic_cast<T*>(module.get())) {
                return m;
            }
        }
        return nullptr;
    }

    // State
    bool IsPlaying() const { return playing_; }
    bool IsAlive() const { return particles_.count > 0 || playing_; }
    uint32 GetParticleCount() const { return particles_.count; }
    const ParticleData& GetParticles() const { return particles_; }

    void Play();
    void Pause();
    void Stop(bool clear = false);

    // Transform
    Math::Vector3 position;
    Math::Quaternion rotation;
    bool simulate_in_world_space = true;

private:
    void SpawnParticles(uint32 count);
    void UpdateParticles(float32 delta_time);
    void RemoveDeadParticles();

    ParticleData particles_;
    Array<UniquePtr<IParticleModule>> modules_;

    float32 emission_rate_ = 10.0f;
    float32 emission_accumulator_ = 0.0f;
    float32 duration_ = 5.0f;
    float32 time_ = 0.0f;
    bool looping_ = true;
    bool playing_ = false;

    uint32 burst_count_ = 0;
    float32 burst_interval_ = 0.0f;
    float32 burst_timer_ = 0.0f;

    std::mt19937 rng_;
};

/**
 * Particle system - manages multiple emitters
 */
class ParticleSystem {
public:
    ParticleSystem();
    ~ParticleSystem();

    void Update(float32 delta_time);
    void Clear();

    ParticleEmitter* AddEmitter();
    void RemoveEmitter(ParticleEmitter* emitter);

    void Play();
    void Pause();
    void Stop(bool clear = false);

    bool IsPlaying() const { return playing_; }
    bool IsAlive() const;
    uint32 GetTotalParticleCount() const;

    const Array<UniquePtr<ParticleEmitter>>& GetEmitters() const { return emitters_; }

    // Transform
    Math::Vector3 position;
    Math::Quaternion rotation;
    Math::Vector3 scale = {1, 1, 1};

private:
    Array<UniquePtr<ParticleEmitter>> emitters_;
    bool playing_ = false;
};

/**
 * Particle System Manager
 */
class ParticleManager {
public:
    static ParticleManager& Instance();

    bool Initialize();
    void Shutdown();
    void Update(float32 delta_time);

    SharedPtr<ParticleSystem> CreateSystem();
    void DestroySystem(SharedPtr<ParticleSystem> system);

    // Load particle system from file
    SharedPtr<ParticleSystem> Load(const String& path);

    // Global settings
    void SetMaxParticles(uint32 max) { global_max_particles_ = max; }
    uint32 GetMaxParticles() const { return global_max_particles_; }
    uint32 GetTotalActiveParticles() const;

    // Time scale
    void SetTimeScale(float32 scale) { time_scale_ = scale; }
    float32 GetTimeScale() const { return time_scale_; }

private:
    ParticleManager() = default;

    Array<SharedPtr<ParticleSystem>> systems_;
    HashMap<String, SharedPtr<ParticleSystem>> loaded_systems_;
    uint32 global_max_particles_ = 100000;
    float32 time_scale_ = 1.0f;
};

} // namespace DRX::Particles

/**
 * DRX Engine - Particle System Implementation
 */

#include "Particles/Public/ParticleSystem.h"
#include <algorithm>
#include <random>
#include <cmath>

namespace DRX::Particles {

// ============================================================================
// ParticleEmitter
// ============================================================================

ParticleEmitter::ParticleEmitter(const EmitterConfig& config)
    : config_(config) {
    particles_.resize(config.max_particles);
    active_count_ = 0;
}

void ParticleEmitter::Update(float32 delta_time) {
    // Spawn new particles
    if (emitting_) {
        emission_accumulator_ += config_.emission_rate * delta_time;
        while (emission_accumulator_ >= 1.0f && active_count_ < config_.max_particles) {
            SpawnParticle();
            emission_accumulator_ -= 1.0f;
        }
    }

    // Update existing particles
    for (uint32 i = 0; i < active_count_;) {
        ParticleData& p = particles_[i];
        p.lifetime -= delta_time;

        if (p.lifetime <= 0) {
            // Remove dead particle by swapping with last
            particles_[i] = particles_[active_count_ - 1];
            active_count_--;
            continue;
        }

        // Apply modules
        for (auto& module : modules_) {
            module->Update(p, delta_time);
        }

        // Basic integration
        p.velocity = p.velocity + p.acceleration * delta_time;
        p.position = p.position + p.velocity * delta_time;
        p.rotation += p.angular_velocity * delta_time;

        ++i;
    }
}

void ParticleEmitter::Emit(uint32 count) {
    for (uint32 i = 0; i < count && active_count_ < config_.max_particles; ++i) {
        SpawnParticle();
    }
}

void ParticleEmitter::Clear() {
    active_count_ = 0;
}

void ParticleEmitter::AddModule(SharedPtr<ParticleModule> module) {
    modules_.push_back(module);
}

void ParticleEmitter::RemoveModule(SharedPtr<ParticleModule> module) {
    modules_.erase(
        std::remove(modules_.begin(), modules_.end(), module),
        modules_.end()
    );
}

void ParticleEmitter::SpawnParticle() {
    if (active_count_ >= config_.max_particles) return;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float32> dis(0.0f, 1.0f);

    ParticleData& p = particles_[active_count_];

    // Position based on shape
    switch (config_.shape) {
        case EmitterShape::Point:
            p.position = position_;
            break;

        case EmitterShape::Sphere: {
            float32 theta = dis(gen) * 2.0f * 3.14159265359f;
            float32 phi = std::acos(2.0f * dis(gen) - 1.0f);
            float32 r = std::cbrt(dis(gen)) * config_.shape_radius;
            p.position = position_ + Math::Vector3(
                r * sin(phi) * cos(theta),
                r * sin(phi) * sin(theta),
                r * cos(phi)
            );
            break;
        }

        case EmitterShape::Box:
            p.position = position_ + Math::Vector3(
                (dis(gen) - 0.5f) * config_.shape_size.x,
                (dis(gen) - 0.5f) * config_.shape_size.y,
                (dis(gen) - 0.5f) * config_.shape_size.z
            );
            break;

        case EmitterShape::Cone: {
            float32 angle = dis(gen) * config_.shape_angle * 3.14159265359f / 180.0f;
            float32 theta = dis(gen) * 2.0f * 3.14159265359f;
            float32 r = dis(gen) * config_.shape_radius;
            p.position = position_ + Math::Vector3(
                r * cos(theta),
                0,
                r * sin(theta)
            );
            break;
        }

        case EmitterShape::Circle: {
            float32 theta = dis(gen) * 2.0f * 3.14159265359f;
            float32 r = std::sqrt(dis(gen)) * config_.shape_radius;
            p.position = position_ + Math::Vector3(
                r * cos(theta),
                0,
                r * sin(theta)
            );
            break;
        }

        case EmitterShape::Edge:
            p.position = position_ + Math::Vector3(
                (dis(gen) - 0.5f) * config_.shape_size.x,
                0,
                0
            );
            break;
    }

    // Initial velocity
    Math::Vector3 dir;
    if (config_.shape == EmitterShape::Sphere) {
        dir = (p.position - position_).Normalized();
    } else if (config_.shape == EmitterShape::Cone) {
        float32 angle = dis(gen) * config_.shape_angle * 3.14159265359f / 180.0f;
        float32 theta = dis(gen) * 2.0f * 3.14159265359f;
        dir = Math::Vector3(
            sin(angle) * cos(theta),
            cos(angle),
            sin(angle) * sin(theta)
        );
    } else {
        dir = Math::Vector3::Up;
    }

    float32 speed = config_.initial_speed.x + dis(gen) * (config_.initial_speed.y - config_.initial_speed.x);
    p.velocity = dir * speed;

    // Lifetime
    p.lifetime = config_.lifetime.x + dis(gen) * (config_.lifetime.y - config_.lifetime.x);
    p.max_lifetime = p.lifetime;

    // Size
    p.size = config_.initial_size.x + dis(gen) * (config_.initial_size.y - config_.initial_size.x);

    // Color
    p.color = config_.initial_color;

    // Rotation
    p.rotation = dis(gen) * 360.0f;
    p.angular_velocity = (dis(gen) - 0.5f) * 180.0f;

    // Acceleration
    p.acceleration = Math::Vector3::Zero;

    active_count_++;
}

// ============================================================================
// Particle Modules
// ============================================================================

void VelocityOverLifetimeModule::Update(ParticleData& particle, float32 delta_time) {
    float32 t = 1.0f - (particle.lifetime / particle.max_lifetime);
    particle.velocity = particle.velocity + SampleCurve(t) * delta_time;
}

Math::Vector3 VelocityOverLifetimeModule::SampleCurve(float32 t) const {
    // Linear interpolation for simplicity
    return velocity_start + (velocity_end - velocity_start) * t;
}

void ForceModule::Update(ParticleData& particle, float32 delta_time) {
    particle.acceleration = particle.acceleration + force;
}

void GravityModule::Update(ParticleData& particle, float32 delta_time) {
    particle.velocity.y -= gravity * delta_time;
}

void DragModule::Update(ParticleData& particle, float32 delta_time) {
    float32 speed = particle.velocity.Length();
    if (speed > 0.001f) {
        float32 drag_force = drag * speed * speed;
        Math::Vector3 drag_accel = -particle.velocity.Normalized() * drag_force;
        particle.velocity = particle.velocity + drag_accel * delta_time;
    }
}

void VortexModule::Update(ParticleData& particle, float32 delta_time) {
    Math::Vector3 to_center = center - particle.position;
    to_center.y = 0;  // Only horizontal
    float32 dist = to_center.Length();

    if (dist > 0.1f) {
        Math::Vector3 tangent = Math::Vector3::Cross(axis, to_center).Normalized();
        float32 vortex_strength = strength / (dist + 1.0f);
        particle.velocity = particle.velocity + tangent * vortex_strength * delta_time;
    }
}

void ColorOverLifetimeModule::Update(ParticleData& particle, float32 delta_time) {
    float32 t = 1.0f - (particle.lifetime / particle.max_lifetime);
    particle.color = Math::Vector4::Lerp(color_start, color_end, t);
}

void SizeOverLifetimeModule::Update(ParticleData& particle, float32 delta_time) {
    float32 t = 1.0f - (particle.lifetime / particle.max_lifetime);
    particle.size = size_start + (size_end - size_start) * t;
}

void RotationOverLifetimeModule::Update(ParticleData& particle, float32 delta_time) {
    particle.angular_velocity = angular_velocity;
}

void CollisionModule::Update(ParticleData& particle, float32 delta_time) {
    // Ground collision
    if (particle.position.y < plane_y) {
        particle.position.y = plane_y;
        particle.velocity.y = -particle.velocity.y * bounce;
        particle.velocity = particle.velocity * (1.0f - friction);

        if (kill_on_collision) {
            particle.lifetime = 0;
        }
    }
}

void NoiseModule::Update(ParticleData& particle, float32 delta_time) {
    // Simple pseudo-noise using sin
    float32 t = particle.max_lifetime - particle.lifetime;
    float32 nx = sin(particle.position.x * frequency + t * scroll_speed) * strength;
    float32 ny = sin(particle.position.y * frequency + t * scroll_speed + 1.0f) * strength;
    float32 nz = sin(particle.position.z * frequency + t * scroll_speed + 2.0f) * strength;

    particle.velocity = particle.velocity + Math::Vector3(nx, ny, nz) * delta_time;
}

void AttractionModule::Update(ParticleData& particle, float32 delta_time) {
    Math::Vector3 to_target = target - particle.position;
    float32 dist = to_target.Length();

    if (dist > 0.1f) {
        float32 force = strength / (dist * dist + 1.0f);
        force = std::min(force, max_force);
        particle.velocity = particle.velocity + to_target.Normalized() * force * delta_time;
    }
}

// ============================================================================
// ParticleSystem
// ============================================================================

ParticleSystem::ParticleSystem(const String& name)
    : name_(name) {}

void ParticleSystem::Update(float32 delta_time) {
    if (!playing_) return;

    for (auto& emitter : emitters_) {
        emitter->Update(delta_time);
    }
}

void ParticleSystem::Render(VkCommandBuffer cmd) {
    if (!playing_) return;

    // Build vertex data from all emitters
    Array<ParticleVertex> vertices;
    Array<uint32> indices;

    for (const auto& emitter : emitters_) {
        uint32 base_vertex = static_cast<uint32>(vertices.size());

        for (uint32 i = 0; i < emitter->GetActiveCount(); ++i) {
            const ParticleData& p = emitter->GetParticles()[i];

            // Billboard quad (4 vertices per particle)
            ParticleVertex v;
            v.color = p.color;
            v.size = p.size;
            v.rotation = p.rotation;

            // Center vertex with offset encoding
            v.position = p.position;
            v.texcoord = Math::Vector2(0, 0);
            vertices.push_back(v);

            v.texcoord = Math::Vector2(1, 0);
            vertices.push_back(v);

            v.texcoord = Math::Vector2(1, 1);
            vertices.push_back(v);

            v.texcoord = Math::Vector2(0, 1);
            vertices.push_back(v);

            // Indices for quad
            uint32 idx = base_vertex + i * 4;
            indices.push_back(idx);
            indices.push_back(idx + 1);
            indices.push_back(idx + 2);
            indices.push_back(idx);
            indices.push_back(idx + 2);
            indices.push_back(idx + 3);
        }
    }

    // Upload and render
    // ...
}

void ParticleSystem::Play() {
    playing_ = true;
    for (auto& emitter : emitters_) {
        emitter->SetEmitting(true);
    }
}

void ParticleSystem::Pause() {
    playing_ = false;
}

void ParticleSystem::Stop() {
    playing_ = false;
    for (auto& emitter : emitters_) {
        emitter->SetEmitting(false);
        emitter->Clear();
    }
}

void ParticleSystem::AddEmitter(SharedPtr<ParticleEmitter> emitter) {
    emitters_.push_back(emitter);
}

void ParticleSystem::RemoveEmitter(SharedPtr<ParticleEmitter> emitter) {
    emitters_.erase(
        std::remove(emitters_.begin(), emitters_.end(), emitter),
        emitters_.end()
    );
}

uint32 ParticleSystem::GetTotalParticleCount() const {
    uint32 count = 0;
    for (const auto& emitter : emitters_) {
        count += emitter->GetActiveCount();
    }
    return count;
}

// ============================================================================
// ParticleSystemManager
// ============================================================================

ParticleSystemManager& ParticleSystemManager::Instance() {
    static ParticleSystemManager instance;
    return instance;
}

bool ParticleSystemManager::Initialize() {
    return true;
}

void ParticleSystemManager::Shutdown() {
    systems_.clear();
    active_systems_.clear();
}

void ParticleSystemManager::Update(float32 delta_time) {
    for (auto& system : active_systems_) {
        system->Update(delta_time);
    }
}

void ParticleSystemManager::Render(VkCommandBuffer cmd) {
    for (auto& system : active_systems_) {
        system->Render(cmd);
    }
}

SharedPtr<ParticleSystem> ParticleSystemManager::CreateSystem(const String& name) {
    auto system = std::make_shared<ParticleSystem>(name);
    systems_[name] = system;
    return system;
}

SharedPtr<ParticleSystem> ParticleSystemManager::GetSystem(const String& name) {
    auto it = systems_.find(name);
    return it != systems_.end() ? it->second : nullptr;
}

void ParticleSystemManager::DestroySystem(const String& name) {
    auto it = systems_.find(name);
    if (it != systems_.end()) {
        RemoveActiveSystem(it->second.get());
        systems_.erase(it);
    }
}

void ParticleSystemManager::AddActiveSystem(ParticleSystem* system) {
    if (system && std::find(active_systems_.begin(), active_systems_.end(), system) == active_systems_.end()) {
        active_systems_.push_back(system);
    }
}

void ParticleSystemManager::RemoveActiveSystem(ParticleSystem* system) {
    active_systems_.erase(
        std::remove(active_systems_.begin(), active_systems_.end(), system),
        active_systems_.end()
    );
}

} // namespace DRX::Particles

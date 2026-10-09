/**
 * DRX Engine - Particle System Implementation
 */

#include "Particles/Public/ParticleSystem.h"
#include "Core/Public/Log.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace DRX::Particles {

namespace {

constexpr float32 kPi = 3.14159265358979323846f;
constexpr float32 kDegToRad = kPi / 180.0f;

float32 Random01(std::mt19937& rng) {
    return std::uniform_real_distribution<float32>(0.0f, 1.0f)(rng);
}

Math::Vector3 RandomUnitVector(std::mt19937& rng) {
    const float32 z = Random01(rng) * 2.0f - 1.0f;
    const float32 theta = Random01(rng) * 2.0f * kPi;
    const float32 r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return Math::Vector3(r * std::cos(theta), r * std::sin(theta), z);
}

Math::Vector4 MultiplyComponents(const Math::Vector4& a, const Math::Vector4& b) {
    return Math::Vector4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);
}

// Smooth 3D value noise in [-1, 1]
float32 Hash3(int32 x, int32 y, int32 z) {
    uint32 h = static_cast<uint32>(x) * 374761393u + static_cast<uint32>(y) * 668265263u +
               static_cast<uint32>(z) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float32>(h & 0xFFFFFFu) / static_cast<float32>(0xFFFFFFu) * 2.0f - 1.0f;
}

float32 ValueNoise(float32 x, float32 y, float32 z) {
    const float32 fx = std::floor(x);
    const float32 fy = std::floor(y);
    const float32 fz = std::floor(z);
    const auto ix = static_cast<int32>(fx);
    const auto iy = static_cast<int32>(fy);
    const auto iz = static_cast<int32>(fz);

    auto smooth = [](float32 t) { return t * t * (3.0f - 2.0f * t); };
    const float32 tx = smooth(x - fx);
    const float32 ty = smooth(y - fy);
    const float32 tz = smooth(z - fz);

    auto lerp = [](float32 a, float32 b, float32 t) { return a + (b - a) * t; };
    const float32 c00 = lerp(Hash3(ix, iy, iz), Hash3(ix + 1, iy, iz), tx);
    const float32 c10 = lerp(Hash3(ix, iy + 1, iz), Hash3(ix + 1, iy + 1, iz), tx);
    const float32 c01 = lerp(Hash3(ix, iy, iz + 1), Hash3(ix + 1, iy, iz + 1), tx);
    const float32 c11 = lerp(Hash3(ix, iy + 1, iz + 1), Hash3(ix + 1, iy + 1, iz + 1), tx);
    return lerp(lerp(c00, c10, ty), lerp(c01, c11, ty), tz);
}

} // namespace

// ============================================================================
// RandomRange
// ============================================================================

template<>
float32 RandomRange<float32>::Sample(std::mt19937& rng) const {
    return min + (max - min) * Random01(rng);
}

template<>
Math::Vector3 RandomRange<Math::Vector3>::Sample(std::mt19937& rng) const {
    return Math::Vector3(
        min.x + (max.x - min.x) * Random01(rng),
        min.y + (max.y - min.y) * Random01(rng),
        min.z + (max.z - min.z) * Random01(rng));
}

template<>
Math::Vector4 RandomRange<Math::Vector4>::Sample(std::mt19937& rng) const {
    // Colors interpolate along one parameter to keep hue consistent
    return Math::Vector4::Lerp(min, max, Random01(rng));
}

// ============================================================================
// ParticleData
// ============================================================================

void ParticleData::Resize(uint32 new_capacity) {
    positions.resize(new_capacity);
    velocities.resize(new_capacity);
    colors.resize(new_capacity);
    start_colors.resize(new_capacity);
    sizes.resize(new_capacity);
    start_sizes.resize(new_capacity);
    rotations.resize(new_capacity);
    angular_velocities.resize(new_capacity);
    lifetimes.resize(new_capacity);
    ages.resize(new_capacity);
    texture_indices.resize(new_capacity);
    start_frames.resize(new_capacity);

    capacity = new_capacity;
    count = std::min(count, capacity);
}

void ParticleData::Kill(uint32 index) {
    if (index >= count) return;

    const uint32 last = count - 1;
    if (index != last) {
        positions[index] = positions[last];
        velocities[index] = velocities[last];
        colors[index] = colors[last];
        start_colors[index] = start_colors[last];
        sizes[index] = sizes[last];
        start_sizes[index] = start_sizes[last];
        rotations[index] = rotations[last];
        angular_velocities[index] = angular_velocities[last];
        lifetimes[index] = lifetimes[last];
        ages[index] = ages[last];
        texture_indices[index] = texture_indices[last];
        start_frames[index] = start_frames[last];
    }
    --count;
}

// ============================================================================
// EmissionShapeParams
// ============================================================================

Math::Vector3 EmissionShapeParams::SamplePosition(std::mt19937& rng) const {
    switch (shape) {
        case EmissionShape::Point:
        case EmissionShape::Mesh:
            return Math::Vector3::Zero;

        case EmissionShape::Sphere:
        case EmissionShape::Hemisphere: {
            Math::Vector3 dir = RandomUnitVector(rng);
            if (shape == EmissionShape::Hemisphere) {
                dir.y = std::abs(dir.y);
            }
            // thickness 0 = surface only, 1 = full volume (uniform)
            const float32 inner = std::clamp(1.0f - radius_thickness, 0.0f, 1.0f);
            const float32 inner3 = inner * inner * inner;
            const float32 r = radius * std::cbrt(inner3 + (1.0f - inner3) * Random01(rng));
            return dir * r;
        }

        case EmissionShape::Cone: {
            const float32 theta = Random01(rng) * 2.0f * kPi;
            const float32 r = std::sqrt(Random01(rng)) * radius;
            return Math::Vector3(r * std::cos(theta), 0.0f, r * std::sin(theta));
        }

        case EmissionShape::Box:
            return Math::Vector3(
                (Random01(rng) - 0.5f) * box_size.x,
                (Random01(rng) - 0.5f) * box_size.y,
                (Random01(rng) - 0.5f) * box_size.z);

        case EmissionShape::Circle: {
            const float32 theta = Random01(rng) * arc * kDegToRad;
            const float32 inner = std::clamp(1.0f - radius_thickness, 0.0f, 1.0f);
            const float32 r = radius * std::sqrt(inner * inner + (1.0f - inner * inner) * Random01(rng));
            return Math::Vector3(r * std::cos(theta), 0.0f, r * std::sin(theta));
        }

        case EmissionShape::Edge:
            return Math::Vector3((Random01(rng) - 0.5f) * radius * 2.0f, 0.0f, 0.0f);
    }
    return Math::Vector3::Zero;
}

Math::Vector3 EmissionShapeParams::SampleDirection(std::mt19937& rng, const Math::Vector3& position) const {
    switch (shape) {
        case EmissionShape::Sphere:
        case EmissionShape::Hemisphere: {
            if (position.LengthSquared() > 1e-8f) {
                return position.Normalized();
            }
            Math::Vector3 dir = RandomUnitVector(rng);
            if (shape == EmissionShape::Hemisphere) {
                dir.y = std::abs(dir.y);
            }
            return dir;
        }

        case EmissionShape::Cone: {
            // Uniform direction inside a cone around +Y
            const float32 cos_max = std::cos(std::clamp(angle, 0.0f, 180.0f) * kDegToRad);
            const float32 cos_theta = 1.0f - Random01(rng) * (1.0f - cos_max);
            const float32 sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
            const float32 phi = Random01(rng) * 2.0f * kPi;
            return Math::Vector3(sin_theta * std::cos(phi), cos_theta, sin_theta * std::sin(phi));
        }

        case EmissionShape::Circle: {
            Math::Vector3 dir(position.x, 0.0f, position.z);
            return dir.LengthSquared() > 1e-8f ? dir.Normalized() : Math::Vector3::Right;
        }

        case EmissionShape::Point:
        case EmissionShape::Mesh:
            return RandomUnitVector(rng);

        case EmissionShape::Box:
        case EmissionShape::Edge:
            return Math::Vector3::Up;
    }
    return Math::Vector3::Up;
}

// ============================================================================
// Modules
// ============================================================================

void VelocityModule::Initialize(ParticleData& particles, uint32 start, uint32 count, std::mt19937& rng) {
    for (uint32 i = start; i < start + count; ++i) {
        particles.velocities[i] += initial_velocity.Sample(rng);
    }
}

void ForceModule::Update(ParticleData& particles, float32 delta_time) {
    const Math::Vector3 acceleration = gravity + constant_force;
    const float32 damping = std::max(0.0f, 1.0f - drag * delta_time);

    for (uint32 i = 0; i < particles.count; ++i) {
        particles.velocities[i] = (particles.velocities[i] + acceleration * delta_time) * damping;
    }
}

Math::Vector4 ColorOverLifetimeModule::SampleGradient(float32 t) const {
    if (gradient.empty()) return Math::Vector4(1, 1, 1, 1);
    if (t <= gradient.front().time) return gradient.front().color;
    if (t >= gradient.back().time) return gradient.back().color;

    for (size_t i = 1; i < gradient.size(); ++i) {
        if (t <= gradient[i].time) {
            const GradientPoint& a = gradient[i - 1];
            const GradientPoint& b = gradient[i];
            const float32 span = b.time - a.time;
            const float32 local = span > 1e-6f ? (t - a.time) / span : 0.0f;
            return Math::Vector4::Lerp(a.color, b.color, local);
        }
    }
    return gradient.back().color;
}

void ColorOverLifetimeModule::Update(ParticleData& particles, float32 /*delta_time*/) {
    if (gradient.empty()) return;

    for (uint32 i = 0; i < particles.count; ++i) {
        particles.colors[i] = MultiplyComponents(particles.start_colors[i],
                                                 SampleGradient(particles.GetNormalizedAge(i)));
    }
}

float32 SizeOverLifetimeModule::SampleCurve(float32 t) const {
    if (size_curve.empty()) return 1.0f;
    if (t <= size_curve.front().time) return size_curve.front().value;
    if (t >= size_curve.back().time) return size_curve.back().value;

    for (size_t i = 1; i < size_curve.size(); ++i) {
        if (t <= size_curve[i].time) {
            const CurvePoint& a = size_curve[i - 1];
            const CurvePoint& b = size_curve[i];
            const float32 span = b.time - a.time;
            const float32 local = span > 1e-6f ? (t - a.time) / span : 0.0f;
            return a.value + (b.value - a.value) * local;
        }
    }
    return size_curve.back().value;
}

void SizeOverLifetimeModule::Update(ParticleData& particles, float32 /*delta_time*/) {
    for (uint32 i = 0; i < particles.count; ++i) {
        particles.sizes[i] = particles.start_sizes[i] * SampleCurve(particles.GetNormalizedAge(i)) * size_multiplier;
    }
}

void RotationOverLifetimeModule::Initialize(ParticleData& particles, uint32 start, uint32 count, std::mt19937& rng) {
    for (uint32 i = start; i < start + count; ++i) {
        float32 velocity = angular_velocity.Sample(rng);
        if (random_direction && Random01(rng) < 0.5f) {
            velocity = -velocity;
        }
        particles.angular_velocities[i] = velocity;
    }
}

void TextureSheetModule::Initialize(ParticleData& particles, uint32 start, uint32 count, std::mt19937& rng) {
    const uint32 total_frames = std::max(1u, tiles_x * tiles_y);
    for (uint32 i = start; i < start + count; ++i) {
        const uint32 frame = random_start_frame
            ? std::min(static_cast<uint32>(Random01(rng) * static_cast<float32>(total_frames)), total_frames - 1)
            : 0u;
        particles.start_frames[i] = frame;
        particles.texture_indices[i] = frame;
    }
}

void TextureSheetModule::Update(ParticleData& particles, float32 /*delta_time*/) {
    const uint32 total_frames = std::max(1u, tiles_x * tiles_y);
    for (uint32 i = 0; i < particles.count; ++i) {
        const auto elapsed = static_cast<uint32>(std::max(0.0f, particles.ages[i] * frame_rate));
        const uint32 frame = particles.start_frames[i] + elapsed;
        particles.texture_indices[i] = loop ? frame % total_frames : std::min(frame, total_frames - 1);
    }
}

void CollisionModule::Update(ParticleData& particles, float32 /*delta_time*/) {
    auto resolve = [&](uint32 i, const Math::Vector3& normal, float32 penetration) {
        particles.positions[i] += normal * penetration;

        Math::Vector3& v = particles.velocities[i];
        const float32 vn = Math::Vector3::Dot(v, normal);
        if (vn < 0.0f) {
            const Math::Vector3 normal_part = normal * vn;
            const Math::Vector3 tangent_part = v - normal_part;
            v = tangent_part * (1.0f - friction) - normal_part * bounce;
        }

        if (lifetime_loss > 0.0f) {
            particles.ages[i] += particles.lifetimes[i] * lifetime_loss;
        }
    };

    for (uint32 i = 0; i < particles.count; ++i) {
        const float32 radius = particles.sizes[i] * 0.5f * radius_scale;

        if (type == CollisionType::World) {
            const float32 penetration = ground_height + radius - particles.positions[i].y;
            if (penetration > 0.0f) {
                resolve(i, Math::Vector3::Up, penetration);
            }
        } else {
            for (const auto& plane : planes) {
                const Math::Vector3 normal = Math::Vector3(plane.x, plane.y, plane.z).Normalized();
                const float32 distance = Math::Vector3::Dot(normal, particles.positions[i]) - plane.w - radius;
                if (distance < 0.0f) {
                    resolve(i, normal, -distance);
                }
            }
        }
    }
}

void NoiseModule::Update(ParticleData& particles, float32 delta_time) {
    time_ += delta_time * scroll_speed;

    for (uint32 i = 0; i < particles.count; ++i) {
        const Math::Vector3& p = particles.positions[i];
        Math::Vector3 force;
        float32 amplitude = 1.0f;
        float32 freq = frequency;

        for (uint32 o = 0; o < std::max(1u, octaves); ++o) {
            force.x += ValueNoise(p.x * freq + time_, p.y * freq, p.z * freq) * amplitude;
            force.y += ValueNoise(p.x * freq, p.y * freq + time_ + 31.7f, p.z * freq) * amplitude;
            force.z += ValueNoise(p.x * freq, p.y * freq, p.z * freq + time_ + 57.3f) * amplitude;
            amplitude *= 0.5f;
            freq *= 2.0f;
        }

        particles.velocities[i] += force * (strength * delta_time);
    }
}

// ============================================================================
// ParticleEmitter
// ============================================================================

ParticleEmitter::ParticleEmitter()
    : rng_(std::random_device{}()) {
    particles_.Resize(1000);
}

ParticleEmitter::~ParticleEmitter() = default;

void ParticleEmitter::SetMaxParticles(uint32 max) {
    particles_.Resize(max);
}

void ParticleEmitter::SetEmissionRate(float32 particles_per_second) {
    emission_rate_ = std::max(0.0f, particles_per_second);
}

void ParticleEmitter::SetBurstCount(uint32 count, float32 interval) {
    burst_count_ = count;
    burst_interval_ = std::max(0.0f, interval);
    burst_timer_ = 0.0f;
    burst_fired_ = false;
}

void ParticleEmitter::SetDuration(float32 duration, bool looping) {
    duration_ = std::max(0.0f, duration);
    looping_ = looping;
}

void ParticleEmitter::SetParentTransform(const Math::Vector3& parent_position, const Math::Quaternion& parent_rotation) {
    parent_position_ = parent_position;
    parent_rotation_ = parent_rotation;
}

void ParticleEmitter::Play() {
    if (!playing_) {
        time_ = 0.0f;
        emission_accumulator_ = 0.0f;
        burst_timer_ = 0.0f;
        burst_fired_ = false;
    }
    playing_ = true;
}

void ParticleEmitter::Pause() {
    playing_ = false;
}

void ParticleEmitter::Stop(bool clear) {
    playing_ = false;
    if (clear) {
        Clear();
    }
}

void ParticleEmitter::Clear() {
    particles_.count = 0;
}

void ParticleEmitter::Emit(uint32 count) {
    SpawnParticles(count);
}

void ParticleEmitter::Update(float32 delta_time) {
    if (delta_time <= 0.0f) return;

    if (playing_) {
        // Continuous emission
        emission_accumulator_ += emission_rate_ * delta_time;
        if (emission_accumulator_ >= 1.0f) {
            const auto to_spawn = static_cast<uint32>(emission_accumulator_);
            emission_accumulator_ -= static_cast<float32>(to_spawn);
            SpawnParticles(to_spawn);
        }

        // Bursts
        if (burst_count_ > 0) {
            if (burst_interval_ <= 0.0f) {
                if (!burst_fired_) {
                    SpawnParticles(burst_count_);
                    burst_fired_ = true;
                }
            } else {
                burst_timer_ -= delta_time;
                while (burst_timer_ <= 0.0f) {
                    SpawnParticles(burst_count_);
                    burst_timer_ += burst_interval_;
                }
            }
        }

        time_ += delta_time;
        if (duration_ > 0.0f && time_ >= duration_) {
            if (looping_) {
                time_ = std::fmod(time_, duration_);
                burst_fired_ = false;
            } else {
                playing_ = false;
            }
        }
    }

    UpdateParticles(delta_time);
}

void ParticleEmitter::SpawnParticles(uint32 count) {
    const uint32 available = particles_.capacity - particles_.count;
    count = std::min(count, available);
    if (count == 0) return;

    const Math::Quaternion world_rotation = (parent_rotation_ * rotation).Normalized();
    const Math::Vector3 world_position = parent_position_ + parent_rotation_ * position;

    const uint32 start = particles_.count;
    for (uint32 i = start; i < start + count; ++i) {
        const Math::Vector3 local_pos = emission_shape.SamplePosition(rng_);
        const Math::Vector3 local_dir = emission_shape.SampleDirection(rng_, local_pos);
        const float32 speed = start_speed.Sample(rng_);

        if (simulate_in_world_space) {
            particles_.positions[i] = world_position + world_rotation * local_pos;
            particles_.velocities[i] = (world_rotation * local_dir) * speed;
        } else {
            particles_.positions[i] = local_pos;
            particles_.velocities[i] = local_dir * speed;
        }

        const Math::Vector4 color = start_color.Sample(rng_);
        const float32 size = std::max(0.0f, start_size.Sample(rng_));

        particles_.colors[i] = color;
        particles_.start_colors[i] = color;
        particles_.sizes[i] = size;
        particles_.start_sizes[i] = size;
        particles_.rotations[i] = start_rotation.Sample(rng_);
        particles_.angular_velocities[i] = 0.0f;
        particles_.lifetimes[i] = std::max(0.001f, lifetime.Sample(rng_));
        particles_.ages[i] = 0.0f;
        particles_.texture_indices[i] = 0;
        particles_.start_frames[i] = 0;
    }
    particles_.count += count;

    for (auto& module : modules_) {
        if (module->enabled) {
            module->Initialize(particles_, start, count, rng_);
        }
    }
}

void ParticleEmitter::UpdateParticles(float32 delta_time) {
    for (uint32 i = 0; i < particles_.count; ++i) {
        particles_.ages[i] += delta_time;
    }
    RemoveDeadParticles();

    for (auto& module : modules_) {
        if (module->enabled) {
            module->Update(particles_, delta_time);
        }
    }

    for (uint32 i = 0; i < particles_.count; ++i) {
        particles_.positions[i] += particles_.velocities[i] * delta_time;
        particles_.rotations[i] += particles_.angular_velocities[i] * delta_time;
    }

    // Collision modules may age particles past their lifetime
    RemoveDeadParticles();
}

void ParticleEmitter::RemoveDeadParticles() {
    for (uint32 i = 0; i < particles_.count;) {
        if (particles_.ages[i] >= particles_.lifetimes[i]) {
            particles_.Kill(i);
        } else {
            ++i;
        }
    }
}

// ============================================================================
// ParticleSystem
// ============================================================================

ParticleSystem::ParticleSystem(const String& name)
    : name_(name) {}

ParticleSystem::~ParticleSystem() = default;

void ParticleSystem::Update(float32 delta_time) {
    for (auto& emitter : emitters_) {
        emitter->SetParentTransform(position, rotation);
        emitter->Update(delta_time);
    }
}

void ParticleSystem::Clear() {
    for (auto& emitter : emitters_) {
        emitter->Clear();
    }
}

ParticleEmitter* ParticleSystem::AddEmitter() {
    emitters_.push_back(MakeUnique<ParticleEmitter>());
    ParticleEmitter* emitter = emitters_.back().get();
    emitter->SetParentTransform(position, rotation);
    if (playing_) {
        emitter->Play();
    }
    return emitter;
}

void ParticleSystem::RemoveEmitter(ParticleEmitter* emitter) {
    emitters_.erase(std::remove_if(emitters_.begin(), emitters_.end(),
        [emitter](const UniquePtr<ParticleEmitter>& e) { return e.get() == emitter; }),
        emitters_.end());
}

void ParticleSystem::Play() {
    playing_ = true;
    for (auto& emitter : emitters_) {
        emitter->Play();
    }
}

void ParticleSystem::Pause() {
    playing_ = false;
    for (auto& emitter : emitters_) {
        emitter->Pause();
    }
}

void ParticleSystem::Stop(bool clear) {
    playing_ = false;
    for (auto& emitter : emitters_) {
        emitter->Stop(clear);
    }
}

bool ParticleSystem::IsAlive() const {
    return std::any_of(emitters_.begin(), emitters_.end(),
        [](const UniquePtr<ParticleEmitter>& e) { return e->IsAlive(); });
}

uint32 ParticleSystem::GetTotalParticleCount() const {
    uint32 total = 0;
    for (const auto& emitter : emitters_) {
        total += emitter->GetParticleCount();
    }
    return total;
}

// ============================================================================
// ParticleManager
// ============================================================================

ParticleManager& ParticleManager::Instance() {
    static ParticleManager instance;
    return instance;
}

bool ParticleManager::Initialize() {
    return true;
}

void ParticleManager::Shutdown() {
    systems_.clear();
    loaded_descriptions_.clear();
}

void ParticleManager::Update(float32 delta_time) {
    const float32 scaled = delta_time * time_scale_;
    for (auto& system : systems_) {
        system->Update(scaled);
    }
}

SharedPtr<ParticleSystem> ParticleManager::CreateSystem(const String& name) {
    auto system = MakeShared<ParticleSystem>(name);
    systems_.push_back(system);
    return system;
}

void ParticleManager::DestroySystem(const SharedPtr<ParticleSystem>& system) {
    systems_.erase(std::remove(systems_.begin(), systems_.end(), system), systems_.end());
}

uint32 ParticleManager::GetTotalActiveParticles() const {
    uint32 total = 0;
    for (const auto& system : systems_) {
        total += system->GetTotalParticleCount();
    }
    return total;
}

SharedPtr<ParticleSystem> ParticleManager::Load(const String& path) {
    auto it = loaded_descriptions_.find(path);
    if (it == loaded_descriptions_.end()) {
        std::ifstream file(path);
        if (!file) {
            DRX_LOG_ERROR("Failed to open particle system: {}", path);
            return nullptr;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        it = loaded_descriptions_.emplace(path, buffer.str()).first;
    }
    return LoadFromString(it->second, path);
}

SharedPtr<ParticleSystem> ParticleManager::LoadFromString(const String& json, const String& name) {
    rapidjson::Document doc;
    doc.Parse(json.c_str(), json.size());
    if (doc.HasParseError() || !doc.IsObject()) {
        DRX_LOG_ERROR("Invalid particle JSON '{}': {} at offset {}", name,
                      rapidjson::GetParseError_En(doc.GetParseError()), doc.GetErrorOffset());
        return nullptr;
    }

    auto read_float = [](const rapidjson::Value& obj, const char* key, float32 fallback) {
        auto m = obj.FindMember(key);
        return (m != obj.MemberEnd() && m->value.IsNumber()) ? m->value.GetFloat() : fallback;
    };
    auto read_range = [](const rapidjson::Value& obj, const char* key, FloatRange fallback) {
        auto m = obj.FindMember(key);
        if (m == obj.MemberEnd()) return fallback;
        if (m->value.IsNumber()) return FloatRange{m->value.GetFloat(), m->value.GetFloat()};
        if (m->value.IsArray() && m->value.Size() == 2 && m->value[0].IsNumber() && m->value[1].IsNumber()) {
            return FloatRange{m->value[0].GetFloat(), m->value[1].GetFloat()};
        }
        return fallback;
    };
    auto read_vec3 = [](const rapidjson::Value& v, Math::Vector3 fallback) {
        if (!v.IsArray() || v.Size() != 3) return fallback;
        for (const auto& c : v.GetArray()) if (!c.IsNumber()) return fallback;
        return Math::Vector3(v[0].GetFloat(), v[1].GetFloat(), v[2].GetFloat());
    };
    auto read_vec4 = [](const rapidjson::Value& v, Math::Vector4 fallback) {
        if (!v.IsArray() || v.Size() != 4) return fallback;
        for (const auto& c : v.GetArray()) if (!c.IsNumber()) return fallback;
        return Math::Vector4(v[0].GetFloat(), v[1].GetFloat(), v[2].GetFloat(), v[3].GetFloat());
    };

    auto system = CreateSystem(name);

    auto emitters = doc.FindMember("emitters");
    if (emitters == doc.MemberEnd() || !emitters->value.IsArray()) {
        DRX_LOG_WARNING("Particle system '{}' has no emitters", name);
        return system;
    }

    for (const auto& e : emitters->value.GetArray()) {
        if (!e.IsObject()) continue;

        ParticleEmitter* emitter = system->AddEmitter();
        const auto max_particles = static_cast<uint32>(std::max(1.0f, read_float(e, "max_particles", 1000.0f)));
        emitter->SetMaxParticles(std::min(max_particles, global_max_particles_));
        emitter->SetEmissionRate(read_float(e, "emission_rate", 10.0f));

        bool looping = true;
        auto loop_member = e.FindMember("looping");
        if (loop_member != e.MemberEnd() && loop_member->value.IsBool()) {
            looping = loop_member->value.GetBool();
        }
        emitter->SetDuration(read_float(e, "duration", 5.0f), looping);

        auto burst = e.FindMember("burst");
        if (burst != e.MemberEnd() && burst->value.IsArray() && burst->value.Size() == 2 &&
            burst->value[0].IsNumber() && burst->value[1].IsNumber()) {
            emitter->SetBurstCount(static_cast<uint32>(std::max(0.0f, burst->value[0].GetFloat())),
                                   burst->value[1].GetFloat());
        }

        emitter->lifetime = read_range(e, "lifetime", emitter->lifetime);
        emitter->start_size = read_range(e, "start_size", emitter->start_size);
        emitter->start_speed = read_range(e, "start_speed", emitter->start_speed);
        emitter->start_rotation = read_range(e, "start_rotation", emitter->start_rotation);

        auto color = e.FindMember("start_color");
        if (color != e.MemberEnd() && color->value.IsArray()) {
            if (color->value.Size() == 2 && color->value[0].IsArray()) {
                emitter->start_color.min = read_vec4(color->value[0], emitter->start_color.min);
                emitter->start_color.max = read_vec4(color->value[1], emitter->start_color.max);
            } else {
                const Math::Vector4 c = read_vec4(color->value, emitter->start_color.min);
                emitter->start_color = {c, c};
            }
        }

        auto offset = e.FindMember("position");
        if (offset != e.MemberEnd()) {
            emitter->position = read_vec3(offset->value, emitter->position);
        }

        auto shape = e.FindMember("shape");
        if (shape != e.MemberEnd() && shape->value.IsString()) {
            static const HashMap<String, EmissionShape> shapes = {
                {"point", EmissionShape::Point}, {"sphere", EmissionShape::Sphere},
                {"hemisphere", EmissionShape::Hemisphere}, {"cone", EmissionShape::Cone},
                {"box", EmissionShape::Box}, {"circle", EmissionShape::Circle}, {"edge", EmissionShape::Edge}};
            auto found = shapes.find(shape->value.GetString());
            if (found != shapes.end()) {
                emitter->emission_shape.shape = found->second;
            } else {
                DRX_LOG_WARNING("Unknown particle shape '{}' in {}", shape->value.GetString(), name);
            }
        }
        emitter->emission_shape.radius = read_float(e, "radius", emitter->emission_shape.radius);
        emitter->emission_shape.radius_thickness = read_float(e, "radius_thickness", emitter->emission_shape.radius_thickness);
        emitter->emission_shape.angle = read_float(e, "angle", emitter->emission_shape.angle);
        auto box = e.FindMember("box_size");
        if (box != e.MemberEnd()) {
            emitter->emission_shape.box_size = read_vec3(box->value, emitter->emission_shape.box_size);
        }

        auto blend = e.FindMember("blend");
        if (blend != e.MemberEnd() && blend->value.IsString()) {
            const String mode = blend->value.GetString();
            if (mode == "additive") emitter->blend_mode = ParticleEmitter::BlendMode::Additive;
            else if (mode == "multiply") emitter->blend_mode = ParticleEmitter::BlendMode::Multiply;
            else emitter->blend_mode = ParticleEmitter::BlendMode::AlphaBlend;
        }

        auto gravity = e.FindMember("gravity");
        const float32 drag = read_float(e, "drag", 0.0f);
        if (gravity != e.MemberEnd() || drag > 0.0f) {
            auto* force = emitter->AddModule<ForceModule>();
            force->gravity = gravity != e.MemberEnd() ? read_vec3(gravity->value, force->gravity) : Math::Vector3::Zero;
            force->drag = drag;
        }

        auto color_curve = e.FindMember("color_over_lifetime");
        if (color_curve != e.MemberEnd() && color_curve->value.IsArray()) {
            auto* module = emitter->AddModule<ColorOverLifetimeModule>();
            for (const auto& point : color_curve->value.GetArray()) {
                if (point.IsArray() && point.Size() == 5 && point[0].IsNumber()) {
                    module->gradient.push_back({point[0].GetFloat(),
                        Math::Vector4(point[1].GetFloat(), point[2].GetFloat(), point[3].GetFloat(), point[4].GetFloat())});
                }
            }
        }

        auto size_curve = e.FindMember("size_over_lifetime");
        if (size_curve != e.MemberEnd() && size_curve->value.IsArray()) {
            auto* module = emitter->AddModule<SizeOverLifetimeModule>();
            for (const auto& point : size_curve->value.GetArray()) {
                if (point.IsArray() && point.Size() == 2 && point[0].IsNumber() && point[1].IsNumber()) {
                    module->size_curve.push_back({point[0].GetFloat(), point[1].GetFloat()});
                }
            }
        }

        const FloatRange spin = read_range(e, "angular_velocity", FloatRange{0.0f, 0.0f});
        if (spin.min != 0.0f || spin.max != 0.0f) {
            auto* module = emitter->AddModule<RotationOverLifetimeModule>();
            module->angular_velocity = spin;
        }

        const float32 noise = read_float(e, "noise", 0.0f);
        if (noise > 0.0f) {
            auto* module = emitter->AddModule<NoiseModule>();
            module->strength = noise;
            module->frequency = read_float(e, "noise_frequency", 1.0f);
        }

        auto collide = e.FindMember("collide_ground");
        if (collide != e.MemberEnd() && collide->value.IsNumber()) {
            auto* module = emitter->AddModule<CollisionModule>();
            module->type = CollisionModule::CollisionType::World;
            module->ground_height = collide->value.GetFloat();
            module->bounce = read_float(e, "bounce", module->bounce);
        }
    }

    return system;
}

} // namespace DRX::Particles

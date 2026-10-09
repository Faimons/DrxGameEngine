/**
 * DRX Engine - Terrain System Implementation
 */

#include "Terrain/Public/TerrainSystem.h"
#include "Renderer/Public/SceneRenderer.h"
#include "Core/Public/Log.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>

namespace DRX::Terrain {

namespace {

constexpr float32 kRadToDeg = 57.29577951308232f;
constexpr float32 kDegToRad = 0.017453292519943295f;

float32 SmoothStep(float32 edge0, float32 edge1, float32 x) {
    if (edge1 <= edge0) return x >= edge0 ? 1.0f : 0.0f;
    const float32 t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

// ============================================================================
// TerrainHeightmap
// ============================================================================

bool TerrainHeightmap::Create(uint32 width, uint32 height, float32 default_height) {
    if (width < 2 || height < 2) {
        DRX_LOG_ERROR("Heightmap must be at least 2x2 (got {}x{})", width, height);
        return false;
    }
    width_ = width;
    height_ = height;
    data_.assign(static_cast<size_t>(width) * height, default_height);
    return true;
}

bool TerrainHeightmap::LoadFromFile(const String& path) {
    const auto dot = path.rfind('.');
    const String ext = dot == String::npos ? String() : path.substr(dot + 1);

    if (ext == "raw" || ext == "r16") {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            DRX_LOG_ERROR("Failed to open heightmap {}", path);
            return false;
        }
        const auto size = static_cast<size_t>(file.tellg());
        const size_t pixels = size / 2;
        const auto side = static_cast<uint32>(std::lround(std::sqrt(static_cast<double>(pixels))));
        if (side < 2 || static_cast<size_t>(side) * side != pixels) {
            DRX_LOG_ERROR("RAW heightmap {} is not a square 16-bit image", path);
            return false;
        }
        Array<uint16> raw(pixels);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(pixels * 2));
        Create(side, side);
        for (size_t i = 0; i < pixels; ++i) {
            data_[i] = static_cast<float32>(raw[i]) / 65535.0f;
        }
        return true;
    }

    int w = 0, h = 0, channels = 0;
    stbi_us* pixels = stbi_load_16(path.c_str(), &w, &h, &channels, 1);
    if (!pixels) {
        DRX_LOG_ERROR("Failed to load heightmap image {}", path);
        return false;
    }
    const bool ok = Create(static_cast<uint32>(w), static_cast<uint32>(h));
    if (ok) {
        for (size_t i = 0; i < data_.size(); ++i) {
            data_[i] = static_cast<float32>(pixels[i]) / 65535.0f;
        }
    }
    stbi_image_free(pixels);
    return ok;
}

float32 TerrainHeightmap::GetHeight(uint32 x, uint32 y) const {
    if (data_.empty()) return 0.0f;
    x = std::min(x, width_ - 1);
    y = std::min(y, height_ - 1);
    return data_[static_cast<size_t>(y) * width_ + x];
}

float32 TerrainHeightmap::GetHeightBilinear(float32 u, float32 v) const {
    if (data_.empty()) return 0.0f;
    const float32 fx = std::clamp(u, 0.0f, 1.0f) * static_cast<float32>(width_ - 1);
    const float32 fy = std::clamp(v, 0.0f, 1.0f) * static_cast<float32>(height_ - 1);
    const auto x0 = static_cast<uint32>(fx);
    const auto y0 = static_cast<uint32>(fy);
    const float32 tx = fx - static_cast<float32>(x0);
    const float32 ty = fy - static_cast<float32>(y0);

    const float32 h00 = GetHeight(x0, y0);
    const float32 h10 = GetHeight(x0 + 1, y0);
    const float32 h01 = GetHeight(x0, y0 + 1);
    const float32 h11 = GetHeight(x0 + 1, y0 + 1);
    return (h00 + (h10 - h00) * tx) + ((h01 + (h11 - h01) * tx) - (h00 + (h10 - h00) * tx)) * ty;
}

void TerrainHeightmap::SetHeight(uint32 x, uint32 y, float32 height) {
    if (x < width_ && y < height_) {
        data_[static_cast<size_t>(y) * width_ + x] = height;
    }
}

void TerrainHeightmap::Resample(uint32 width, uint32 height) {
    if (width == width_ && height == height_) return;
    TerrainHeightmap result;
    if (!result.Create(width, height)) return;
    for (uint32 y = 0; y < height; ++y) {
        for (uint32 x = 0; x < width; ++x) {
            result.data_[static_cast<size_t>(y) * width + x] =
                GetHeightBilinear(static_cast<float32>(x) / static_cast<float32>(width - 1),
                                  static_cast<float32>(y) / static_cast<float32>(height - 1));
        }
    }
    *this = std::move(result);
}

void TerrainHeightmap::Normalize() {
    if (data_.empty()) return;
    const auto [min_it, max_it] = std::minmax_element(data_.begin(), data_.end());
    const float32 min_h = *min_it;
    const float32 range = *max_it - min_h;
    for (auto& h : data_) {
        h = range > 1e-6f ? (h - min_h) / range : 0.0f;
    }
}

template<typename Fn>
void TerrainHeightmap::ForEachInBrush(uint32 cx, uint32 cy, uint32 radius, Fn&& fn) {
    if (radius == 0 || data_.empty()) return;
    const auto r = static_cast<int32>(radius);
    for (int32 dy = -r; dy <= r; ++dy) {
        for (int32 dx = -r; dx <= r; ++dx) {
            const int32 px = static_cast<int32>(cx) + dx;
            const int32 py = static_cast<int32>(cy) + dy;
            if (px < 0 || py < 0 || px >= static_cast<int32>(width_) || py >= static_cast<int32>(height_)) continue;
            const float32 dist = std::sqrt(static_cast<float32>(dx * dx + dy * dy));
            if (dist > static_cast<float32>(radius)) continue;
            fn(static_cast<uint32>(px), static_cast<uint32>(py), 1.0f - dist / static_cast<float32>(radius));
        }
    }
}

void TerrainHeightmap::Flatten(uint32 cx, uint32 cy, uint32 radius, float32 target, float32 strength) {
    ForEachInBrush(cx, cy, radius, [&](uint32 x, uint32 y, float32 falloff) {
        const float32 current = GetHeight(x, y);
        SetHeight(x, y, current + (target - current) * std::clamp(strength * falloff, 0.0f, 1.0f));
    });
}

void TerrainHeightmap::Raise(uint32 cx, uint32 cy, uint32 radius, float32 amount, float32 falloff_power) {
    ForEachInBrush(cx, cy, radius, [&](uint32 x, uint32 y, float32 falloff) {
        SetHeight(x, y, GetHeight(x, y) + amount * std::pow(falloff, std::max(0.01f, falloff_power)));
    });
}

void TerrainHeightmap::Lower(uint32 cx, uint32 cy, uint32 radius, float32 amount, float32 falloff_power) {
    Raise(cx, cy, radius, -amount, falloff_power);
}

void TerrainHeightmap::Smooth(uint32 cx, uint32 cy, uint32 radius, float32 strength) {
    const Array<float32> source = data_;
    ForEachInBrush(cx, cy, radius, [&](uint32 x, uint32 y, float32 falloff) {
        float32 sum = 0.0f;
        int32 count = 0;
        for (int32 ky = -1; ky <= 1; ++ky) {
            for (int32 kx = -1; kx <= 1; ++kx) {
                const int32 nx = static_cast<int32>(x) + kx;
                const int32 ny = static_cast<int32>(y) + ky;
                if (nx < 0 || ny < 0 || nx >= static_cast<int32>(width_) || ny >= static_cast<int32>(height_)) continue;
                sum += source[static_cast<size_t>(ny) * width_ + static_cast<size_t>(nx)];
                ++count;
            }
        }
        const float32 current = source[static_cast<size_t>(y) * width_ + x];
        SetHeight(x, y, current + (sum / static_cast<float32>(count) - current) * std::clamp(strength * falloff, 0.0f, 1.0f));
    });
}

void TerrainHeightmap::Noise(uint32 cx, uint32 cy, uint32 radius, float32 strength, uint32 seed) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float32> dis(-1.0f, 1.0f);
    ForEachInBrush(cx, cy, radius, [&](uint32 x, uint32 y, float32 falloff) {
        SetHeight(x, y, GetHeight(x, y) + dis(gen) * strength * falloff);
    });
}

void TerrainHeightmap::GeneratePerlinNoise(float32 frequency, int32 octaves, float32 persistence, uint32 seed) {
    if (data_.empty()) return;

    std::mt19937 gen(seed);
    Array<int32> p(512);
    for (int32 i = 0; i < 256; ++i) p[static_cast<size_t>(i)] = i;
    std::shuffle(p.begin(), p.begin() + 256, gen);
    for (int32 i = 0; i < 256; ++i) p[static_cast<size_t>(256 + i)] = p[static_cast<size_t>(i)];

    auto fade = [](float32 t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
    auto lerp = [](float32 a, float32 b, float32 t) { return a + t * (b - a); };
    auto grad = [](int32 hash, float32 x, float32 y) {
        const int32 h = hash & 7;
        const float32 u = h < 4 ? x : y;
        const float32 v = h < 4 ? y : x;
        return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
    };
    auto perlin = [&](float32 x, float32 y) {
        const auto xi = static_cast<int32>(std::floor(x)) & 255;
        const auto yi = static_cast<int32>(std::floor(y)) & 255;
        x -= std::floor(x);
        y -= std::floor(y);
        const float32 u = fade(x);
        const float32 v = fade(y);
        const int32 a = p[static_cast<size_t>(xi)] + yi;
        const int32 b = p[static_cast<size_t>(xi + 1)] + yi;
        return lerp(lerp(grad(p[static_cast<size_t>(a)], x, y), grad(p[static_cast<size_t>(b)], x - 1, y), u),
                    lerp(grad(p[static_cast<size_t>(a + 1)], x, y - 1), grad(p[static_cast<size_t>(b + 1)], x - 1, y - 1), u), v);
    };

    for (uint32 y = 0; y < height_; ++y) {
        for (uint32 x = 0; x < width_; ++x) {
            float32 total = 0.0f;
            float32 amplitude = 1.0f;
            float32 freq = frequency;
            float32 max_value = 0.0f;
            for (int32 octave = 0; octave < std::max(1, octaves); ++octave) {
                total += perlin(static_cast<float32>(x) * freq / static_cast<float32>(width_),
                                static_cast<float32>(y) * freq / static_cast<float32>(height_)) * amplitude;
                max_value += amplitude;
                amplitude *= persistence;
                freq *= 2.0f;
            }
            data_[static_cast<size_t>(y) * width_ + x] = (total / max_value + 1.0f) * 0.5f;
        }
    }
}

void TerrainHeightmap::GenerateDiamondSquare(float32 roughness, uint32 seed) {
    uint32 n = 1;
    while (n + 1 < std::max(width_, height_)) n *= 2;
    n += 1;
    const uint32 original_w = width_;
    const uint32 original_h = height_;
    Create(n, n, 0.0f);

    std::mt19937 gen(seed);
    std::uniform_real_distribution<float32> dis(-1.0f, 1.0f);
    auto at = [&](uint32 x, uint32 y) -> float32& { return data_[static_cast<size_t>(y) * n + x]; };

    at(0, 0) = dis(gen);
    at(n - 1, 0) = dis(gen);
    at(0, n - 1) = dis(gen);
    at(n - 1, n - 1) = dis(gen);

    float32 scale = 1.0f;
    for (uint32 step = n - 1; step > 1; step /= 2) {
        const uint32 half = step / 2;
        for (uint32 y = half; y < n - 1; y += step) {
            for (uint32 x = half; x < n - 1; x += step) {
                const float32 avg = (at(x - half, y - half) + at(x + half, y - half) +
                                     at(x - half, y + half) + at(x + half, y + half)) * 0.25f;
                at(x, y) = avg + dis(gen) * scale;
            }
        }
        for (uint32 y = 0; y < n; y += half) {
            for (uint32 x = (y + half) % step; x < n; x += step) {
                float32 sum = 0.0f;
                int32 count = 0;
                if (y >= half) { sum += at(x, y - half); ++count; }
                if (y + half < n) { sum += at(x, y + half); ++count; }
                if (x >= half) { sum += at(x - half, y); ++count; }
                if (x + half < n) { sum += at(x + half, y); ++count; }
                at(x, y) = sum / static_cast<float32>(count) + dis(gen) * scale;
            }
        }
        scale *= roughness;
    }

    Normalize();
    if (original_w >= 2 && original_h >= 2) {
        Resample(original_w, original_h);
    }
}

void TerrainHeightmap::ApplyHydraulicErosion(int32 iterations, uint32 seed) {
    if (width_ < 3 || height_ < 3) return;

    std::mt19937 gen(seed);
    std::uniform_real_distribution<float32> dis(0.0f, 1.0f);

    constexpr float32 kInertia = 0.05f;
    constexpr float32 kCapacity = 4.0f;
    constexpr float32 kDeposition = 0.3f;
    constexpr float32 kErosion = 0.3f;
    constexpr float32 kEvaporation = 0.01f;
    constexpr float32 kMinSlope = 0.01f;
    constexpr int32 kMaxLifetime = 30;

    const auto w = static_cast<float32>(width_ - 1);
    const auto h = static_cast<float32>(height_ - 1);

    for (int32 iter = 0; iter < iterations; ++iter) {
        float32 px = dis(gen) * w;
        float32 py = dis(gen) * h;
        float32 dx = 0.0f, dy = 0.0f, sediment = 0.0f, water = 1.0f, speed = 1.0f;

        for (int32 life = 0; life < kMaxLifetime; ++life) {
            const auto x = static_cast<uint32>(px);
            const auto y = static_cast<uint32>(py);
            if (x >= width_ - 1 || y >= height_ - 1) break;

            const float32 h00 = GetHeight(x, y), h10 = GetHeight(x + 1, y);
            const float32 h01 = GetHeight(x, y + 1), h11 = GetHeight(x + 1, y + 1);
            const float32 gx = (h10 - h00) + (h11 - h01);
            const float32 gy = (h01 - h00) + (h11 - h10);

            dx = dx * kInertia - gx * (1.0f - kInertia);
            dy = dy * kInertia - gy * (1.0f - kInertia);
            const float32 len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-4f) break;
            dx /= len;
            dy /= len;

            const float32 nx = px + dx;
            const float32 ny = py + dy;
            if (nx < 0.0f || ny < 0.0f || nx >= w || ny >= h) break;

            const float32 old_height = GetHeightBilinear(px / w, py / h);
            const float32 delta = GetHeightBilinear(nx / w, ny / h) - old_height;
            const float32 capacity = std::max(-delta, kMinSlope) * speed * water * kCapacity;

            float32& cell = data_[static_cast<size_t>(y) * width_ + x];
            if (sediment > capacity || delta > 0.0f) {
                const float32 deposit = delta > 0.0f ? std::min(delta, sediment) : (sediment - capacity) * kDeposition;
                sediment -= deposit;
                cell += deposit;
            } else {
                const float32 erode = std::min((capacity - sediment) * kErosion, -delta);
                sediment += erode;
                cell -= erode;
            }

            px = nx;
            py = ny;
            water *= 1.0f - kEvaporation;
            speed = std::sqrt(std::max(0.0f, speed * speed - delta));
        }
    }
}

// ============================================================================
// TerrainSplatMap
// ============================================================================

bool TerrainSplatMap::Create(uint32 width, uint32 height, uint32 num_layers) {
    if (width == 0 || height == 0 || num_layers == 0) {
        width_ = height_ = num_layers_ = 0;
        layer_weights_.clear();
        return false;
    }
    width_ = width;
    height_ = height;
    num_layers_ = num_layers;
    layer_weights_.assign(num_layers, Array<float32>(static_cast<size_t>(width) * height, 0.0f));
    std::fill(layer_weights_[0].begin(), layer_weights_[0].end(), 1.0f);
    return true;
}

float32 TerrainSplatMap::GetWeight(uint32 x, uint32 y, uint32 layer) const {
    if (layer >= num_layers_ || x >= width_ || y >= height_) return 0.0f;
    return layer_weights_[layer][static_cast<size_t>(y) * width_ + x];
}

float32 TerrainSplatMap::GetWeightBilinear(float32 u, float32 v, uint32 layer) const {
    if (layer >= num_layers_ || width_ == 0) return 0.0f;
    const float32 fx = std::clamp(u, 0.0f, 1.0f) * static_cast<float32>(width_ - 1);
    const float32 fy = std::clamp(v, 0.0f, 1.0f) * static_cast<float32>(height_ - 1);
    const auto x0 = static_cast<uint32>(fx);
    const auto y0 = static_cast<uint32>(fy);
    const uint32 x1 = std::min(x0 + 1, width_ - 1);
    const uint32 y1 = std::min(y0 + 1, height_ - 1);
    const float32 tx = fx - static_cast<float32>(x0);
    const float32 ty = fy - static_cast<float32>(y0);
    const float32 a = GetWeight(x0, y0, layer) + (GetWeight(x1, y0, layer) - GetWeight(x0, y0, layer)) * tx;
    const float32 b = GetWeight(x0, y1, layer) + (GetWeight(x1, y1, layer) - GetWeight(x0, y1, layer)) * tx;
    return a + (b - a) * ty;
}

void TerrainSplatMap::SetWeight(uint32 x, uint32 y, uint32 layer, float32 weight) {
    if (layer >= num_layers_ || x >= width_ || y >= height_) return;
    layer_weights_[layer][static_cast<size_t>(y) * width_ + x] = std::clamp(weight, 0.0f, 1.0f);
    NormalizeWeights(x, y);
}

void TerrainSplatMap::Paint(uint32 cx, uint32 cy, uint32 radius, uint32 layer, float32 strength, float32 falloff) {
    if (layer >= num_layers_ || radius == 0) return;
    const auto r = static_cast<int32>(radius);
    for (int32 dy = -r; dy <= r; ++dy) {
        for (int32 dx = -r; dx <= r; ++dx) {
            const int32 px = static_cast<int32>(cx) + dx;
            const int32 py = static_cast<int32>(cy) + dy;
            if (px < 0 || py < 0 || px >= static_cast<int32>(width_) || py >= static_cast<int32>(height_)) continue;
            const float32 dist = std::sqrt(static_cast<float32>(dx * dx + dy * dy));
            if (dist > static_cast<float32>(radius)) continue;
            const float32 factor = std::pow(1.0f - dist / static_cast<float32>(radius), std::max(0.01f, falloff));
            float32& w = layer_weights_[layer][static_cast<size_t>(py) * width_ + static_cast<size_t>(px)];
            w = std::clamp(w + strength * factor, 0.0f, 1.0f);
            NormalizeWeights(static_cast<uint32>(px), static_cast<uint32>(py));
        }
    }
}

void TerrainSplatMap::NormalizeWeights(uint32 x, uint32 y) {
    const size_t index = static_cast<size_t>(y) * width_ + x;
    float32 total = 0.0f;
    for (uint32 i = 0; i < num_layers_; ++i) total += layer_weights_[i][index];
    if (total > 1e-4f) {
        for (uint32 i = 0; i < num_layers_; ++i) layer_weights_[i][index] /= total;
    }
}

Array<uint8> TerrainSplatMap::GeneratePixels(uint32 layer_start) const {
    Array<uint8> pixels(static_cast<size_t>(width_) * height_ * 4, 0);
    for (uint32 y = 0; y < height_; ++y) {
        for (uint32 x = 0; x < width_; ++x) {
            for (uint32 c = 0; c < 4; ++c) {
                pixels[(static_cast<size_t>(y) * width_ + x) * 4 + c] =
                    static_cast<uint8>(std::lround(GetWeight(x, y, layer_start + c) * 255.0f));
            }
        }
    }
    return pixels;
}

// ============================================================================
// TerrainFoliage
// ============================================================================

void TerrainFoliage::AddType(const FoliageType& type) {
    RemoveType(type.name);
    types_.push_back(type);
}

void TerrainFoliage::RemoveType(const String& name) {
    types_.erase(std::remove_if(types_.begin(), types_.end(), [&](const FoliageType& t) { return t.name == name; }),
                 types_.end());
    instances_.erase(name);
}

void TerrainFoliage::Clear() {
    instances_.clear();
}

void TerrainFoliage::Generate(const Terrain& terrain, uint32 seed) {
    instances_.clear();
    const TerrainConfig& config = terrain.GetConfig();
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float32> dis(0.0f, 1.0f);

    for (const auto& type : types_) {
        auto& list = instances_[type.name];
        const auto count = static_cast<uint32>(std::max(0.0f, type.density) * config.size_x * config.size_z / 100.0f);
        list.reserve(count);

        for (uint32 i = 0; i < count; ++i) {
            const float32 x = config.origin.x + dis(gen) * config.size_x;
            const float32 z = config.origin.z + dis(gen) * config.size_z;
            const float32 height = terrain.GetHeightAt(x, z);
            const float32 slope = terrain.GetSlopeAt(x, z);
            const float32 scale_roll = dis(gen);
            const float32 yaw = type.random_rotation ? dis(gen) * 6.2831853f : 0.0f;

            if (height < type.min_height || height > type.max_height) continue;
            if (slope < type.min_slope || slope > type.max_slope) continue;

            FoliageInstance instance;
            instance.position = Math::Vector3(x, height, z);
            instance.scale = type.min_scale + scale_roll * (type.max_scale - type.min_scale);
            const Math::Quaternion spin = Math::Quaternion::FromAxisAngle(Math::Vector3::Up, yaw);
            instance.rotation = type.align_to_normal
                ? Math::Quaternion::FromToRotation(Math::Vector3::Up, terrain.GetNormalAt(x, z)) * spin
                : spin;
            list.push_back(instance);
        }
    }
}

void TerrainFoliage::Render(Renderer::SceneRenderer& renderer, const Math::Vector3& camera_position) const {
    for (const auto& type : types_) {
        auto it = instances_.find(type.name);
        if (it == instances_.end() || !type.mesh.IsValid()) continue;

        const float32 max_distance_sq = type.render_distance * type.render_distance;
        for (const auto& instance : it->second) {
            if (Math::Vector3::DistanceSquared(instance.position, camera_position) > max_distance_sq) continue;
            const Math::Matrix4 transform = Math::Matrix4::Translation(instance.position) *
                                            instance.rotation.ToMatrix() * Math::Matrix4::Scale(instance.scale);
            renderer.DrawMesh(type.mesh, transform, type.material);
        }
    }
}

uint32 TerrainFoliage::GetInstanceCount() const {
    uint32 count = 0;
    for (const auto& [name, list] : instances_) count += static_cast<uint32>(list.size());
    return count;
}

const Array<FoliageInstance>* TerrainFoliage::GetInstances(const String& type) const {
    auto it = instances_.find(type);
    return it != instances_.end() ? &it->second : nullptr;
}

// ============================================================================
// Terrain
// ============================================================================

bool Terrain::Initialize(const TerrainConfig& config) {
    config_ = config;

    // LOD steps must divide the chunk resolution
    uint32 resolution = 1;
    while (resolution < std::max(2u, config_.chunk_resolution)) resolution *= 2;
    config_.chunk_resolution = resolution;
    config_.chunks_x = std::max(1u, config_.chunks_x);
    config_.chunks_z = std::max(1u, config_.chunks_z);
    config_.lod_levels = std::clamp(config_.lod_levels, 1u, 1u + static_cast<uint32>(std::log2(static_cast<float32>(resolution))));
    if (config_.size_x <= 0.0f || config_.size_z <= 0.0f) {
        DRX_LOG_ERROR("Terrain size must be positive");
        return false;
    }

    if (layers_.empty()) {
        layers_ = DefaultLayers(config_.height_scale);
    }

    heightmap_.Create(HeightmapResolutionX(), HeightmapResolutionZ(), 0.0f);
    chunks_.clear();
    for (uint32 z = 0; z < config_.chunks_z; ++z) {
        for (uint32 x = 0; x < config_.chunks_x; ++x) {
            chunks_.push_back(MakeUnique<TerrainChunk>(x, z));
        }
    }
    material.roughness = config_.roughness;
    RebuildMeshData();
    return true;
}

Array<TerrainLayer> Terrain::DefaultLayers(float32 height_scale) {
    Array<TerrainLayer> layers(4);
    layers[0] = {"Sand", Math::Vector3(0.76f, 0.70f, 0.50f), -1e9f, height_scale * 0.12f, 0.0f, 90.0f, height_scale * 0.04f};
    layers[1] = {"Grass", Math::Vector3(0.30f, 0.45f, 0.18f), height_scale * 0.08f, height_scale * 0.7f, 0.0f, 32.0f, height_scale * 0.05f};
    layers[2] = {"Rock", Math::Vector3(0.42f, 0.40f, 0.38f), -1e9f, 1e9f, 30.0f, 90.0f, 6.0f};
    layers[3] = {"Snow", Math::Vector3(0.92f, 0.94f, 0.97f), height_scale * 0.75f, 1e9f, 0.0f, 40.0f, height_scale * 0.05f};
    return layers;
}

void Terrain::SetLayers(const Array<TerrainLayer>& layers) {
    layers_ = layers;
    RebuildMeshData();
}

void Terrain::SetHeightmap(const TerrainHeightmap& heightmap) {
    heightmap_ = heightmap;
    heightmap_.Resample(HeightmapResolutionX(), HeightmapResolutionZ());
    RebuildMeshData();
}

void Terrain::RebuildMeshData() {
    chunk_mesh_data_.assign(chunks_.size(), {});
    for (size_t i = 0; i < chunks_.size(); ++i) {
        TerrainChunk& chunk = *chunks_[i];
        chunk.bounds_ = Renderer::AABB{};
        for (uint32 lod = 0; lod < config_.lod_levels; ++lod) {
            Renderer::MeshData mesh = BuildChunkMesh(chunk.chunk_x_, chunk.chunk_z_, lod);
            if (lod == 0) chunk.bounds_ = mesh.ComputeBounds();
            chunk_mesh_data_[i].push_back(std::move(mesh));
        }
    }
    meshes_dirty_ = true;
}

Math::Vector3 Terrain::HeightmapNormal(float32 u, float32 v) const {
    // Central differences in world units
    const float32 du = 1.0f / static_cast<float32>(heightmap_.GetWidth() - 1);
    const float32 dv = 1.0f / static_cast<float32>(heightmap_.GetHeight() - 1);
    const float32 hl = heightmap_.GetHeightBilinear(u - du, v) * config_.height_scale;
    const float32 hr = heightmap_.GetHeightBilinear(u + du, v) * config_.height_scale;
    const float32 hd = heightmap_.GetHeightBilinear(u, v - dv) * config_.height_scale;
    const float32 hu = heightmap_.GetHeightBilinear(u, v + dv) * config_.height_scale;
    const float32 step_x = 2.0f * du * config_.size_x;
    const float32 step_z = 2.0f * dv * config_.size_z;
    return Math::Vector3(-(hr - hl) / step_x, 1.0f, -(hu - hd) / step_z).Normalized();
}

Math::Vector3 Terrain::ComputeVertexColor(float32 u, float32 v, float32 height, float32 slope_degrees) const {
    if (layers_.empty()) return Math::Vector3::One;

    Math::Vector3 color = Math::Vector3::Zero;
    float32 total = 0.0f;
    for (uint32 i = 0; i < layers_.size(); ++i) {
        const TerrainLayer& layer = layers_[i];
        float32 weight;
        if (!splatmap_.IsEmpty() && i < splatmap_.GetNumLayers()) {
            weight = splatmap_.GetWeightBilinear(u, v, i);
        } else {
            const float32 blend = std::max(0.01f, layer.blend);
            weight = SmoothStep(layer.min_height - blend, layer.min_height + blend, height) *
                     (1.0f - SmoothStep(layer.max_height - blend, layer.max_height + blend, height)) *
                     SmoothStep(layer.min_slope - blend, layer.min_slope + blend, slope_degrees) *
                     (1.0f - SmoothStep(layer.max_slope - blend, layer.max_slope + blend, slope_degrees));
            // Later layers (rock, snow) override earlier ones where they apply
            weight *= 1.0f + static_cast<float32>(i);
        }
        color += layer.color * weight;
        total += weight;
    }
    return total > 1e-5f ? color / total : layers_.front().color;
}

Renderer::MeshData Terrain::BuildChunkMesh(uint32 chunk_x, uint32 chunk_z, uint32 lod) const {
    Renderer::MeshData mesh;
    const uint32 res = config_.chunk_resolution;
    const uint32 step = 1u << lod;
    const uint32 verts = res / step + 1;

    const float32 total_x = static_cast<float32>(config_.chunks_x * res);
    const float32 total_z = static_cast<float32>(config_.chunks_z * res);

    auto make_vertex = [&](uint32 gx, uint32 gz, float32 y_offset) {
        const float32 u = static_cast<float32>(gx) / total_x;
        const float32 v = static_cast<float32>(gz) / total_z;
        const float32 height = heightmap_.GetHeight(gx, gz) * config_.height_scale;
        const Math::Vector3 normal = HeightmapNormal(u, v);
        const float32 slope = std::acos(std::clamp(normal.y, -1.0f, 1.0f)) * kRadToDeg;

        Renderer::Vertex vertex;
        vertex.position = Math::Vector3(config_.origin.x + u * config_.size_x,
                                        config_.origin.y + height + y_offset,
                                        config_.origin.z + v * config_.size_z);
        vertex.normal = normal;
        vertex.color = ComputeVertexColor(u, v, height, slope);
        vertex.tex_coord = Math::Vector2(u * config_.size_x * 0.1f, v * config_.size_z * 0.1f);
        return vertex;
    };

    const uint32 base_x = chunk_x * res;
    const uint32 base_z = chunk_z * res;
    for (uint32 z = 0; z < verts; ++z) {
        for (uint32 x = 0; x < verts; ++x) {
            mesh.vertices.push_back(make_vertex(base_x + x * step, base_z + z * step, 0.0f));
        }
    }

    for (uint32 z = 0; z + 1 < verts; ++z) {
        for (uint32 x = 0; x + 1 < verts; ++x) {
            const uint32 tl = z * verts + x;
            const uint32 tr = tl + 1;
            const uint32 bl = tl + verts;
            const uint32 br = bl + 1;
            // Counter-clockwise when seen from above (+Y)
            mesh.indices.insert(mesh.indices.end(), {tl, bl, tr, tr, bl, br});
        }
    }

    // Skirts along the four edges hide cracks between neighbouring LODs
    if (config_.skirt_depth > 0.0f) {
        auto add_skirt = [&](auto edge_index, bool flip) {
            const auto first_skirt = static_cast<uint32>(mesh.vertices.size());
            for (uint32 i = 0; i < verts; ++i) {
                Renderer::Vertex v = mesh.vertices[edge_index(i)];
                v.position.y -= config_.skirt_depth;
                mesh.vertices.push_back(v);
            }
            for (uint32 i = 0; i + 1 < verts; ++i) {
                const uint32 a = edge_index(i), b = edge_index(i + 1);
                const uint32 c = first_skirt + i, d = first_skirt + i + 1;
                if (flip) mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
                else mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
            }
        };
        // Winding chosen so every skirt faces away from the chunk
        add_skirt([&](uint32 i) { return i; }, true);                               // z = 0 edge (-Z)
        add_skirt([&](uint32 i) { return (verts - 1) * verts + i; }, false);        // z = max edge (+Z)
        add_skirt([&](uint32 i) { return i * verts; }, false);                      // x = 0 edge (-X)
        add_skirt([&](uint32 i) { return i * verts + verts - 1; }, true);           // x = max edge (+X)
    }

    return mesh;
}

bool Terrain::UploadMeshes(Renderer::SceneRenderer& renderer) {
    if (!meshes_dirty_) return true;

    ReleaseMeshes(renderer);
    bool ok = true;
    for (size_t i = 0; i < chunks_.size(); ++i) {
        TerrainChunk& chunk = *chunks_[i];
        for (uint32 lod = 0; lod < chunk_mesh_data_[i].size(); ++lod) {
            const MeshHandle handle = renderer.CreateMesh(chunk_mesh_data_[i][lod], "terrain_chunk");
            ok &= handle.IsValid();
            chunk.lod_meshes_.push_back(handle);
        }
    }
    meshes_dirty_ = !ok;
    return ok;
}

void Terrain::ReleaseMeshes(Renderer::SceneRenderer& renderer) {
    for (auto& chunk : chunks_) {
        for (MeshHandle mesh : chunk->lod_meshes_) {
            renderer.DestroyMesh(mesh);
        }
        chunk->lod_meshes_.clear();
    }
    meshes_dirty_ = true;
}

void Terrain::Render(Renderer::SceneRenderer& renderer, const Math::Vector3& camera_position) {
    if (meshes_dirty_ && !UploadMeshes(renderer)) {
        return;
    }

    for (auto& chunk : chunks_) {
        if (chunk->lod_meshes_.empty()) continue;

        const Math::Vector3 closest = Math::Vector3::Max(chunk->bounds_.min, Math::Vector3::Min(camera_position, chunk->bounds_.max));
        const float32 distance = Math::Vector3::Distance(closest, camera_position);

        uint32 lod = 0;
        while (lod + 1 < chunk->lod_meshes_.size() && lod < config_.lod_distances.size() &&
               distance > config_.lod_distances[lod]) {
            ++lod;
        }
        chunk->current_lod_ = lod;
        renderer.DrawMesh(chunk->lod_meshes_[lod], Math::Matrix4::Identity(), material);
    }

    foliage_.Render(renderer, camera_position);
}

bool Terrain::IsInside(float32 x, float32 z) const {
    return x >= config_.origin.x && z >= config_.origin.z &&
           x <= config_.origin.x + config_.size_x && z <= config_.origin.z + config_.size_z;
}

float32 Terrain::GetHeightAt(float32 x, float32 z) const {
    const float32 u = (x - config_.origin.x) / config_.size_x;
    const float32 v = (z - config_.origin.z) / config_.size_z;
    return config_.origin.y + heightmap_.GetHeightBilinear(u, v) * config_.height_scale;
}

Math::Vector3 Terrain::GetNormalAt(float32 x, float32 z) const {
    return HeightmapNormal((x - config_.origin.x) / config_.size_x, (z - config_.origin.z) / config_.size_z);
}

float32 Terrain::GetSlopeAt(float32 x, float32 z) const {
    return std::acos(std::clamp(GetNormalAt(x, z).y, -1.0f, 1.0f)) * kRadToDeg;
}

bool Terrain::Raycast(const Math::Vector3& origin, const Math::Vector3& direction, float32 max_distance,
                      float32& out_distance, Math::Vector3& out_point) const {
    const Math::Vector3 dir = direction.Normalized();
    if (dir.LengthSquared() < 1e-8f || max_distance <= 0.0f) return false;

    // March in steps of half a heightmap cell, then refine by bisection
    const float32 cell = std::min(config_.size_x / static_cast<float32>(heightmap_.GetWidth() - 1),
                                  config_.size_z / static_cast<float32>(heightmap_.GetHeight() - 1));
    const float32 step = std::max(0.05f, cell * 0.5f);

    float32 previous = 0.0f;
    for (float32 t = 0.0f; t <= max_distance; t += step) {
        const Math::Vector3 p = origin + dir * t;
        if (IsInside(p.x, p.z) && p.y <= GetHeightAt(p.x, p.z)) {
            float32 lo = previous, hi = t;
            for (int i = 0; i < 16; ++i) {
                const float32 mid = (lo + hi) * 0.5f;
                const Math::Vector3 q = origin + dir * mid;
                if (q.y <= GetHeightAt(q.x, q.z)) hi = mid; else lo = mid;
            }
            out_distance = hi;
            out_point = origin + dir * hi;
            return true;
        }
        previous = t;
    }
    return false;
}

Renderer::MeshData Terrain::BuildCollisionMesh(uint32 step) const {
    Renderer::MeshData mesh;
    step = std::max(1u, step);
    const uint32 w = heightmap_.GetWidth();
    const uint32 h = heightmap_.GetHeight();
    if (w < 2 || h < 2) return mesh;

    Array<uint32> xs, zs;
    for (uint32 x = 0; x < w; x += step) xs.push_back(x);
    if (xs.back() != w - 1) xs.push_back(w - 1);
    for (uint32 z = 0; z < h; z += step) zs.push_back(z);
    if (zs.back() != h - 1) zs.push_back(h - 1);

    for (uint32 gz : zs) {
        for (uint32 gx : xs) {
            Renderer::Vertex v;
            v.position = Math::Vector3(config_.origin.x + static_cast<float32>(gx) / static_cast<float32>(w - 1) * config_.size_x,
                                       config_.origin.y + heightmap_.GetHeight(gx, gz) * config_.height_scale,
                                       config_.origin.z + static_cast<float32>(gz) / static_cast<float32>(h - 1) * config_.size_z);
            mesh.vertices.push_back(v);
        }
    }
    const auto cols = static_cast<uint32>(xs.size());
    for (uint32 z = 0; z + 1 < zs.size(); ++z) {
        for (uint32 x = 0; x + 1 < cols; ++x) {
            const uint32 tl = z * cols + x, tr = tl + 1, bl = tl + cols, br = bl + 1;
            mesh.indices.insert(mesh.indices.end(), {tl, bl, tr, tr, bl, br});
        }
    }
    return mesh;
}

Renderer::AABB Terrain::GetBounds() const {
    Renderer::AABB bounds;
    for (const auto& chunk : chunks_) bounds.Merge(chunk->bounds_);
    return bounds;
}

} // namespace DRX::Terrain

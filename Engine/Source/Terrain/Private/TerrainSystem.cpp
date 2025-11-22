/**
 * DRX Engine - Terrain System Implementation
 */

#include "Terrain/Public/TerrainSystem.h"
#include <cmath>
#include <algorithm>
#include <random>
#include <fstream>

namespace DRX::Terrain {

// ============================================================================
// TerrainChunk
// ============================================================================

TerrainChunk::TerrainChunk(int32 x, int32 z, uint32 resolution)
    : chunk_x_(x), chunk_z_(z), resolution_(resolution) {
    heights_.resize((resolution_ + 1) * (resolution_ + 1));
}

TerrainChunk::~TerrainChunk() {
    lod_meshes_.clear();
}

bool TerrainChunk::Generate(const float32* heightmap_data, uint32 heightmap_width,
                             float32 height_scale, float32 terrain_size) {
    if (!heightmap_data) return false;

    uint32 chunk_size = resolution_;
    float32 chunk_world_size = terrain_size / std::sqrt(static_cast<float32>(heightmap_width));

    // Calculate world position of chunk center
    center_ = Math::Vector3(
        (chunk_x_ + 0.5f) * chunk_world_size,
        0,
        (chunk_z_ + 0.5f) * chunk_world_size
    );

    bounds_min_ = Math::Vector3(
        chunk_x_ * chunk_world_size,
        0,
        chunk_z_ * chunk_world_size
    );
    bounds_max_ = Math::Vector3(
        (chunk_x_ + 1) * chunk_world_size,
        height_scale,
        (chunk_z_ + 1) * chunk_world_size
    );

    // Sample heightmap for this chunk
    for (uint32 z = 0; z <= resolution_; ++z) {
        for (uint32 x = 0; x <= resolution_; ++x) {
            uint32 hx = chunk_x_ * resolution_ + x;
            uint32 hz = chunk_z_ * resolution_ + z;

            hx = std::min(hx, heightmap_width - 1);
            hz = std::min(hz, heightmap_width - 1);

            float32 height = heightmap_data[hz * heightmap_width + hx] * height_scale;
            heights_[z * (resolution_ + 1) + x] = height;

            // Update bounds
            bounds_min_.y = std::min(bounds_min_.y, height);
            bounds_max_.y = std::max(bounds_max_.y, height);
        }
    }

    // Generate meshes for each LOD level
    for (uint32 lod = 0; lod < 4; ++lod) {
        GenerateMesh(lod);
    }

    return true;
}

void TerrainChunk::GenerateMesh(uint32 lod) {
    uint32 step = 1 << lod;  // LOD0: 1, LOD1: 2, LOD2: 4, LOD3: 8
    uint32 vertex_count = (resolution_ / step + 1);

    Array<Renderer::StaticVertex> vertices;
    Array<uint32> indices;

    // Generate vertices
    for (uint32 z = 0; z <= resolution_; z += step) {
        for (uint32 x = 0; x <= resolution_; x += step) {
            Renderer::StaticVertex vertex;

            float32 fx = static_cast<float32>(x) / resolution_;
            float32 fz = static_cast<float32>(z) / resolution_;

            vertex.position.x = bounds_min_.x + fx * (bounds_max_.x - bounds_min_.x);
            vertex.position.z = bounds_min_.z + fz * (bounds_max_.z - bounds_min_.z);
            vertex.position.y = heights_[z * (resolution_ + 1) + x];

            vertex.texcoord.x = fx;
            vertex.texcoord.y = fz;

            vertex.normal = Math::Vector3(0, 1, 0);  // Will be calculated later
            vertex.tangent = Math::Vector4(1, 0, 0, 1);

            vertices.push_back(vertex);
        }
    }

    // Generate indices
    for (uint32 z = 0; z < vertex_count - 1; ++z) {
        for (uint32 x = 0; x < vertex_count - 1; ++x) {
            uint32 tl = z * vertex_count + x;
            uint32 tr = tl + 1;
            uint32 bl = (z + 1) * vertex_count + x;
            uint32 br = bl + 1;

            // First triangle
            indices.push_back(tl);
            indices.push_back(bl);
            indices.push_back(tr);

            // Second triangle
            indices.push_back(tr);
            indices.push_back(bl);
            indices.push_back(br);
        }
    }

    // Calculate normals
    GenerateNormals(vertices, indices);

    // Create mesh
    auto mesh = std::make_shared<Renderer::Mesh>();
    // mesh->Create(vertices, indices);  // Would call actual mesh creation
    lod_meshes_.push_back(mesh);
}

void TerrainChunk::GenerateNormals(Array<Renderer::StaticVertex>& vertices,
                                    const Array<uint32>& indices) {
    // Reset normals
    for (auto& v : vertices) {
        v.normal = Math::Vector3::Zero;
    }

    // Calculate face normals and accumulate
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32 i0 = indices[i];
        uint32 i1 = indices[i + 1];
        uint32 i2 = indices[i + 2];

        Math::Vector3 v0 = vertices[i0].position;
        Math::Vector3 v1 = vertices[i1].position;
        Math::Vector3 v2 = vertices[i2].position;

        Math::Vector3 edge1 = v1 - v0;
        Math::Vector3 edge2 = v2 - v0;
        Math::Vector3 normal = Math::Vector3::Cross(edge1, edge2).Normalized();

        vertices[i0].normal = vertices[i0].normal + normal;
        vertices[i1].normal = vertices[i1].normal + normal;
        vertices[i2].normal = vertices[i2].normal + normal;
    }

    // Normalize
    for (auto& v : vertices) {
        v.normal = v.normal.Normalized();

        // Calculate tangent
        Math::Vector3 up = Math::Vector3::Up;
        Math::Vector3 tangent = Math::Vector3::Cross(v.normal, up).Normalized();
        if (tangent.LengthSquared() < 0.001f) {
            tangent = Math::Vector3::Cross(v.normal, Math::Vector3::Forward).Normalized();
        }
        v.tangent = Math::Vector4(tangent.x, tangent.y, tangent.z, 1.0f);
    }
}

void TerrainChunk::UpdateLOD(const Math::Vector3& camera_pos) {
    float32 distance = (camera_pos - center_).Length();

    // Simple LOD selection based on distance
    if (distance < 50.0f) current_lod_ = 0;
    else if (distance < 100.0f) current_lod_ = 1;
    else if (distance < 200.0f) current_lod_ = 2;
    else current_lod_ = 3;
}

void TerrainChunk::Render(VkCommandBuffer cmd) {
    if (current_lod_ < lod_meshes_.size()) {
        // lod_meshes_[current_lod_]->Draw(cmd);
    }
}

// ============================================================================
// TerrainHeightmap
// ============================================================================

TerrainHeightmap::TerrainHeightmap() {}

TerrainHeightmap::~TerrainHeightmap() {}

bool TerrainHeightmap::LoadFromFile(const String& path) {
    // Load raw heightmap file or image
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    // Determine size from file
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    // Assume square heightmap with 16-bit values
    size_t pixel_count = file_size / 2;
    width_ = height_ = static_cast<uint32>(std::sqrt(pixel_count));

    Array<uint16> raw_data(pixel_count);
    file.read(reinterpret_cast<char*>(raw_data.data()), file_size);

    data_.resize(pixel_count);
    for (size_t i = 0; i < pixel_count; ++i) {
        data_[i] = static_cast<float32>(raw_data[i]) / 65535.0f;
    }

    return true;
}

bool TerrainHeightmap::LoadFromTexture(SharedPtr<Renderer::Texture> texture) {
    // Would read texture data and convert to heightmap
    return false;
}

bool TerrainHeightmap::Create(uint32 width, uint32 height, float32 default_height) {
    width_ = width;
    height_ = height;
    data_.resize(width * height, default_height);
    return true;
}

float32 TerrainHeightmap::GetHeight(uint32 x, uint32 y) const {
    if (x >= width_ || y >= height_) return 0;
    return data_[y * width_ + x];
}

float32 TerrainHeightmap::GetHeightBilinear(float32 u, float32 v) const {
    float32 fx = u * (width_ - 1);
    float32 fy = v * (height_ - 1);

    uint32 x0 = static_cast<uint32>(fx);
    uint32 y0 = static_cast<uint32>(fy);
    uint32 x1 = std::min(x0 + 1, width_ - 1);
    uint32 y1 = std::min(y0 + 1, height_ - 1);

    float32 tx = fx - x0;
    float32 ty = fy - y0;

    float32 h00 = GetHeight(x0, y0);
    float32 h10 = GetHeight(x1, y0);
    float32 h01 = GetHeight(x0, y1);
    float32 h11 = GetHeight(x1, y1);

    float32 h0 = h00 + (h10 - h00) * tx;
    float32 h1 = h01 + (h11 - h01) * tx;

    return h0 + (h1 - h0) * ty;
}

Math::Vector3 TerrainHeightmap::GetNormal(uint32 x, uint32 y) const {
    float32 hL = GetHeight(x > 0 ? x - 1 : x, y);
    float32 hR = GetHeight(x < width_ - 1 ? x + 1 : x, y);
    float32 hD = GetHeight(x, y > 0 ? y - 1 : y);
    float32 hU = GetHeight(x, y < height_ - 1 ? y + 1 : y);

    Math::Vector3 normal(hL - hR, 2.0f, hD - hU);
    return normal.Normalized();
}

void TerrainHeightmap::SetHeight(uint32 x, uint32 y, float32 height) {
    if (x < width_ && y < height_) {
        data_[y * width_ + x] = height;
    }
}

void TerrainHeightmap::Flatten(uint32 cx, uint32 cy, uint32 radius, float32 target, float32 strength) {
    for (int32 y = -static_cast<int32>(radius); y <= static_cast<int32>(radius); ++y) {
        for (int32 x = -static_cast<int32>(radius); x <= static_cast<int32>(radius); ++x) {
            int32 px = cx + x;
            int32 py = cy + y;

            if (px < 0 || py < 0 || px >= static_cast<int32>(width_) || py >= static_cast<int32>(height_))
                continue;

            float32 dist = std::sqrt(static_cast<float32>(x * x + y * y));
            if (dist > radius) continue;

            float32 falloff = 1.0f - (dist / radius);
            float32 current = GetHeight(px, py);
            float32 newHeight = current + (target - current) * strength * falloff;
            SetHeight(px, py, newHeight);
        }
    }
}

void TerrainHeightmap::Raise(uint32 cx, uint32 cy, uint32 radius, float32 amount, float32 falloff) {
    for (int32 y = -static_cast<int32>(radius); y <= static_cast<int32>(radius); ++y) {
        for (int32 x = -static_cast<int32>(radius); x <= static_cast<int32>(radius); ++x) {
            int32 px = cx + x;
            int32 py = cy + y;

            if (px < 0 || py < 0 || px >= static_cast<int32>(width_) || py >= static_cast<int32>(height_))
                continue;

            float32 dist = std::sqrt(static_cast<float32>(x * x + y * y));
            if (dist > radius) continue;

            float32 factor = std::pow(1.0f - (dist / radius), falloff);
            float32 current = GetHeight(px, py);
            SetHeight(px, py, current + amount * factor);
        }
    }
}

void TerrainHeightmap::Lower(uint32 cx, uint32 cy, uint32 radius, float32 amount, float32 falloff) {
    Raise(cx, cy, radius, -amount, falloff);
}

void TerrainHeightmap::Smooth(uint32 cx, uint32 cy, uint32 radius, float32 strength) {
    Array<float32> temp_data = data_;

    for (int32 y = -static_cast<int32>(radius); y <= static_cast<int32>(radius); ++y) {
        for (int32 x = -static_cast<int32>(radius); x <= static_cast<int32>(radius); ++x) {
            int32 px = cx + x;
            int32 py = cy + y;

            if (px < 0 || py < 0 || px >= static_cast<int32>(width_) || py >= static_cast<int32>(height_))
                continue;

            float32 dist = std::sqrt(static_cast<float32>(x * x + y * y));
            if (dist > radius) continue;

            // Average of neighbors
            float32 sum = 0;
            int32 count = 0;

            for (int32 ky = -1; ky <= 1; ++ky) {
                for (int32 kx = -1; kx <= 1; ++kx) {
                    int32 nx = px + kx;
                    int32 ny = py + ky;
                    if (nx >= 0 && ny >= 0 && nx < static_cast<int32>(width_) && ny < static_cast<int32>(height_)) {
                        sum += data_[ny * width_ + nx];
                        count++;
                    }
                }
            }

            float32 avg = sum / count;
            float32 current = data_[py * width_ + px];
            float32 falloff = 1.0f - (dist / radius);
            temp_data[py * width_ + px] = current + (avg - current) * strength * falloff;
        }
    }

    data_ = temp_data;
}

void TerrainHeightmap::Noise(uint32 cx, uint32 cy, uint32 radius, float32 scale, float32 strength) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float32> dis(-1.0f, 1.0f);

    for (int32 y = -static_cast<int32>(radius); y <= static_cast<int32>(radius); ++y) {
        for (int32 x = -static_cast<int32>(radius); x <= static_cast<int32>(radius); ++x) {
            int32 px = cx + x;
            int32 py = cy + y;

            if (px < 0 || py < 0 || px >= static_cast<int32>(width_) || py >= static_cast<int32>(height_))
                continue;

            float32 dist = std::sqrt(static_cast<float32>(x * x + y * y));
            if (dist > radius) continue;

            float32 falloff = 1.0f - (dist / radius);
            float32 noise = dis(gen) * scale * strength * falloff;
            float32 current = GetHeight(px, py);
            SetHeight(px, py, current + noise);
        }
    }
}

void TerrainHeightmap::GeneratePerlinNoise(float32 frequency, int32 octaves, float32 persistence) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float32> dis(0.0f, 1000.0f);
    float32 seed = dis(gen);

    auto fade = [](float32 t) { return t * t * t * (t * (t * 6 - 15) + 10); };
    auto lerp = [](float32 a, float32 b, float32 t) { return a + t * (b - a); };

    auto grad = [](int32 hash, float32 x, float32 y) {
        int32 h = hash & 3;
        float32 u = h < 2 ? x : y;
        float32 v = h < 2 ? y : x;
        return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
    };

    // Simple permutation table
    Array<int32> p(512);
    for (int32 i = 0; i < 256; ++i) p[i] = i;
    std::shuffle(p.begin(), p.begin() + 256, gen);
    for (int32 i = 0; i < 256; ++i) p[256 + i] = p[i];

    for (uint32 y = 0; y < height_; ++y) {
        for (uint32 x = 0; x < width_; ++x) {
            float32 total = 0;
            float32 amplitude = 1.0f;
            float32 freq = frequency;
            float32 max_value = 0;

            for (int32 octave = 0; octave < octaves; ++octave) {
                float32 nx = x * freq / width_ + seed;
                float32 ny = y * freq / height_ + seed;

                int32 X = static_cast<int32>(std::floor(nx)) & 255;
                int32 Y = static_cast<int32>(std::floor(ny)) & 255;

                nx -= std::floor(nx);
                ny -= std::floor(ny);

                float32 u = fade(nx);
                float32 v = fade(ny);

                int32 A = p[X] + Y;
                int32 B = p[X + 1] + Y;

                float32 res = lerp(
                    lerp(grad(p[A], nx, ny), grad(p[B], nx - 1, ny), u),
                    lerp(grad(p[A + 1], nx, ny - 1), grad(p[B + 1], nx - 1, ny - 1), u),
                    v
                );

                total += res * amplitude;
                max_value += amplitude;
                amplitude *= persistence;
                freq *= 2;
            }

            data_[y * width_ + x] = (total / max_value + 1.0f) * 0.5f;
        }
    }
}

void TerrainHeightmap::GenerateDiamondSquare(float32 roughness) {
    // Ensure power of 2 + 1 size
    uint32 size = std::max(width_, height_);
    uint32 n = 1;
    while (n + 1 < size) n *= 2;
    n += 1;

    if (width_ != n || height_ != n) {
        Create(n, n, 0.0f);
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float32> dis(-1.0f, 1.0f);

    // Initialize corners
    data_[0] = dis(gen);
    data_[n - 1] = dis(gen);
    data_[(n - 1) * n] = dis(gen);
    data_[(n - 1) * n + n - 1] = dis(gen);

    float32 scale = 1.0f;
    uint32 step = n - 1;

    while (step > 1) {
        uint32 half = step / 2;

        // Diamond step
        for (uint32 y = half; y < n - 1; y += step) {
            for (uint32 x = half; x < n - 1; x += step) {
                float32 avg = (
                    data_[(y - half) * n + (x - half)] +
                    data_[(y - half) * n + (x + half)] +
                    data_[(y + half) * n + (x - half)] +
                    data_[(y + half) * n + (x + half)]
                ) / 4.0f;
                data_[y * n + x] = avg + dis(gen) * scale;
            }
        }

        // Square step
        for (uint32 y = 0; y < n; y += half) {
            for (uint32 x = (y + half) % step; x < n; x += step) {
                float32 sum = 0;
                int32 count = 0;

                if (y >= half) { sum += data_[(y - half) * n + x]; count++; }
                if (y + half < n) { sum += data_[(y + half) * n + x]; count++; }
                if (x >= half) { sum += data_[y * n + (x - half)]; count++; }
                if (x + half < n) { sum += data_[y * n + (x + half)]; count++; }

                data_[y * n + x] = sum / count + dis(gen) * scale;
            }
        }

        step = half;
        scale *= roughness;
    }

    // Normalize to 0-1
    float32 min_h = *std::min_element(data_.begin(), data_.end());
    float32 max_h = *std::max_element(data_.begin(), data_.end());
    float32 range = max_h - min_h;

    for (auto& h : data_) {
        h = (h - min_h) / range;
    }
}

void TerrainHeightmap::ApplyHydraulicErosion(int32 iterations) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float32> dis(0.0f, 1.0f);

    const float32 inertia = 0.05f;
    const float32 capacity = 4.0f;
    const float32 deposition = 0.3f;
    const float32 erosion = 0.3f;
    const float32 evaporation = 0.01f;
    const float32 min_slope = 0.01f;
    const int32 max_lifetime = 30;

    for (int32 iter = 0; iter < iterations; ++iter) {
        float32 px = dis(gen) * (width_ - 1);
        float32 py = dis(gen) * (height_ - 1);
        float32 dx = 0, dy = 0;
        float32 sediment = 0;
        float32 water = 1;
        float32 speed = 1;

        for (int32 lifetime = 0; lifetime < max_lifetime; ++lifetime) {
            int32 x = static_cast<int32>(px);
            int32 y = static_cast<int32>(py);

            if (x < 0 || y < 0 || x >= static_cast<int32>(width_) - 1 || y >= static_cast<int32>(height_) - 1)
                break;

            // Calculate gradient
            float32 h00 = data_[y * width_ + x];
            float32 h10 = data_[y * width_ + x + 1];
            float32 h01 = data_[(y + 1) * width_ + x];
            float32 h11 = data_[(y + 1) * width_ + x + 1];

            float32 gx = (h10 - h00) + (h11 - h01);
            float32 gy = (h01 - h00) + (h11 - h10);

            // Update direction with inertia
            dx = dx * inertia - gx * (1 - inertia);
            dy = dy * inertia - gy * (1 - inertia);

            float32 len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.0001f) break;

            dx /= len;
            dy /= len;

            float32 new_px = px + dx;
            float32 new_py = py + dy;

            if (new_px < 0 || new_py < 0 || new_px >= width_ - 1 || new_py >= height_ - 1)
                break;

            float32 old_height = GetHeightBilinear(px / (width_ - 1), py / (height_ - 1));
            float32 new_height = GetHeightBilinear(new_px / (width_ - 1), new_py / (height_ - 1));
            float32 delta_height = new_height - old_height;

            float32 c = std::max(-delta_height, min_slope) * speed * water * capacity;

            if (sediment > c) {
                // Deposit
                float32 deposit = (sediment - c) * deposition;
                sediment -= deposit;
                data_[y * width_ + x] += deposit;
            } else {
                // Erode
                float32 erode = std::min((c - sediment) * erosion, -delta_height);
                sediment += erode;
                data_[y * width_ + x] -= erode;
            }

            px = new_px;
            py = new_py;
            water *= (1 - evaporation);
            speed = std::sqrt(speed * speed + delta_height);
        }
    }
}

// ============================================================================
// TerrainSplatMap
// ============================================================================

TerrainSplatMap::TerrainSplatMap() {}

TerrainSplatMap::~TerrainSplatMap() {}

bool TerrainSplatMap::Create(uint32 width, uint32 height, uint32 num_layers) {
    width_ = width;
    height_ = height;
    num_layers_ = num_layers;

    layer_weights_.resize(num_layers);
    for (auto& layer : layer_weights_) {
        layer.resize(width * height, 0.0f);
    }

    // Set first layer to full weight
    if (num_layers > 0) {
        std::fill(layer_weights_[0].begin(), layer_weights_[0].end(), 1.0f);
    }

    return true;
}

bool TerrainSplatMap::LoadFromFile(const String& path) {
    // Would load splat map from image file
    return false;
}

float32 TerrainSplatMap::GetWeight(uint32 x, uint32 y, uint32 layer) const {
    if (layer >= num_layers_ || x >= width_ || y >= height_) return 0;
    return layer_weights_[layer][y * width_ + x];
}

void TerrainSplatMap::SetWeight(uint32 x, uint32 y, uint32 layer, float32 weight) {
    if (layer >= num_layers_ || x >= width_ || y >= height_) return;
    layer_weights_[layer][y * width_ + x] = std::clamp(weight, 0.0f, 1.0f);
    NormalizeWeights(x, y);
}

void TerrainSplatMap::Paint(uint32 cx, uint32 cy, uint32 radius, uint32 layer,
                            float32 strength, float32 falloff) {
    if (layer >= num_layers_) return;

    for (int32 y = -static_cast<int32>(radius); y <= static_cast<int32>(radius); ++y) {
        for (int32 x = -static_cast<int32>(radius); x <= static_cast<int32>(radius); ++x) {
            int32 px = cx + x;
            int32 py = cy + y;

            if (px < 0 || py < 0 || px >= static_cast<int32>(width_) || py >= static_cast<int32>(height_))
                continue;

            float32 dist = std::sqrt(static_cast<float32>(x * x + y * y));
            if (dist > radius) continue;

            float32 factor = std::pow(1.0f - (dist / radius), falloff);
            float32 current = GetWeight(px, py, layer);
            float32 newWeight = current + strength * factor;

            layer_weights_[layer][py * width_ + px] = std::clamp(newWeight, 0.0f, 1.0f);
            NormalizeWeights(px, py);
        }
    }
}

void TerrainSplatMap::NormalizeWeights(uint32 x, uint32 y) {
    float32 total = 0;
    for (uint32 i = 0; i < num_layers_; ++i) {
        total += layer_weights_[i][y * width_ + x];
    }

    if (total > 0.001f) {
        for (uint32 i = 0; i < num_layers_; ++i) {
            layer_weights_[i][y * width_ + x] /= total;
        }
    }
}

SharedPtr<Renderer::Texture> TerrainSplatMap::GenerateSplatTexture(uint32 layer_start) const {
    // Generate RGBA texture with 4 layer weights
    // Would create and upload texture
    return nullptr;
}

// ============================================================================
// TerrainFoliage
// ============================================================================

TerrainFoliage::TerrainFoliage() {}

TerrainFoliage::~TerrainFoliage() {}

void TerrainFoliage::AddFoliageType(const FoliageType& type) {
    foliage_types_[type.name] = type;
}

void TerrainFoliage::RemoveFoliageType(const String& name) {
    foliage_types_.erase(name);
    instances_.erase(name);
}

void TerrainFoliage::Generate(const TerrainHeightmap& heightmap, const TerrainSplatMap& splatmap,
                               const TerrainConfig& config) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float32> dis(0.0f, 1.0f);

    instances_.clear();

    for (auto& [name, type] : foliage_types_) {
        Array<FoliageInstance>& type_instances = instances_[name];

        // Calculate number of instances based on density
        uint32 instance_count = static_cast<uint32>(type.density * config.terrain_width * config.terrain_length / 100.0f);

        for (uint32 i = 0; i < instance_count; ++i) {
            float32 u = dis(gen);
            float32 v = dis(gen);

            float32 height = heightmap.GetHeightBilinear(u, v) * config.height_scale;
            Math::Vector3 normal = heightmap.GetNormal(
                static_cast<uint32>(u * heightmap.GetWidth()),
                static_cast<uint32>(v * heightmap.GetHeight())
            );

            // Check height constraints
            if (height < type.min_height || height > type.max_height) continue;

            // Check slope constraints
            float32 slope = std::acos(normal.y) * 180.0f / 3.14159265359f;
            if (slope < type.min_slope || slope > type.max_slope) continue;

            FoliageInstance instance;
            instance.position = Math::Vector3(
                u * config.terrain_width,
                height,
                v * config.terrain_length
            );

            // Random scale
            float32 scale = type.min_scale + dis(gen) * (type.max_scale - type.min_scale);
            instance.scale = Math::Vector3(scale, scale, scale);

            // Rotation
            if (type.align_to_normal) {
                Math::Vector3 up = normal;
                Math::Vector3 forward = Math::Vector3::Cross(Math::Vector3::Right, up).Normalized();
                Math::Vector3 right = Math::Vector3::Cross(up, forward);

                float32 yaw = type.random_rotation ? dis(gen) * 360.0f : 0;
                instance.rotation = Math::Quaternion::FromAxisAngle(up, yaw * 3.14159265359f / 180.0f);
            } else {
                float32 yaw = type.random_rotation ? dis(gen) * 360.0f : 0;
                instance.rotation = Math::Quaternion::FromAxisAngle(Math::Vector3::Up, yaw * 3.14159265359f / 180.0f);
            }

            instance.color = Math::Vector4(1, 1, 1, 1);

            type_instances.push_back(instance);
        }
    }
}

void TerrainFoliage::Clear() {
    instances_.clear();
}

void TerrainFoliage::Render(VkCommandBuffer cmd, const Math::Matrix4& view_projection) {
    // Render foliage using instanced rendering
}

void TerrainFoliage::UpdateCulling(const Math::Vector3& camera_pos, float32 cull_distance) {
    // Mark instances outside cull distance as invisible
}

uint32 TerrainFoliage::GetInstanceCount() const {
    uint32 count = 0;
    for (const auto& [name, type_instances] : instances_) {
        count += static_cast<uint32>(type_instances.size());
    }
    return count;
}

// ============================================================================
// Terrain
// ============================================================================

Terrain::Terrain() {}

Terrain::~Terrain() {
    Shutdown();
}

bool Terrain::Initialize(const TerrainConfig& config) {
    config_ = config;

    // Create heightmap
    heightmap_.Create(
        config.num_chunks_x * config.chunk_resolution + 1,
        config.num_chunks_z * config.chunk_resolution + 1,
        0.5f
    );

    // Create splat map
    splatmap_.Create(config.splat_map_resolution, config.splat_map_resolution, config.max_layers);

    return true;
}

void Terrain::Shutdown() {
    chunks_.clear();
    layers_.clear();
}

void Terrain::Update(const Math::Vector3& camera_pos, float32 delta_time) {
    UpdateChunkLODs(camera_pos);
    foliage_.UpdateCulling(camera_pos, 100.0f);
}

void Terrain::Render(VkCommandBuffer cmd, const Math::Matrix4& view_projection) {
    for (auto& chunk : chunks_) {
        chunk->Render(cmd);
    }
    foliage_.Render(cmd, view_projection);
}

void Terrain::SetHeightmap(const TerrainHeightmap& heightmap) {
    heightmap_ = heightmap;
    GenerateChunks();
}

void Terrain::AddLayer(const TerrainLayer& layer) {
    layers_.push_back(layer);
}

void Terrain::RemoveLayer(uint32 index) {
    if (index < layers_.size()) {
        layers_.erase(layers_.begin() + index);
    }
}

float32 Terrain::GetHeightAt(float32 x, float32 z) const {
    float32 u = x / config_.terrain_width;
    float32 v = z / config_.terrain_length;
    return heightmap_.GetHeightBilinear(u, v) * config_.height_scale;
}

Math::Vector3 Terrain::GetNormalAt(float32 x, float32 z) const {
    uint32 hx = static_cast<uint32>((x / config_.terrain_width) * heightmap_.GetWidth());
    uint32 hz = static_cast<uint32>((z / config_.terrain_length) * heightmap_.GetHeight());
    return heightmap_.GetNormal(hx, hz);
}

bool Terrain::Raycast(const Math::Vector3& origin, const Math::Vector3& direction,
                      float32& out_distance, Math::Vector3& out_point) const {
    // Simple terrain raycast using ray marching
    const float32 step = 1.0f;
    const float32 max_distance = 1000.0f;

    Math::Vector3 pos = origin;
    float32 distance = 0;

    while (distance < max_distance) {
        float32 terrain_height = GetHeightAt(pos.x, pos.z);

        if (pos.y <= terrain_height) {
            out_distance = distance;
            out_point = pos;
            return true;
        }

        pos = pos + direction * step;
        distance += step;
    }

    return false;
}

Math::Vector3 Terrain::GetMin() const {
    return Math::Vector3(0, 0, 0);
}

Math::Vector3 Terrain::GetMax() const {
    return Math::Vector3(config_.terrain_width, config_.height_scale, config_.terrain_length);
}

void Terrain::GenerateChunks() {
    chunks_.clear();

    for (uint32 z = 0; z < config_.num_chunks_z; ++z) {
        for (uint32 x = 0; x < config_.num_chunks_x; ++x) {
            auto chunk = std::make_unique<TerrainChunk>(x, z, config_.chunk_resolution);
            chunk->Generate(
                heightmap_.GetData(),
                heightmap_.GetWidth(),
                config_.height_scale,
                config_.terrain_width
            );
            chunks_.push_back(std::move(chunk));
        }
    }
}

void Terrain::UpdateChunkLODs(const Math::Vector3& camera_pos) {
    for (auto& chunk : chunks_) {
        chunk->UpdateLOD(camera_pos);
    }
}

// ============================================================================
// TerrainManager
// ============================================================================

TerrainManager& TerrainManager::Instance() {
    static TerrainManager instance;
    return instance;
}

bool TerrainManager::Initialize() {
    return true;
}

void TerrainManager::Shutdown() {
    terrains_.clear();
    active_terrain_ = nullptr;
}

void TerrainManager::Update(float32 delta_time) {
    // Could update streaming, etc.
}

Terrain* TerrainManager::CreateTerrain(const String& name, const TerrainConfig& config) {
    auto terrain = std::make_unique<Terrain>();
    terrain->Initialize(config);
    Terrain* ptr = terrain.get();
    terrains_[name] = std::move(terrain);
    return ptr;
}

void TerrainManager::DestroyTerrain(const String& name) {
    auto it = terrains_.find(name);
    if (it != terrains_.end()) {
        if (active_terrain_ == it->second.get()) {
            active_terrain_ = nullptr;
        }
        terrains_.erase(it);
    }
}

Terrain* TerrainManager::GetTerrain(const String& name) {
    auto it = terrains_.find(name);
    return it != terrains_.end() ? it->second.get() : nullptr;
}

void TerrainManager::SetActiveTerrain(Terrain* terrain) {
    active_terrain_ = terrain;
}

} // namespace DRX::Terrain

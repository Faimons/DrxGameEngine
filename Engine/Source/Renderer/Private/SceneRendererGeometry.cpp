/**
 * DRX Engine - Scene Renderer: CPU-built geometry (particles, debug lines, UI)
 */

#include "Renderer/Public/SceneRenderer.h"
#include "Particles/Public/ParticleSystem.h"
#include "UI/Public/UISystem.h"

#include <cmath>

namespace DRX::Renderer {

namespace {
constexpr float32 kTwoPi = 6.28318530717958647692f;
}

// ============================================================================
// Particles
// ============================================================================

void SceneRenderer::DrawParticles(const Particles::ParticleEmitter& emitter) {
    if (!frame_begun_) return;

    const Particles::ParticleData& particles = emitter.GetParticles();
    if (particles.count == 0) return;

    // Billboards face the camera: right/up from the view matrix rows
    const Math::Vector3 camera_right(camera_.view[0][0], camera_.view[1][0], camera_.view[2][0]);
    const Math::Vector3 camera_up(camera_.view[0][1], camera_.view[1][1], camera_.view[2][1]);

    const bool local_space = !emitter.simulate_in_world_space;
    const Math::Vector3 origin = emitter.GetWorldPosition();
    const Math::Quaternion orientation = emitter.GetWorldRotation();

    uint32 tiles_x = 1;
    uint32 tiles_y = 1;
    if (const auto* sheet = emitter.GetModule<Particles::TextureSheetModule>(); sheet && sheet->enabled) {
        tiles_x = std::max(1u, sheet->tiles_x);
        tiles_y = std::max(1u, sheet->tiles_y);
    }
    const float32 tile_w = 1.0f / static_cast<float32>(tiles_x);
    const float32 tile_h = 1.0f / static_cast<float32>(tiles_y);

    ParticleBatch batch;
    batch.first_vertex = static_cast<uint32>(particle_vertices_.size());
    batch.texture = emitter.texture.IsValid() ? emitter.texture.GetId() : particle_texture_.GetId();
    batch.additive = emitter.blend_mode == Particles::ParticleEmitter::BlendMode::Additive;

    particle_vertices_.reserve(particle_vertices_.size() + particles.count * 6);

    for (uint32 i = 0; i < particles.count; ++i) {
        Math::Vector3 center = particles.positions[i];
        if (local_space) {
            center = origin + orientation * center;
        }

        const float32 half = particles.sizes[i] * 0.5f;
        if (half <= 0.0f) continue;

        const float32 c = std::cos(particles.rotations[i]);
        const float32 s = std::sin(particles.rotations[i]);
        Math::Vector3 axis_x = (camera_right * c + camera_up * s) * half;
        Math::Vector3 axis_y = (camera_up * c - camera_right * s) * half;

        // Stretched billboards align with velocity
        if (emitter.render_mode == Particles::ParticleEmitter::RenderMode::StretchedBillboard) {
            Math::Vector3 velocity = particles.velocities[i];
            if (local_space) velocity = orientation * velocity;
            const float32 speed = velocity.Length();
            if (speed > 1e-4f) {
                const Math::Vector3 dir = velocity / speed;
                axis_y = dir * (half + speed * emitter.stretch_factor);
                // Perpendicular to both the motion and the view axis
                const Math::Vector3 view_axis = Math::Vector3::Cross(camera_right, camera_up);
                Math::Vector3 side = Math::Vector3::Cross(dir, view_axis);
                if (side.LengthSquared() < 1e-8f) side = camera_right;
                axis_x = side.Normalized() * half;
            }
        }

        const uint32 frame = particles.texture_indices[i] % (tiles_x * tiles_y);
        const float32 u0 = static_cast<float32>(frame % tiles_x) * tile_w;
        const float32 v0 = static_cast<float32>(frame / tiles_x) * tile_h;
        const float32 u1 = u0 + tile_w;
        const float32 v1 = v0 + tile_h;

        const Math::Vector4& color = particles.colors[i];
        const ParticleVertex corners[4] = {
            {center - axis_x - axis_y, {u0, v1}, color},
            {center + axis_x - axis_y, {u1, v1}, color},
            {center + axis_x + axis_y, {u1, v0}, color},
            {center - axis_x + axis_y, {u0, v0}, color},
        };
        particle_vertices_.push_back(corners[0]);
        particle_vertices_.push_back(corners[1]);
        particle_vertices_.push_back(corners[2]);
        particle_vertices_.push_back(corners[0]);
        particle_vertices_.push_back(corners[2]);
        particle_vertices_.push_back(corners[3]);
        ++stats_.particles;
    }

    batch.vertex_count = static_cast<uint32>(particle_vertices_.size()) - batch.first_vertex;
    if (batch.vertex_count == 0) return;

    // Merge with the previous batch when state is identical
    if (!particle_batches_.empty()) {
        ParticleBatch& last = particle_batches_.back();
        if (last.texture == batch.texture && last.additive == batch.additive &&
            last.first_vertex + last.vertex_count == batch.first_vertex) {
            last.vertex_count += batch.vertex_count;
            return;
        }
    }
    particle_batches_.push_back(batch);
}

void SceneRenderer::DrawParticles(const Particles::ParticleSystem& system) {
    for (const auto& emitter : system.GetEmitters()) {
        DrawParticles(*emitter);
    }
}

// ============================================================================
// Debug lines
// ============================================================================

void SceneRenderer::DrawLine(const Math::Vector3& a, const Math::Vector3& b, const Math::Vector4& color) {
    if (!frame_begun_) return;
    line_vertices_.push_back({a, color});
    line_vertices_.push_back({b, color});
}

void SceneRenderer::DrawWireBox(const AABB& box, const Math::Vector4& color) {
    if (!box.IsValid()) return;
    const Math::Vector3& lo = box.min;
    const Math::Vector3& hi = box.max;
    const Math::Vector3 c[8] = {
        {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
        {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
    };
    const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                              {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) {
        DrawLine(c[e[0]], c[e[1]], color);
    }
}

void SceneRenderer::DrawWireSphere(const Math::Vector3& center, float32 radius, const Math::Vector4& color,
                                   uint32 segments) {
    segments = std::max(4u, segments);
    for (uint32 i = 0; i < segments; ++i) {
        const float32 a0 = kTwoPi * static_cast<float32>(i) / static_cast<float32>(segments);
        const float32 a1 = kTwoPi * static_cast<float32>(i + 1) / static_cast<float32>(segments);
        const float32 c0 = std::cos(a0) * radius, s0 = std::sin(a0) * radius;
        const float32 c1 = std::cos(a1) * radius, s1 = std::sin(a1) * radius;
        DrawLine(center + Math::Vector3(c0, s0, 0), center + Math::Vector3(c1, s1, 0), color);
        DrawLine(center + Math::Vector3(c0, 0, s0), center + Math::Vector3(c1, 0, s1), color);
        DrawLine(center + Math::Vector3(0, c0, s0), center + Math::Vector3(0, c1, s1), color);
    }
}

void SceneRenderer::DrawGrid(const Math::Vector3& center, float32 size, float32 spacing, const Math::Vector4& color) {
    if (spacing <= 0.0f || size <= 0.0f) return;
    const auto lines = static_cast<int32>(std::floor(size / spacing));
    const float32 half = static_cast<float32>(lines) * spacing;
    for (int32 i = -lines; i <= lines; ++i) {
        const float32 offset = static_cast<float32>(i) * spacing;
        DrawLine(center + Math::Vector3(offset, 0, -half), center + Math::Vector3(offset, 0, half), color);
        DrawLine(center + Math::Vector3(-half, 0, offset), center + Math::Vector3(half, 0, offset), color);
    }
}

// ============================================================================
// UI
// ============================================================================

void SceneRenderer::DrawUI(const Array<UI::DrawCommand>& commands, float32 logical_width, float32 logical_height) {
    if (!frame_begun_ || commands.empty()) return;

    const VkExtent2D extent = device_->GetSwapChainExtent();
    const float32 scale_x = logical_width > 0.0f ? static_cast<float32>(extent.width) / logical_width : 1.0f;
    const float32 scale_y = logical_height > 0.0f ? static_cast<float32>(extent.height) / logical_height : 1.0f;
    BuildUIGeometry(commands, scale_x, scale_y);
}

void SceneRenderer::BuildUIGeometry(const Array<UI::DrawCommand>& commands, float32 scale_x, float32 scale_y) {
    const Math::Vector2 white = font_.GetWhiteUV();

    auto begin_batch = [this](uint32 texture, bool rgba) {
        if (!ui_batches_.empty()) {
            const UIBatch& last = ui_batches_.back();
            if (last.texture == texture && last.rgba == rgba) return;
        }
        UIBatch batch;
        batch.first_vertex = static_cast<uint32>(ui_vertices_.size());
        batch.texture = texture;
        batch.rgba = rgba;
        ui_batches_.push_back(batch);
    };

    auto quad = [this](float32 x0, float32 y0, float32 x1, float32 y1, float32 u0, float32 v0, float32 u1,
                       float32 v1, const Math::Vector4& color) {
        const UIVertex a{{x0, y0}, {u0, v0}, color};
        const UIVertex b{{x1, y0}, {u1, v0}, color};
        const UIVertex c{{x1, y1}, {u1, v1}, color};
        const UIVertex d{{x0, y1}, {u0, v1}, color};
        ui_vertices_.insert(ui_vertices_.end(), {a, b, c, a, c, d});
    };

    auto solid_quad = [&](float32 x0, float32 y0, float32 x1, float32 y1, const Math::Vector4& color) {
        quad(x0, y0, x1, y1, white.x, white.y, white.x, white.y, color);
    };

    // Thick line as a quad
    auto solid_line = [&](float32 x0, float32 y0, float32 x1, float32 y1, float32 thickness, const Math::Vector4& color) {
        const float32 dx = x1 - x0;
        const float32 dy = y1 - y0;
        const float32 length = std::sqrt(dx * dx + dy * dy);
        if (length < 1e-4f) return;
        const float32 nx = -dy / length * thickness * 0.5f;
        const float32 ny = dx / length * thickness * 0.5f;
        const UIVertex a{{x0 + nx, y0 + ny}, white, color};
        const UIVertex b{{x1 + nx, y1 + ny}, white, color};
        const UIVertex c{{x1 - nx, y1 - ny}, white, color};
        const UIVertex d{{x0 - nx, y0 - ny}, white, color};
        ui_vertices_.insert(ui_vertices_.end(), {a, b, c, a, c, d});
    };

    for (const auto& cmd : commands) {
        const float32 x = cmd.rect.x * scale_x;
        const float32 y = cmd.rect.y * scale_y;
        const float32 w = cmd.rect.width * scale_x;
        const float32 h = cmd.rect.height * scale_y;

        switch (cmd.type) {
            case UI::DrawCommandType::Rect:
                begin_batch(0, false);
                solid_quad(x, y, x + w, y + h, cmd.color);
                break;

            case UI::DrawCommandType::RectOutline: {
                begin_batch(0, false);
                const float32 t = std::max(1.0f, cmd.thickness * scale_x);
                solid_quad(x, y, x + w, y + t, cmd.color);
                solid_quad(x, y + h - t, x + w, y + h, cmd.color);
                solid_quad(x, y + t, x + t, y + h - t, cmd.color);
                solid_quad(x + w - t, y + t, x + w, y + h - t, cmd.color);
                break;
            }

            case UI::DrawCommandType::Text: {
                begin_batch(0, false);
                glyph_scratch_.clear();
                // Layout in logical pixels, then scale to the framebuffer
                font_.LayoutText(cmd.text, cmd.rect.x, cmd.rect.y, cmd.font_size, glyph_scratch_);
                for (const auto& g : glyph_scratch_) {
                    quad(g.x0 * scale_x, g.y0 * scale_y, g.x1 * scale_x, g.y1 * scale_y,
                         g.u0, g.v0, g.u1, g.v1, cmd.color);
                }
                break;
            }

            case UI::DrawCommandType::Image: {
                const bool has_texture = cmd.texture.IsValid();
                begin_batch(has_texture ? cmd.texture.GetId() : 0, has_texture);
                if (has_texture) {
                    quad(x, y, x + w, y + h, 0.0f, 0.0f, 1.0f, 1.0f, cmd.color);
                } else {
                    solid_quad(x, y, x + w, y + h, cmd.color);
                }
                break;
            }

            case UI::DrawCommandType::Line:
                // rect = (x0, y0, x1, y1)
                begin_batch(0, false);
                solid_line(cmd.rect.x * scale_x, cmd.rect.y * scale_y, cmd.rect.width * scale_x,
                           cmd.rect.height * scale_y, std::max(1.0f, cmd.thickness * scale_x), cmd.color);
                break;

            case UI::DrawCommandType::Circle: {
                begin_batch(0, false);
                const float32 cx = x + w * 0.5f;
                const float32 cy = y + h * 0.5f;
                const float32 rx = w * 0.5f;
                const float32 ry = h * 0.5f;
                const uint32 segments = std::clamp(static_cast<uint32>(std::max(rx, ry) * 0.75f), 12u, 64u);
                for (uint32 i = 0; i < segments; ++i) {
                    const float32 a0 = kTwoPi * static_cast<float32>(i) / static_cast<float32>(segments);
                    const float32 a1 = kTwoPi * static_cast<float32>(i + 1) / static_cast<float32>(segments);
                    const UIVertex center{{cx, cy}, white, cmd.color};
                    const UIVertex p0{{cx + std::cos(a0) * rx, cy + std::sin(a0) * ry}, white, cmd.color};
                    const UIVertex p1{{cx + std::cos(a1) * rx, cy + std::sin(a1) * ry}, white, cmd.color};
                    ui_vertices_.insert(ui_vertices_.end(), {center, p0, p1});
                }
                break;
            }
        }

        if (!ui_batches_.empty()) {
            UIBatch& last = ui_batches_.back();
            last.vertex_count = static_cast<uint32>(ui_vertices_.size()) - last.first_vertex;
        }
    }

    // Drop empty batches (e.g. text that produced no glyphs)
    ui_batches_.erase(std::remove_if(ui_batches_.begin(), ui_batches_.end(),
                                     [](const UIBatch& b) { return b.vertex_count == 0; }),
                      ui_batches_.end());
    stats_.ui_vertices = static_cast<uint32>(ui_vertices_.size());
}

} // namespace DRX::Renderer

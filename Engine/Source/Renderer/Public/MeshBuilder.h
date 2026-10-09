#pragma once

/**
 * DRX Engine - Mesh and texture builders for graybox levels and debug geometry
 */

#include "Core/Public/CoreTypes.h"
#include "Renderer/Public/RenderTypes.h"

namespace DRX::Renderer::MeshBuilder {

/**
 * Axis aligned box centred at the origin. With world_uvs, one texture repeat equals
 * `uv_scale` metres so grid textures show real dimensions.
 */
MeshData Box(const Math::Vector3& size, bool world_uvs = true, float32 uv_scale = 1.0f,
             const Math::Vector3& color = Math::Vector3::One);

// Horizontal plane (facing +Y) centred at the origin
MeshData Plane(float32 width, float32 depth, uint32 subdivisions = 1, bool world_uvs = true,
               float32 uv_scale = 1.0f);

MeshData Sphere(float32 radius, uint32 segments = 24, uint32 rings = 16);

// Y-up cylinder centred at the origin
MeshData Cylinder(float32 radius, float32 height, uint32 segments = 24);

// Y-up capsule centred at the origin; height includes the hemispheres
MeshData Capsule(float32 radius, float32 height, uint32 segments = 16, uint32 rings = 8);

/**
 * RGBA8 grid pattern: background with lines every `cells` pixels and a thicker border
 */
Array<uint8> GridTexture(uint32 size, uint32 cells, const Math::Vector4& background, const Math::Vector4& line,
                         const Math::Vector4& border);

Array<uint8> CheckerTexture(uint32 size, uint32 cells, const Math::Vector4& a, const Math::Vector4& b);

} // namespace DRX::Renderer::MeshBuilder

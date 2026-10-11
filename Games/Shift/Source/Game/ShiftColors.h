#pragma once

/**
 * SHIFT - Colours shared by the game and the editor
 */

#include "../Core/ShiftLevel.h"

#include "Core/Public/Math/Vector4.h"

namespace Shift {

inline Math::Vector4 SurfaceColor(Surface surface) {
    switch (surface) {
        case Surface::Floor: return {0.55f, 0.56f, 0.58f, 1.0f};
        case Surface::Wall: return {0.72f, 0.72f, 0.74f, 1.0f};
        case Surface::Ceiling: return {0.6f, 0.6f, 0.62f, 1.0f};
        case Surface::Cover: return {0.85f, 0.55f, 0.25f, 1.0f};
        case Surface::Step: return {0.6f, 0.75f, 0.6f, 1.0f};
        case Surface::SpawnA: return {0.2f, 0.45f, 1.0f, 1.0f};
        case Surface::SpawnB: return {1.0f, 0.3f, 0.25f, 1.0f};
        case Surface::Moving: return {0.55f, 0.6f, 0.75f, 1.0f};
    }
    return {1, 1, 1, 1};
}

// Colour chosen in the editor (w > 0) or the default colour of the surface
inline Math::Vector4 LookColor(const Math::Vector4& custom, Surface fallback) {
    return custom.w > 0.0f ? Math::Vector4(custom.x, custom.y, custom.z, 1.0f) : SurfaceColor(fallback);
}

inline Math::Vector4 TeamColor(Team team) {
    return team == Team::A ? Math::Vector4(0.25f, 0.5f, 1.0f, 1.0f) : Math::Vector4(0.95f, 0.3f, 0.25f, 1.0f);
}

} // namespace Shift

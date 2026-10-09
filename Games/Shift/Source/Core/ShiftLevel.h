#pragma once

/**
 * SHIFT - Level description
 * Graybox levels are lists of boxes. The same description feeds the physics (server and
 * client) and the renderer, so what you see is what you collide with.
 */

#include "ShiftTypes.h"
#include "ShiftWorld.h"

namespace Shift {

enum class Surface : uint8 {
    Floor,
    Wall,
    Ceiling,
    Cover,
    Step,
    SpawnA,
    SpawnB
};

struct LevelBox {
    Math::Vector3 center;
    Math::Vector3 size;      // full extents
    Surface surface = Surface::Wall;
    bool collision = true;
    bool casts_shadow = true;
};

struct Level {
    String name;
    Array<LevelBox> boxes;
    SpawnPoint spawns[2];

    void AddBox(const Math::Vector3& center, const Math::Vector3& size, Surface surface);
    // Adds the collision of every box and the spawns to the world
    void Apply(ShiftWorld& world) const;
};

/**
 * Phase 1/2 test room: 12 x 12 x 4 m closed graybox with cover, a ledge, a step row and a
 * shooting lane. Team A spawns south, the target dummy (team B) stands north.
 */
Level BuildTestRoom();

} // namespace Shift

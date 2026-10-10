#pragma once

/**
 * SHIFT - Level description
 * Graybox levels are lists of boxes. The same description feeds the physics (server and
 * client) and the renderer, so what you see is what you collide with.
 *
 * Coordinates: x west (-) to east (+), z north (-) to south (+), y up; the floor is at y = 0.
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
    SpawnB,
    Moving
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
    Array<MovingSegmentDef> segments;
    SpawnPoint spawns[2];
    ShiftMode shift_mode = ShiftMode::Patterns;
    bool has_core = false;
    Math::Vector3 core_home;
    ZoneBox exits[2];

    void AddBox(const Math::Vector3& center, const Math::Vector3& size, Surface surface);
    // Box from minimum and maximum corner
    void AddBlock(float32 x0, float32 x1, float32 z0, float32 z1, Surface surface, float32 y0 = 0.0f, float32 y1 = 4.0f);
    // Adds the collision of every box, the moving segments, spawns, exits and core to the world
    void Apply(ShiftWorld& world) const;
};

/**
 * Test room (phases 1 to 3): 12 x 12 x 4 m closed graybox with cover, a step row and a divider
 * wall whose 3 m doorway is closed and opened by a moving segment on every shift.
 */
Level BuildTestRoom();

/**
 * Arena (phase 6, blueprint section 8): start rooms A (west) and B (east), north wing (4 m wide)
 * and south wing (3 m wide) from west to east, core chamber (16 x 16 m) in the middle, machine
 * room (10 x 10 m) east of the core chamber. Moving parts for the five shift patterns.
 */
Level BuildArena();

// "arena" or "testroom"
Level BuildLevel(const String& name);

} // namespace Shift

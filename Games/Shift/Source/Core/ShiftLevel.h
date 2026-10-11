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

// Decoration model (render only), path below the Assets folder
struct LevelProp {
    String model;
    Math::Vector3 position;
    float32 yaw = 0.0f;      // degrees
    float32 scale = 1.0f;
};

struct Level {
    String name;
    Array<LevelBox> boxes;
    Array<LevelProp> props;
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

/**
 * Factory (large map): 5 x 3 halls of 14 x 14 m with cover, raised platforms, two rotating halls
 * next to the core hall and moving doors for every pattern, so each shift changes the routes.
 */
Level BuildFactory();

// Built-in maps: "arena", "fabrik" or "testroom"
bool BuildBuiltinLevel(const String& name, Level& level);
const Array<String>& BuiltinLevelNames();

/**
 * Map by name: `<map_dir>/<name>.json` when that file exists (maps from the editor), otherwise the
 * built-in map of that name, otherwise the arena.
 */
Level BuildLevel(const String& name, const String& map_dir = "");

// Map files (JSON)
String LevelToJson(const Level& level);
bool LevelFromJson(const String& json, Level& level, String* error = nullptr);
bool SaveLevel(const Level& level, const String& path, String* error = nullptr);
bool LoadLevel(const String& path, Level& level, String* error = nullptr);
// Names of the map files in a folder (without .json), sorted
Array<String> ListLevelFiles(const String& map_dir);

const char* SurfaceId(Surface surface);
const char* SegmentGroupId(SegmentGroup group);
const char* SegmentGroupName(SegmentGroup group);   // German display name

} // namespace Shift

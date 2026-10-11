/**
 * SHIFT - Level description
 */

#include "ShiftLevel.h"

#include "Core/Public/Log.h"
#include "Core/Public/Platform.h"

// Own namespace: assimp ships another rapidjson build, identical template names would clash at link time
#define RAPIDJSON_NAMESPACE shift_rapidjson
#define RAPIDJSON_NAMESPACE_BEGIN namespace shift_rapidjson {
#define RAPIDJSON_NAMESPACE_END }
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

namespace rapidjson = shift_rapidjson;

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <system_error>
#include <utility>

namespace Shift {

namespace {

constexpr float32 kHeight = 4.0f;
constexpr float32 kWall = 0.5f;

// Flat coloured plate under a start (look only)
void AddSpawnMarker(Level& level, const Math::Vector3& center, Surface surface) {
    LevelBox marker;
    marker.center = center;
    marker.size = {1.5f, 0.02f, 1.5f};
    marker.surface = surface;
    marker.collision = false;
    marker.casts_shadow = false;
    level.boxes.push_back(marker);
}

MovingSegmentDef SlidingSegment(const String& name, SegmentGroup group, const Math::Vector3& center,
                                const Math::Vector3& size, const Math::Vector3& move) {
    MovingSegmentDef def;
    def.name = name;
    def.group = group;
    def.center = center;
    def.size = size;
    def.move = move;
    return def;
}

// Door or gate that sinks into the floor when open (state 1)
MovingSegmentDef SinkingDoor(const String& name, SegmentGroup group, float32 x0, float32 x1, float32 z0, float32 z1) {
    const Math::Vector3 size(x1 - x0, kHeight, z1 - z0);
    const Math::Vector3 center((x0 + x1) * 0.5f, kHeight * 0.5f, (z0 + z1) * 0.5f);
    return SlidingSegment(name, group, center, size, {0.0f, -(kHeight + 0.1f), 0.0f});
}

// Rotating square (half 4.9, walls 0.4, 3 m openings west and south); pattern 2 turns it -90 degrees,
// so the openings face north and west. The corners sweep a circle of 6.93 m radius.
void AddRotatingRoom(Level& level, const Math::Vector3& center, const String& name) {
    using Math::Vector3;
    constexpr float32 kInner = 4.9f;
    constexpr float32 kThick = 0.4f;
    constexpr float32 kDoorHalf = 1.5f;
    const float32 face = kInner - kThick * 0.5f;   // wall centre line
    const float32 piece = (2.0f * kInner - 2.0f * kDoorHalf) * 0.5f;
    const Vector3 pivot(center.x, kHeight * 0.5f, center.z);
    auto add = [&](const char* part, const Vector3& offset, const Vector3& size) {
        MovingSegmentDef def;
        def.name = name + " " + part;
        def.group = SegmentGroup::MachineRoom;
        def.center = pivot + offset;
        def.size = size;
        def.rotate = -90.0f;
        def.pivot = pivot;
        level.segments.push_back(def);
    };
    add("N", {0.0f, 0.0f, -face}, {2.0f * kInner, kHeight, kThick});
    add("O", {face, 0.0f, 0.0f}, {kThick, kHeight, 2.0f * kInner - 2.0f * kThick});
    add("W1", {-face, 0.0f, -kDoorHalf - piece * 0.5f}, {kThick, kHeight, piece - kThick});
    add("W2", {-face, 0.0f, kDoorHalf + piece * 0.5f}, {kThick, kHeight, piece - kThick});
    add("S1", {-kDoorHalf - piece * 0.5f, 0.0f, face}, {piece, kHeight, kThick});
    add("S2", {kDoorHalf + piece * 0.5f, 0.0f, face}, {piece, kHeight, kThick});
}

} // namespace

void Level::AddBox(const Math::Vector3& center, const Math::Vector3& size, Surface surface) {
    LevelBox box;
    box.center = center;
    box.size = size;
    box.surface = surface;
    // The ceiling would put the whole level into the sun shadow
    box.casts_shadow = surface != Surface::Ceiling;
    boxes.push_back(box);
}

void Level::AddBlock(float32 x0, float32 x1, float32 z0, float32 z1, Surface surface, float32 y0, float32 y1) {
    if (x1 <= x0 || z1 <= z0 || y1 <= y0) {
        DRX_LOG_WARNING("Level '{}': empty block ignored", name);
        return;
    }
    AddBox({(x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f}, {x1 - x0, y1 - y0, z1 - z0}, surface);
}

void Level::Apply(ShiftWorld& world) const {
    for (const LevelBox& box : boxes) {
        if (box.collision) world.AddStaticBox(box.center, box.size);
    }
    world.SetShiftMode(shift_mode);
    for (const MovingSegmentDef& segment : segments) world.AddSegment(segment);
    world.SetSpawn(Team::A, spawns[0]);
    world.SetSpawn(Team::B, spawns[1]);
    world.SetExit(Team::A, exits[0]);
    world.SetExit(Team::B, exits[1]);
    if (has_core) world.SetCoreHome(core_home);
}

Level BuildTestRoom() {
    using Math::Vector3;
    constexpr float32 kSize = 12.0f;
    const float32 half = kSize * 0.5f;

    Level level;
    level.name = "Testraum";
    level.shift_mode = ShiftMode::ToggleAll;

    level.AddBox({0, -0.25f, 0}, {kSize + 2 * kWall, 0.5f, kSize + 2 * kWall}, Surface::Floor);
    level.AddBox({0, kHeight + 0.25f, 0}, {kSize + 2 * kWall, 0.5f, kSize + 2 * kWall}, Surface::Ceiling);
    level.AddBox({0, kHeight * 0.5f, -half - kWall * 0.5f}, {kSize + 2 * kWall, kHeight, kWall}, Surface::Wall);
    level.AddBox({0, kHeight * 0.5f, half + kWall * 0.5f}, {kSize + 2 * kWall, kHeight, kWall}, Surface::Wall);
    level.AddBox({-half - kWall * 0.5f, kHeight * 0.5f, 0}, {kWall, kHeight, kSize}, Surface::Wall);
    level.AddBox({half + kWall * 0.5f, kHeight * 0.5f, 0}, {kWall, kHeight, kSize}, Surface::Wall);

    // Cover blocks (waist and head height), a jumpable ledge (0.9 m) and a step row (0.25 m each)
    level.AddBox({-3.0f, 0.5f, 0.0f}, {1.5f, 1.0f, 0.6f}, Surface::Cover);
    level.AddBox({3.0f, 0.9f, -0.3f}, {0.6f, 1.8f, 1.5f}, Surface::Cover);
    level.AddBox({-4.5f, 0.45f, -4.0f}, {2.0f, 0.9f, 2.0f}, Surface::Cover);
    for (int i = 0; i < 4; ++i) {
        const float32 h = 0.25f * static_cast<float32>(i + 1);
        level.AddBox({4.6f, h * 0.5f, 2.0f + 0.4f * static_cast<float32>(i)}, {1.6f, h, 0.4f}, Surface::Step);
    }

    // Divider at z = -2 with a 3 m doorway (x -1.5 .. 1.5). The door segment runs in a track just
    // north of the divider and parks behind its east part; a stop block ends the track in the west,
    // so a player caught in the closing doorway is crushed against it.
    constexpr float32 kDividerZ = -2.0f;
    constexpr float32 kDividerThickness = 0.4f;
    constexpr float32 kDoorHalf = 1.5f;
    const float32 west_length = half - kDoorHalf;
    level.AddBox({-kDoorHalf - west_length * 0.5f, kHeight * 0.5f, kDividerZ}, {west_length, kHeight, kDividerThickness},
                 Surface::Wall);
    level.AddBox({kDoorHalf + west_length * 0.5f, kHeight * 0.5f, kDividerZ}, {west_length, kHeight, kDividerThickness},
                 Surface::Wall);

    constexpr float32 kTrackZ = kDividerZ - kDividerThickness * 0.5f - 0.25f;   // segment centre line
    constexpr float32 kSegmentDepth = 0.4f;
    constexpr float32 kSegmentWidth = 3.4f;
    level.AddBox({-kDoorHalf - 0.2f - 0.5f, kHeight * 0.5f, kTrackZ}, {1.0f, kHeight, kSegmentDepth}, Surface::Wall);

    // State 0 = closed (touches the stop), state 1 = open; the door starts open
    level.segments.push_back(SlidingSegment("Testtuer", SegmentGroup::Toggle,
                                            {-kDoorHalf - 0.2f + kSegmentWidth * 0.5f, kHeight * 0.5f, kTrackZ},
                                            {kSegmentWidth, kHeight, kSegmentDepth}, {kSegmentWidth, 0.0f, 0.0f}));

    // Spawn markers (no collision)
    AddSpawnMarker(level, {0.0f, 0.01f, 4.5f}, Surface::SpawnA);
    AddSpawnMarker(level, {0.0f, 0.01f, -4.5f}, Surface::SpawnB);

    level.spawns[0] = {Vector3(0, 0, 4.5f), 0.0f};      // looks north (-Z)
    level.spawns[1] = {Vector3(0, 0, -4.5f), 180.0f};   // looks south
    return level;
}

Level BuildArena() {
    using Math::Vector3;
    Level level;
    level.name = "Arena";
    level.shift_mode = ShiftMode::Patterns;

    // Interior extents (walls are added outside of them)
    constexpr float32 kRoomX0 = 24.0f, kRoomX1 = 36.0f;    // start rooms (mirrored for A)
    constexpr float32 kRoomZ = 6.0f;
    constexpr float32 kWingX = 28.0f;                      // wings run from -28 to 28
    constexpr float32 kNorthZ0 = -12.5f, kNorthZ1 = -8.5f; // north wing 4 m
    constexpr float32 kSouthZ0 = 8.5f, kSouthZ1 = 11.5f;   // south wing 3 m
    constexpr float32 kCore = 8.0f;                        // core chamber -8 .. 8
    constexpr float32 kDoor = 1.5f;                        // door half width
    constexpr float32 kLinkX0 = 24.0f, kLinkX1 = 28.0f;    // start room <-> wing links (mirrored)
    constexpr float32 kMachineX = 15.5f;                   // machine room centre (z = 0)
    constexpr float32 kMachineLinkX0 = 14.0f, kMachineLinkX1 = 17.0f;

    // Floor and ceiling over everything
    level.AddBlock(-37.0f, 37.0f, -13.5f, 13.5f, Surface::Floor, -0.5f, 0.0f);
    level.AddBlock(-37.0f, 37.0f, -13.5f, 13.5f, Surface::Ceiling, kHeight, kHeight + 0.5f);

    for (const float32 side : {-1.0f, 1.0f}) {
        // Mirror helper: x range on this side
        auto X = [side](float32 a, float32 b, float32& x0, float32& x1) {
            x0 = side < 0.0f ? -b : a;
            x1 = side < 0.0f ? -a : b;
        };
        float32 x0, x1;

        // --- Start room ---
        X(kRoomX0 - kWall, kRoomX1 + kWall, x0, x1);
        // North and south walls with a door to the wing link
        float32 lx0, lx1;
        X(kLinkX0, kLinkX1, lx0, lx1);
        for (const float32 z : {-kRoomZ - kWall, kRoomZ}) {
            level.AddBlock(x0, lx0, z, z + kWall, Surface::Wall);
            level.AddBlock(lx1, x1, z, z + kWall, Surface::Wall);
        }
        // Inner (towards the core) and outer walls
        X(kRoomX0 - kWall, kRoomX0, x0, x1);
        level.AddBlock(x0, x1, -kRoomZ - kWall, kRoomZ + kWall, Surface::Wall);
        X(kRoomX1, kRoomX1 + kWall, x0, x1);
        level.AddBlock(x0, x1, -kRoomZ - kWall, kRoomZ + kWall, Surface::Wall);

        // Links from the start room to both wings (2 m), side walls only
        for (const auto& [za, zb] : {std::pair<float32, float32>{kNorthZ1 + kWall, -kRoomZ - kWall},
                                     std::pair<float32, float32>{kRoomZ + kWall, kSouthZ0 - kWall}}) {
            X(kLinkX0 - kWall, kLinkX0, x0, x1);
            level.AddBlock(x0, x1, za, zb, Surface::Wall);
            X(kLinkX1, kLinkX1 + kWall, x0, x1);
            level.AddBlock(x0, x1, za, zb, Surface::Wall);
        }

        // Cover in the start room
        X(29.0f, 30.2f, x0, x1);
        level.AddBlock(x0, x1, -3.5f, -1.5f, Surface::Cover, 0.0f, 1.1f);
        level.AddBlock(x0, x1, 1.5f, 3.5f, Surface::Cover, 0.0f, 1.1f);

        // Wing end walls
        X(kWingX, kWingX + kWall, x0, x1);
        level.AddBlock(x0, x1, kNorthZ0, kNorthZ1, Surface::Wall);
        level.AddBlock(x0, x1, kSouthZ0, kSouthZ1, Surface::Wall);

        // Wing cover: crates in the long north wing, low blocks in the narrow south wing
        X(18.5f, 19.7f, x0, x1);
        level.AddBlock(x0, x1, -11.6f, -10.4f, Surface::Cover, 0.0f, 1.2f);
        X(5.0f, 6.0f, x0, x1);
        level.AddBlock(x0, x1, 10.6f, 11.5f, Surface::Cover, 0.0f, 1.0f);
        X(21.0f, 22.0f, x0, x1);
        level.AddBlock(x0, x1, 8.5f, 9.4f, Surface::Cover, 0.0f, 1.0f);

        // Gates across the wings at x = +-12 (pattern 1); state 1 = open
        X(11.8f, 12.2f, x0, x1);
        level.segments.push_back(SinkingDoor(side < 0.0f ? "Nordtor West" : "Nordtor Ost", SegmentGroup::NorthGates,
                                             x0, x1, kNorthZ0, kNorthZ1));
        level.segments.push_back(SinkingDoor(side < 0.0f ? "Suedtor West" : "Suedtor Ost", SegmentGroup::SouthGates,
                                             x0, x1, kSouthZ0, kSouthZ1));

        // Core chamber pillars
        level.AddBlock(side * 4.0f - 0.6f, side * 4.0f + 0.6f, -4.6f, -3.4f, Surface::Wall);
        level.AddBlock(side * 4.0f - 0.6f, side * 4.0f + 0.6f, 3.4f, 4.6f, Surface::Wall);
        level.AddBlock(side * 5.5f - 0.3f, side * 5.5f + 0.3f, -0.9f, 0.9f, Surface::Cover, 0.0f, 1.0f);
    }

    // --- Wing walls ---
    const float32 wing_x0 = -kWingX - kWall, wing_x1 = kWingX + kWall;
    level.AddBlock(wing_x0, wing_x1, kNorthZ0 - kWall, kNorthZ0, Surface::Wall);   // north outer
    level.AddBlock(wing_x0, wing_x1, kSouthZ1, kSouthZ1 + kWall, Surface::Wall);   // south outer
    // Inner walls (towards the core) with openings: start room links, core doors, machine room links
    struct Gap {
        float32 x0, x1;
    };
    const Gap gaps[] = {{-kLinkX1, -kLinkX0}, {-kDoor, kDoor}, {kMachineLinkX0, kMachineLinkX1}, {kLinkX0, kLinkX1}};
    for (const float32 z : {kNorthZ1, kSouthZ0 - kWall}) {
        float32 x = wing_x0;
        for (const Gap& gap : gaps) {
            level.AddBlock(x, gap.x0, z, z + kWall, Surface::Wall);
            x = gap.x1;
        }
        level.AddBlock(x, wing_x1, z, z + kWall, Surface::Wall);
    }

    // --- Core chamber: west wall solid, east wall with the machine room door ---
    level.AddBlock(-kCore - kWall, -kCore, -kCore, kCore, Surface::Wall);
    level.AddBlock(kCore, kCore + kWall, -kCore, -kDoor, Surface::Wall);
    level.AddBlock(kCore, kCore + kWall, kDoor, kCore, Surface::Wall);
    // Core doors (pattern 3): wing doors start open, the machine room door starts closed
    level.segments.push_back(SinkingDoor("Kerntuer Nord", SegmentGroup::CoreDoorsNS, -kDoor, kDoor, kNorthZ1 + 0.05f,
                                         kNorthZ1 + kWall - 0.05f));
    level.segments.push_back(SinkingDoor("Kerntuer Sued", SegmentGroup::CoreDoorsNS, -kDoor, kDoor,
                                         kSouthZ0 - kWall + 0.05f, kSouthZ0 - 0.05f));
    level.segments.push_back(SinkingDoor("Kerntuer Ost", SegmentGroup::CoreDoorE, kCore + 0.05f, kCore + kWall - 0.05f,
                                         -kDoor, kDoor));

    // --- Machine room: thick static shell, rotating inner walls (pattern 2) ---
    // The shell is thick enough to hide the corners of the rotating walls; passages lead west
    // (core), north and south (wings).
    constexpr float32 kMachineHalf = 5.0f;
    const float32 mx0 = kMachineX - kMachineHalf, mx1 = kMachineX + kMachineHalf;
    for (const float32 sign : {-1.0f, 1.0f}) {   // north and south shell around the wing passages
        const float32 za = sign < 0.0f ? kNorthZ1 + kWall : kMachineHalf;
        const float32 zb = sign < 0.0f ? -kMachineHalf : kSouthZ0 - kWall;
        level.AddBlock(kCore + kWall, kMachineLinkX0, za, zb, Surface::Wall);
        level.AddBlock(kMachineLinkX1, mx1 + 2.0f, za, zb, Surface::Wall);
    }
    level.AddBlock(kCore + kWall, mx0, -kMachineHalf, -kDoor, Surface::Wall);   // west shell
    level.AddBlock(kCore + kWall, mx0, kDoor, kMachineHalf, Surface::Wall);
    level.AddBlock(mx1, mx1 + 2.0f, -kMachineHalf, kMachineHalf, Surface::Wall);   // east shell

    AddRotatingRoom(level, Vector3(kMachineX, 0.0f, 0.0f), "Maschine");

    // --- North wing side panels (pattern 4): move 1 m in from each side; state 1 = retracted ---
    {
        constexpr float32 kThick = 0.4f;
        const float32 y = kHeight * 0.5f;
        level.segments.push_back(SlidingSegment("Engpass Nord", SegmentGroup::NorthPanels,
                                                {0.0f, y, kNorthZ0 - kWall * 0.5f + 1.0f}, {20.0f, kHeight, kThick},
                                                {0.0f, 0.0f, -1.0f}));
        for (const float32 side : {-1.0f, 1.0f}) {
            level.segments.push_back(SlidingSegment(side < 0.0f ? "Engpass Sued West" : "Engpass Sued Ost",
                                                    SegmentGroup::NorthPanels,
                                                    {side * 6.0f, y, kNorthZ1 + kWall * 0.5f - 1.0f},
                                                    {8.0f, kHeight, kThick}, {0.0f, 0.0f, 1.0f}));
        }
    }

    // Spawn markers and exit zones (3 x 3 m at the far end of each start room)
    AddSpawnMarker(level, {-30.0f, 0.01f, 0.0f}, Surface::SpawnA);
    AddSpawnMarker(level, {30.0f, 0.01f, 0.0f}, Surface::SpawnB);
    level.spawns[0] = {Vector3(-30.0f, 0.0f, 0.0f), 90.0f};    // looks east
    level.spawns[1] = {Vector3(30.0f, 0.0f, 0.0f), 270.0f};    // looks west
    level.exits[0] = {Vector3(-34.0f, 1.0f, 0.0f), Vector3(3.0f, 2.0f, 3.0f)};
    level.exits[1] = {Vector3(34.0f, 1.0f, 0.0f), Vector3(3.0f, 2.0f, 3.0f)};
    level.has_core = true;
    level.core_home = Vector3(0.0f, 0.0f, 0.0f);
    return level;
}

Level BuildFactory() {
    using Math::Vector3;
    Level level;
    level.name = "Fabrik";
    level.shift_mode = ShiftMode::Patterns;

    // 5 x 3 halls of 14 x 14 m; hall (column c, row r) spans x = -35 + 14c .. , z = -21 + 14r ..
    constexpr int kCols = 5;
    constexpr int kRows = 3;
    constexpr float32 kCell = 14.0f;
    constexpr float32 kX0 = -35.0f;
    constexpr float32 kZ0 = -21.0f;
    constexpr float32 kDoorHalf = 1.5f;
    const float32 half_wall = kWall * 0.5f;
    auto line_x = [&](int i) { return kX0 + kCell * static_cast<float32>(i); };
    auto line_z = [&](int j) { return kZ0 + kCell * static_cast<float32>(j); };

    level.AddBlock(line_x(0) - 1.0f, line_x(kCols) + 1.0f, line_z(0) - 1.0f, line_z(kRows) + 1.0f, Surface::Floor, -0.5f, 0.0f);
    level.AddBlock(line_x(0) - 1.0f, line_x(kCols) + 1.0f, line_z(0) - 1.0f, line_z(kRows) + 1.0f, Surface::Ceiling,
                   kHeight, kHeight + 0.5f);

    // Links between neighbouring halls: open doorway, or a door that moves with a shift pattern.
    // Start halls (0,1) and (4,1) always reach the core hall (2,1): along the middle row when the
    // core side doors are open, otherwise through the open wing row and the core's north/south doors.
    enum class Link { Open, Door };
    struct LinkInfo {
        Link link = Link::Open;
        SegmentGroup group = SegmentGroup::Toggle;
    };
    auto east_link = [](int c, int r) -> LinkInfo {   // between hall (c, r) and (c + 1, r)
        if (r == 1) return (c == 1 || c == 2) ? LinkInfo{Link::Door, SegmentGroup::CoreDoorE} : LinkInfo{};
        if (c == 1 || c == 2) return {Link::Door, r == 0 ? SegmentGroup::NorthGates : SegmentGroup::SouthGates};
        return {};
    };
    auto south_link = [](int c, int /*r*/) -> LinkInfo {   // between hall (c, r) and (c, r + 1)
        if (c == 2) return {Link::Door, SegmentGroup::CoreDoorsNS};
        if (c == 1 || c == 3) return {Link::Door, SegmentGroup::NorthPanels};
        return {};
    };
    auto door_name = [](SegmentGroup group, int c, int r) {
        return fmt::format("Tor {}-{}-{}", static_cast<int>(group), c, r);
    };

    // Walls along x = const (west/east sides of halls)
    for (int i = 0; i <= kCols; ++i) {
        const float32 x = line_x(i);
        for (int r = 0; r < kRows; ++r) {
            const float32 z0 = line_z(r) - half_wall, z1 = line_z(r + 1) + half_wall;
            const float32 mid = (line_z(r) + line_z(r + 1)) * 0.5f;
            if (i == 0 || i == kCols) {
                level.AddBlock(x - half_wall, x + half_wall, z0, z1, Surface::Wall);
                continue;
            }
            level.AddBlock(x - half_wall, x + half_wall, z0, mid - kDoorHalf, Surface::Wall);
            level.AddBlock(x - half_wall, x + half_wall, mid + kDoorHalf, z1, Surface::Wall);
            const LinkInfo info = east_link(i - 1, r);
            if (info.link == Link::Door) {
                level.segments.push_back(SinkingDoor(door_name(info.group, i, r), info.group, x - 0.2f, x + 0.2f,
                                                     mid - kDoorHalf, mid + kDoorHalf));
            }
        }
    }
    // Walls along z = const (north/south sides of halls)
    for (int j = 0; j <= kRows; ++j) {
        const float32 z = line_z(j);
        for (int c = 0; c < kCols; ++c) {
            const float32 x0 = line_x(c), x1 = line_x(c + 1);
            const float32 mid = (x0 + x1) * 0.5f;
            if (j == 0 || j == kRows) {
                level.AddBlock(x0, x1, z - half_wall, z + half_wall, Surface::Wall);
                continue;
            }
            level.AddBlock(x0, mid - kDoorHalf, z - half_wall, z + half_wall, Surface::Wall);
            level.AddBlock(mid + kDoorHalf, x1, z - half_wall, z + half_wall, Surface::Wall);
            const LinkInfo info = south_link(c, j - 1);
            if (info.link == Link::Door) {
                level.segments.push_back(SinkingDoor(door_name(info.group, c, j), info.group, mid - kDoorHalf,
                                                     mid + kDoorHalf, z - 0.2f, z + 0.2f));
            }
        }
    }

    // Hall contents: rotating rooms north and south of the core hall, cover everywhere else.
    // Cover sits in the four quarters (2.5 .. 5.5 m from the centre) so the doorway lanes stay free.
    for (int c = 0; c < kCols; ++c) {
        for (int r = 0; r < kRows; ++r) {
            const float32 cx = (line_x(c) + line_x(c + 1)) * 0.5f;
            const float32 cz = (line_z(r) + line_z(r + 1)) * 0.5f;
            if (c == 2 && r != 1) {
                AddRotatingRoom(level, Vector3(cx, 0.0f, cz), r == 0 ? "Drehhalle Nord" : "Drehhalle Sued");
                continue;
            }
            const bool platform_hall = (c == 1 && r == 0) || (c == 3 && r == 2);
            for (int q = 0; q < 4; ++q) {
                const float32 sx = (q & 1) ? 1.0f : -1.0f;
                const float32 sz = (q & 2) ? 1.0f : -1.0f;
                const float32 qx = cx + sx * 4.0f, qz = cz + sz * 4.0f;
                if (platform_hall && q == 3) {
                    // Raised platform (1 m) with three steps on its centre side
                    level.AddBlock(qx - 1.0f, qx + 1.5f, qz - 1.5f, qz + 1.5f, Surface::Cover, 0.0f, 1.0f);
                    for (int step = 0; step < 3; ++step) {
                        const float32 x = qx - 1.0f - 0.4f * static_cast<float32>(step + 1);
                        level.AddBlock(x, x + 0.4f, qz - 1.5f, qz + 1.5f, Surface::Step, 0.0f,
                                       0.25f * static_cast<float32>(3 - step));
                    }
                    continue;
                }
                const uint32 kind = (c == 2 && r == 1) ? 2u : HashU32(static_cast<uint32>(c), static_cast<uint32>(r),
                                                                      static_cast<uint32>(q) + 17u) % 5u;
                switch (kind) {
                    case 1: level.AddBlock(qx - 0.6f, qx + 0.6f, qz - 0.6f, qz + 0.6f, Surface::Cover, 0.0f, 1.2f); break;
                    case 2: level.AddBlock(qx - 0.5f, qx + 0.5f, qz - 0.5f, qz + 0.5f, Surface::Wall); break;
                    case 3: level.AddBlock(qx - 1.5f, qx + 1.5f, qz - 0.25f, qz + 0.25f, Surface::Cover, 0.0f, 1.1f); break;
                    case 4: level.AddBlock(qx - 0.25f, qx + 0.25f, qz - 1.5f, qz + 1.5f, Surface::Cover, 0.0f, 1.1f); break;
                    default: break;   // open quarter
                }
            }
        }
    }

    // Start halls west and east of the middle row; exits at their outer walls; core in the middle
    AddSpawnMarker(level, {-26.0f, 0.01f, 0.0f}, Surface::SpawnA);
    AddSpawnMarker(level, {26.0f, 0.01f, 0.0f}, Surface::SpawnB);
    level.spawns[0] = {Vector3(-26.0f, 0.0f, 0.0f), 90.0f};
    level.spawns[1] = {Vector3(26.0f, 0.0f, 0.0f), 270.0f};
    level.exits[0] = {Vector3(-32.5f, 1.0f, 0.0f), Vector3(3.0f, 2.0f, 3.0f)};
    level.exits[1] = {Vector3(32.5f, 1.0f, 0.0f), Vector3(3.0f, 2.0f, 3.0f)};
    level.has_core = true;
    level.core_home = Vector3(0.0f, 0.0f, 0.0f);
    return level;
}

namespace {

// Wall along X (constant z) or along Z (constant x) from `from` to `to`, leaving the given gaps open
void WallWithGaps(Level& level, bool along_x, float32 fixed, float32 from, float32 to,
                  std::initializer_list<std::pair<float32, float32>> gaps) {
    float32 start = from;
    const auto piece = [&](float32 a, float32 b) {
        if (b - a < 0.05f) return;
        if (along_x) level.AddBlock(a, b, fixed - kWall * 0.5f, fixed + kWall * 0.5f, Surface::Wall);
        else level.AddBlock(fixed - kWall * 0.5f, fixed + kWall * 0.5f, a, b, Surface::Wall);
    };
    for (const auto& gap : gaps) {
        piece(start, gap.first);
        start = gap.second;
    }
    piece(start, to);
}

} // namespace

Level BuildHelix() {
    using Math::Vector3;
    constexpr float32 kHalf = 30.0f;
    Level level;
    level.name = "Helix";
    level.shift_mode = ShiftMode::Patterns;

    // Shell: floor, ceiling, outer walls
    level.AddBlock(-kHalf - kWall, kHalf + kWall, -kHalf - kWall, kHalf + kWall, Surface::Floor, -0.5f, 0.0f);
    level.AddBlock(-kHalf - kWall, kHalf + kWall, -kHalf - kWall, kHalf + kWall, Surface::Ceiling, kHeight, kHeight + 0.5f);
    WallWithGaps(level, true, -kHalf - kWall * 0.5f, -kHalf - kWall, kHalf + kWall, {});
    WallWithGaps(level, true, kHalf + kWall * 0.5f, -kHalf - kWall, kHalf + kWall, {});
    WallWithGaps(level, false, -kHalf - kWall * 0.5f, -kHalf, kHalf, {});
    WallWithGaps(level, false, kHalf + kWall * 0.5f, -kHalf, kHalf, {});

    for (const float32 side : {-1.0f, 1.0f}) {
        const auto x = [side](float32 v) { return v * side; };
        const auto span = [side](float32 a, float32 b) { return side < 0.0f ? std::make_pair(-b, -a) : std::make_pair(a, b); };
        const char* west = side < 0.0f ? "West" : "Ost";

        // Start room (north corner, 12 x 10 m): open to the south, north gate into the north lab
        const auto start_south = span(22.0f, 26.0f);
        WallWithGaps(level, true, -20.0f, side < 0.0f ? -kHalf : 18.0f, side < 0.0f ? -18.0f : kHalf, {start_south});
        WallWithGaps(level, false, x(18.0f), -kHalf, -20.0f, {{-27.5f, -24.5f}});
        level.segments.push_back(SinkingDoor(fmt::format("Nordtor {}", west), SegmentGroup::NorthGates,
                                             x(18.0f) - kWall * 0.5f, x(18.0f) + kWall * 0.5f, -27.5f, -24.5f));
        level.AddBlock(span(20.0f, 21.5f).first, span(20.0f, 21.5f).second, -23.0f, -22.0f, Surface::Cover, 0.0f, 1.0f);

        // North lab half: south wall with a door to the hall
        const auto lab_door = span(9.0f, 12.0f);
        WallWithGaps(level, true, -20.0f, side < 0.0f ? -18.0f : 0.0f, side < 0.0f ? 0.0f : 18.0f, {lab_door});
        // Lab benches and a cryo tank
        level.AddBlock(span(3.0f, 7.0f).first, span(3.0f, 7.0f).second, -27.0f, -26.0f, Surface::Cover, 0.0f, 1.0f);
        level.AddBlock(span(12.0f, 15.0f).first, span(12.0f, 15.0f).second, -26.5f, -25.5f, Surface::Cover, 0.0f, 1.0f);
        level.AddBlock(span(7.5f, 9.5f).first, span(7.5f, 9.5f).second, -24.0f, -22.0f, Surface::Cover, 0.0f, 2.2f);

        // Side lab (12 x 20 m): doors north and south, lab gate to the hall (closes in "Engpass")
        const auto lab_ns = span(22.0f, 26.0f);
        WallWithGaps(level, true, -12.0f, side < 0.0f ? -kHalf : 18.0f, side < 0.0f ? -18.0f : kHalf, {lab_ns});
        WallWithGaps(level, true, 8.0f, side < 0.0f ? -kHalf : 18.0f, side < 0.0f ? -18.0f : kHalf, {lab_ns});
        WallWithGaps(level, false, x(18.0f), -12.0f, 8.0f, {{-4.4f, -1.6f}});
        // Lab gate: closed in state 0, slides north into the wall in state 1 (open at round start)
        level.segments.push_back(SlidingSegment(fmt::format("Labortor {}", west), SegmentGroup::NorthPanels,
                                                Vector3(x(18.0f), kHeight * 0.5f, -3.0f), Vector3(kWall, kHeight, 2.8f),
                                                Vector3(0.0f, 0.0f, -2.9f)));
        for (const float32 tz : {-7.0f, 3.0f}) {
            level.AddBlock(span(21.0f, 23.0f).first, span(21.0f, 23.0f).second, tz - 1.0f, tz + 1.0f, Surface::Cover, 0.0f, 2.2f);
            level.AddBlock(span(25.0f, 27.0f).first, span(25.0f, 27.0f).second, tz - 1.0f, tz + 1.0f, Surface::Cover, 0.0f, 2.2f);
        }

        // Hall pillars and cover between labs and core
        level.AddBlock(span(12.0f, 13.5f).first, span(12.0f, 13.5f).second, -16.5f, -15.0f, Surface::Wall);
        level.AddBlock(span(4.0f, 5.5f).first, span(4.0f, 5.5f).second, -16.5f, -15.0f, Surface::Cover, 0.0f, 1.0f);
        level.AddBlock(span(12.0f, 13.0f).first, span(12.0f, 13.0f).second, -6.0f, -3.0f, Surface::Cover, 0.0f, 1.0f);
        level.AddBlock(span(12.0f, 13.0f).first, span(12.0f, 13.0f).second, 1.0f, 4.0f, Surface::Cover, 0.0f, 1.8f);

        // Core chamber west / east wall with the side door
        WallWithGaps(level, false, x(8.0f), -8.0f, 8.0f, {{-1.5f, 1.5f}});
        level.segments.push_back(SinkingDoor(fmt::format("Kerntuer {}", west), SegmentGroup::CoreDoorE,
                                             x(8.0f) - kWall * 0.5f, x(8.0f) + kWall * 0.5f, -1.5f, 1.5f));
        level.AddBlock(span(4.5f, 5.5f).first, span(4.5f, 5.5f).second, -5.5f, -4.5f, Surface::Cover, 0.0f, 1.0f);
        level.AddBlock(span(4.5f, 5.5f).first, span(4.5f, 5.5f).second, 4.5f, 5.5f, Surface::Cover, 0.0f, 1.0f);

        // Exit room (south corner) with the south gate to the yard
        WallWithGaps(level, true, 12.0f, side < 0.0f ? -kHalf : 18.0f, side < 0.0f ? -18.0f : kHalf, {span(22.0f, 26.0f)});
        WallWithGaps(level, false, x(18.0f), 12.0f, kHalf, {{19.0f, 22.0f}});
        level.segments.push_back(SinkingDoor(fmt::format("Suedtor {}", west), SegmentGroup::SouthGates,
                                             x(18.0f) - kWall * 0.5f, x(18.0f) + kWall * 0.5f, 19.0f, 22.0f));
        level.AddBlock(span(20.0f, 21.0f).first, span(20.0f, 21.0f).second, 16.0f, 18.0f, Surface::Cover, 0.0f, 1.0f);

        // South yard cover (outside the rotating room's sweep)
        level.AddBlock(span(11.0f, 13.0f).first, span(11.0f, 13.0f).second, 16.0f, 17.0f, Surface::Cover, 0.0f, 1.0f);
        level.AddBlock(span(10.0f, 11.0f).first, span(10.0f, 11.0f).second, 23.0f, 25.0f, Surface::Cover, 0.0f, 1.8f);
    }

    // Core chamber north and south walls with the main doors (open at round start)
    for (const float32 z : {-8.0f, 8.0f}) {
        WallWithGaps(level, true, z, -8.0f - kWall * 0.5f, 8.0f + kWall * 0.5f, {{-1.5f, 1.5f}});
        level.segments.push_back(SinkingDoor(z < 0.0f ? "Kerntuer Nord" : "Kerntuer Sued", SegmentGroup::CoreDoorsNS, -1.5f, 1.5f,
                                             z - kWall * 0.5f, z + kWall * 0.5f));
    }
    // North lab centre: cryo tank
    level.AddBlock(-1.0f, 1.0f, -27.0f, -25.0f, Surface::Cover, 0.0f, 2.5f);
    // Rotating room in the south (pattern "Drehraum")
    AddRotatingRoom(level, Vector3(0.0f, 0.0f, 20.5f), "Drehraum");

    level.spawns[0] = {Vector3(-24.0f, 0.0f, -25.0f), 180.0f};   // looks south
    level.spawns[1] = {Vector3(24.0f, 0.0f, -25.0f), 180.0f};
    level.exits[0] = {Vector3(-24.0f, 1.0f, 24.0f), Vector3(3.0f, 2.0f, 3.0f)};
    level.exits[1] = {Vector3(24.0f, 1.0f, 24.0f), Vector3(3.0f, 2.0f, 3.0f)};
    level.has_core = true;
    level.core_home = Vector3(0.0f, 0.0f, 0.0f);
    return level;
}

bool BuildBuiltinLevel(const String& name, Level& level) {
    if (name == "helix") level = BuildHelix();
    else if (name == "testroom") level = BuildTestRoom();
    else if (name == "fabrik") level = BuildFactory();
    else if (name == "arena") level = BuildArena();
    else return false;
    return true;
}

const Array<String>& BuiltinLevelNames() {
    static const Array<String> names = {"arena", "fabrik", "helix", "testroom"};
    return names;
}

Level BuildLevel(const String& name, const String& map_dir) {
    Level level;
    if (!map_dir.empty()) {
        const std::filesystem::path file = std::filesystem::path(map_dir) / (name + ".json");
        std::error_code ec;
        if (std::filesystem::exists(file, ec)) {
            String error;
            if (LoadLevel(file.string(), level, &error)) return level;
            DRX_LOG_ERROR("Map '{}': {}", file.string(), error);
        }
    }
    if (BuildBuiltinLevel(name, level)) return level;
    DRX_LOG_WARNING("Unknown map '{}', using the arena", name);
    return BuildArena();
}

// ============================================================================
// Map files
// ============================================================================

namespace {

struct NamedSurface {
    Surface surface;
    const char* id;
};
constexpr NamedSurface kSurfaces[] = {
    {Surface::Floor, "floor"},   {Surface::Wall, "wall"},       {Surface::Ceiling, "ceiling"},
    {Surface::Cover, "cover"},   {Surface::Step, "step"},       {Surface::SpawnA, "spawn_a"},
    {Surface::SpawnB, "spawn_b"}, {Surface::Moving, "moving"},
};

struct NamedGroup {
    SegmentGroup group;
    const char* id;
    const char* name;
};
constexpr NamedGroup kGroups[] = {
    {SegmentGroup::Toggle, "toggle", "Wechsel (jede Verschiebung)"},
    {SegmentGroup::NorthGates, "north_gates", "Nordtore"},
    {SegmentGroup::SouthGates, "south_gates", "Suedtore"},
    {SegmentGroup::MachineRoom, "machine_room", "Drehraum"},
    {SegmentGroup::CoreDoorsNS, "core_doors_ns", "Kerntueren Nord/Sued"},
    {SegmentGroup::CoreDoorE, "core_door_e", "Kerntuer Ost"},
    {SegmentGroup::NorthPanels, "north_panels", "Seitenwaende"},
};

using JsonWriter = rapidjson::PrettyWriter<rapidjson::StringBuffer>;

void WriteVector(JsonWriter& w, const char* key, const Math::Vector3& v) {
    w.Key(key);
    w.SetFormatOptions(rapidjson::kFormatSingleLineArray);
    w.StartArray();
    w.Double(static_cast<double>(v.x));
    w.Double(static_cast<double>(v.y));
    w.Double(static_cast<double>(v.z));
    w.EndArray();
    w.SetFormatOptions(rapidjson::kFormatDefault);
}

void WriteLook(JsonWriter& w, const Math::Vector4& color, float32 glow) {
    if (color.w > 0.0f) WriteVector(w, "color", Math::Vector3(color.x, color.y, color.z));
    if (glow > 0.0f) {
        w.Key("glow");
        w.Double(static_cast<double>(glow));
    }
}

bool ReadVector(const rapidjson::Value& object, const char* key, Math::Vector3& out) {
    const auto it = object.FindMember(key);
    if (it == object.MemberEnd() || !it->value.IsArray() || it->value.Size() != 3) return false;
    for (rapidjson::SizeType i = 0; i < 3; ++i) {
        if (!it->value[i].IsNumber()) return false;
    }
    out = Math::Vector3(it->value[0].GetFloat(), it->value[1].GetFloat(), it->value[2].GetFloat());
    return true;
}

float32 ReadFloat(const rapidjson::Value& object, const char* key, float32 fallback) {
    const auto it = object.FindMember(key);
    return it != object.MemberEnd() && it->value.IsNumber() ? it->value.GetFloat() : fallback;
}

bool ReadBool(const rapidjson::Value& object, const char* key, bool fallback) {
    const auto it = object.FindMember(key);
    return it != object.MemberEnd() && it->value.IsBool() ? it->value.GetBool() : fallback;
}

String ReadString(const rapidjson::Value& object, const char* key, const String& fallback) {
    const auto it = object.FindMember(key);
    return it != object.MemberEnd() && it->value.IsString() ? String(it->value.GetString()) : fallback;
}

void ReadLook(const rapidjson::Value& object, Math::Vector4& color, float32& glow) {
    Math::Vector3 rgb;
    if (ReadVector(object, "color", rgb)) {
        color = Math::Vector4(std::clamp(rgb.x, 0.0f, 1.0f), std::clamp(rgb.y, 0.0f, 1.0f), std::clamp(rgb.z, 0.0f, 1.0f), 1.0f);
    }
    glow = std::clamp(ReadFloat(object, "glow", 0.0f), 0.0f, 20.0f);
}

const rapidjson::Value* ReadArray(const rapidjson::Value& object, const char* key) {
    const auto it = object.FindMember(key);
    return it != object.MemberEnd() && it->value.IsArray() ? &it->value : nullptr;
}

bool Fail(String* error, const String& message) {
    if (error) *error = message;
    return false;
}

} // namespace

const char* SurfaceId(Surface surface) {
    for (const NamedSurface& s : kSurfaces) {
        if (s.surface == surface) return s.id;
    }
    return "wall";
}

const char* SegmentGroupId(SegmentGroup group) {
    for (const NamedGroup& g : kGroups) {
        if (g.group == group) return g.id;
    }
    return "toggle";
}

const char* SegmentGroupName(SegmentGroup group) {
    for (const NamedGroup& g : kGroups) {
        if (g.group == group) return g.name;
    }
    return "?";
}

String LevelToJson(const Level& level) {
    rapidjson::StringBuffer buffer;
    JsonWriter w(buffer);
    w.SetIndent(' ', 2);
    w.StartObject();
    w.Key("format");
    w.Int(1);
    w.Key("name");
    w.String(level.name.c_str());
    w.Key("shift_mode");
    w.String(level.shift_mode == ShiftMode::ToggleAll ? "toggle" : "patterns");

    w.Key("spawns");
    w.StartArray();
    for (const SpawnPoint& spawn : level.spawns) {
        w.StartObject();
        WriteVector(w, "position", spawn.position);
        w.Key("yaw");
        w.Double(static_cast<double>(spawn.yaw));
        w.EndObject();
    }
    w.EndArray();

    w.Key("core");
    if (level.has_core) {
        w.StartObject();
        WriteVector(w, "position", level.core_home);
        w.EndObject();
    } else {
        w.Null();
    }
    w.Key("exits");
    w.StartArray();
    for (const ZoneBox& exit : level.exits) {
        w.StartObject();
        WriteVector(w, "center", exit.center);
        WriteVector(w, "size", exit.size);
        w.EndObject();
    }
    w.EndArray();

    w.Key("boxes");
    w.StartArray();
    for (const LevelBox& box : level.boxes) {
        w.StartObject();
        WriteVector(w, "center", box.center);
        WriteVector(w, "size", box.size);
        w.Key("surface");
        w.String(SurfaceId(box.surface));
        if (!box.collision) {
            w.Key("collision");
            w.Bool(false);
        }
        if (!box.casts_shadow) {
            w.Key("shadow");
            w.Bool(false);
        }
        WriteLook(w, box.color, box.glow);
        w.EndObject();
    }
    w.EndArray();

    w.Key("segments");
    w.StartArray();
    for (const MovingSegmentDef& segment : level.segments) {
        w.StartObject();
        w.Key("name");
        w.String(segment.name.c_str());
        w.Key("group");
        w.String(SegmentGroupId(segment.group));
        WriteVector(w, "center", segment.center);
        WriteVector(w, "size", segment.size);
        WriteVector(w, "move", segment.move);
        w.Key("rotate");
        w.Double(static_cast<double>(segment.rotate));
        WriteVector(w, "pivot", segment.pivot);
        WriteLook(w, segment.color, segment.glow);
        w.EndObject();
    }
    w.EndArray();

    w.Key("props");
    w.StartArray();
    for (const LevelProp& prop : level.props) {
        w.StartObject();
        w.Key("model");
        w.String(prop.model.c_str());
        WriteVector(w, "position", prop.position);
        w.Key("yaw");
        w.Double(static_cast<double>(prop.yaw));
        w.Key("scale");
        w.Double(static_cast<double>(prop.scale));
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    return String(buffer.GetString(), buffer.GetSize());
}

bool LevelFromJson(const String& json, Level& out, String* error) {
    rapidjson::Document doc;
    doc.Parse(json.c_str(), json.size());
    if (doc.HasParseError()) {
        return Fail(error, fmt::format("JSON-Fehler bei Zeichen {}: {}", doc.GetErrorOffset(),
                                       rapidjson::GetParseError_En(doc.GetParseError())));
    }
    if (!doc.IsObject()) return Fail(error, "Kartendatei ist kein JSON-Objekt");

    Level level;
    level.name = ReadString(doc, "name", "Karte");
    level.shift_mode = ReadString(doc, "shift_mode", "patterns") == "toggle" ? ShiftMode::ToggleAll : ShiftMode::Patterns;

    if (const rapidjson::Value* spawns = ReadArray(doc, "spawns")) {
        for (rapidjson::SizeType i = 0; i < spawns->Size() && i < 2; ++i) {
            const rapidjson::Value& s = (*spawns)[i];
            if (!s.IsObject() || !ReadVector(s, "position", level.spawns[i].position)) {
                return Fail(error, fmt::format("Start {} ohne Position", i + 1));
            }
            level.spawns[i].yaw = ReadFloat(s, "yaw", 0.0f);
        }
    }
    const auto core = doc.FindMember("core");
    if (core != doc.MemberEnd() && core->value.IsObject()) {
        if (!ReadVector(core->value, "position", level.core_home)) return Fail(error, "Kern ohne Position");
        level.has_core = true;
    }
    if (const rapidjson::Value* exits = ReadArray(doc, "exits")) {
        for (rapidjson::SizeType i = 0; i < exits->Size() && i < 2; ++i) {
            const rapidjson::Value& e = (*exits)[i];
            if (!e.IsObject() || !ReadVector(e, "center", level.exits[i].center) || !ReadVector(e, "size", level.exits[i].size)) {
                return Fail(error, fmt::format("Ausgang {} unvollstaendig", i + 1));
            }
        }
    }
    if (const rapidjson::Value* boxes = ReadArray(doc, "boxes")) {
        for (rapidjson::SizeType i = 0; i < boxes->Size(); ++i) {
            const rapidjson::Value& b = (*boxes)[i];
            LevelBox box;
            if (!b.IsObject() || !ReadVector(b, "center", box.center) || !ReadVector(b, "size", box.size)) {
                return Fail(error, fmt::format("Block {} unvollstaendig", i + 1));
            }
            if (box.size.x <= 0.0f || box.size.y <= 0.0f || box.size.z <= 0.0f) {
                return Fail(error, fmt::format("Block {} hat keine Groesse", i + 1));
            }
            const String surface = ReadString(b, "surface", "wall");
            for (const NamedSurface& s : kSurfaces) {
                if (surface == s.id) box.surface = s.surface;
            }
            box.collision = ReadBool(b, "collision", true);
            box.casts_shadow = ReadBool(b, "shadow", box.surface != Surface::Ceiling);
            ReadLook(b, box.color, box.glow);
            level.boxes.push_back(box);
        }
    }
    if (const rapidjson::Value* segments = ReadArray(doc, "segments")) {
        for (rapidjson::SizeType i = 0; i < segments->Size(); ++i) {
            const rapidjson::Value& s = (*segments)[i];
            MovingSegmentDef def;
            if (!s.IsObject() || !ReadVector(s, "center", def.center) || !ReadVector(s, "size", def.size)) {
                return Fail(error, fmt::format("Bewegliche Wand {} unvollstaendig", i + 1));
            }
            def.name = ReadString(s, "name", fmt::format("Wand {}", i + 1));
            const String group = ReadString(s, "group", "toggle");
            for (const NamedGroup& g : kGroups) {
                if (group == g.id) def.group = g.group;
            }
            ReadVector(s, "move", def.move);
            def.rotate = ReadFloat(s, "rotate", 0.0f);
            ReadVector(s, "pivot", def.pivot);
            ReadLook(s, def.color, def.glow);
            level.segments.push_back(def);
        }
    }
    if (const rapidjson::Value* props = ReadArray(doc, "props")) {
        for (rapidjson::SizeType i = 0; i < props->Size(); ++i) {
            const rapidjson::Value& p = (*props)[i];
            LevelProp prop;
            prop.model = p.IsObject() ? ReadString(p, "model", "") : "";
            if (prop.model.empty() || !ReadVector(p, "position", prop.position)) {
                return Fail(error, fmt::format("Deko {} unvollstaendig", i + 1));
            }
            prop.yaw = ReadFloat(p, "yaw", 0.0f);
            prop.scale = std::max(0.0001f, ReadFloat(p, "scale", 1.0f));
            level.props.push_back(prop);
        }
    }
    out = std::move(level);
    return true;
}

bool SaveLevel(const Level& level, const String& path, String* error) {
    std::error_code ec;
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    // Write to a temporary file first so a crash never leaves a half written map
    const String temp = path + ".tmp";
    if (!Platform::FileSystem::WriteTextFile(temp, LevelToJson(level))) return Fail(error, fmt::format("'{}' nicht schreibbar", path));
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(temp, path, ec);
        if (ec) return Fail(error, fmt::format("'{}' nicht schreibbar: {}", path, ec.message()));
    }
    return true;
}

bool LoadLevel(const String& path, Level& level, String* error) {
    const Result<String> text = Platform::FileSystem::ReadTextFile(path);
    if (!text) return Fail(error, fmt::format("'{}' nicht lesbar", path));
    return LevelFromJson(text.Value(), level, error);
}

Array<String> ListLevelFiles(const String& map_dir) {
    Array<String> names;
    std::error_code ec;
    if (map_dir.empty() || !std::filesystem::is_directory(map_dir, ec)) return names;
    for (const auto& entry : std::filesystem::directory_iterator(map_dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".json") names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace Shift

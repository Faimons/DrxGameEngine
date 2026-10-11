#pragma once

/**
 * SHIFT - Computer opponents (authority only)
 * Bots produce the same PlayerInput a human sends, so every rule (weapons, core, walls, rounds)
 * applies to them unchanged. Navigation runs on a walkability grid of the level that is refreshed
 * around the moving segments while they move.
 */

#include "ShiftLevel.h"
#include "ShiftWorld.h"

namespace Shift {

class NavGrid {
public:
    // Samples the whole level; `cell` in metres
    void Build(ShiftWorld& world, const Level& level, float32 cell = 0.5f);
    // Re-samples the cells the moving segments can reach when a segment moved since the last call
    void Refresh(ShiftWorld& world);

    // Path from `from` to `to` as world points (feet height); false when unreachable
    bool FindPath(const Math::Vector3& from, const Math::Vector3& to, Array<Math::Vector3>& path) const;
    // True when a character can walk the straight line between both points
    bool IsLineWalkable(const Math::Vector3& from, const Math::Vector3& to) const;
    bool IsWalkable(const Math::Vector3& point) const;
    bool IsValid() const { return width_ > 0 && depth_ > 0; }

private:
    int32 Index(int32 x, int32 z) const { return z * width_ + x; }
    bool CellOf(const Math::Vector3& point, int32& x, int32& z) const;
    Math::Vector3 CellCenter(int32 x, int32 z) const;
    void SampleCell(ShiftWorld& world, int32 index);
    int32 NearestWalkable(int32 x, int32 z, int32 radius) const;

    float32 cell_ = 0.5f;
    float32 radius_ = 0.3f;
    float32 height_ = 1.8f;
    Math::Vector3 origin_;
    int32 width_ = 0;
    int32 depth_ = 0;
    Array<float32> ground_;
    Array<uint8> walkable_;
    Array<int32> dynamic_cells_;
    Array<float32> segment_amounts_;
};

/**
 * Map check for the editor and the tests: in every wall state the plan can produce, both teams must
 * reach the core and the core must reach both exits (without core: the starts must reach each other).
 * Returns the problems found (German, for the editor), empty when the map is fine.
 */
Array<String> CheckLevelRoutes(const ShiftConfig& config, const Level& level);

class BotDirector {
public:
    void Build(ShiftWorld& world, const Level& level);
    PlayerId AddBot(ShiftWorld& world, Team team);
    void Clear() { bots_.clear(); }
    // Call before every ShiftWorld::Step
    void Update(ShiftWorld& world);
    const NavGrid& GetGrid() const { return grid_; }

private:
    struct Bot {
        PlayerId id = kInvalidPlayer;
        Array<Math::Vector3> path;
        size_t next = 0;
        Math::Vector3 path_goal;
        float32 repath_timer = 0.0f;
        PlayerId target = kInvalidPlayer;
        float32 seen_time = 0.0f;
        float32 yaw = 0.0f;
        float32 pitch = 0.0f;
        float32 error_yaw = 0.0f;
        float32 error_pitch = 0.0f;
        float32 error_timer = 0.0f;
        Math::Vector3 stuck_check;
        float32 stuck_timer = 0.0f;
        bool jump = false;
        bool trigger = false;
        bool spawned = false;
    };

    PlayerInput Think(ShiftWorld& world, Bot& bot);
    const SimPlayer* FindTarget(ShiftWorld& world, const SimPlayer& self) const;
    Math::Vector3 ChooseGoal(const ShiftWorld& world, const SimPlayer& self) const;
    void FollowPath(ShiftWorld& world, Bot& bot, const SimPlayer& self, const Math::Vector3& goal, Math::Vector3& direction);

    NavGrid grid_;
    Array<Bot> bots_;
    int32 refresh_counter_ = 0;
};

} // namespace Shift

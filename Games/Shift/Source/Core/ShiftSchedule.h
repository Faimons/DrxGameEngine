#pragma once

/**
 * SHIFT - Wall shift schedule
 * Pure functions of the round seed and round time: server and clients compute identical wall
 * positions without sending them every tick.
 *
 *   shift k (k >= 1) starts at k * interval
 *   warning:  [k * interval - warning_time, k * interval)
 *   movement: [k * interval, k * interval + duration)   (duration limited by max_speed)
 *
 * Every moving segment belongs to a group. A group is in state 0 (start pose) or 1 (moved pose).
 * Each shift applies one pattern that flips some groups:
 *   1 Tor-Tausch    north gates and south gates swap (one wing open, the other closed)
 *   2 Drehraum      machine room walls rotate 90 degrees (and back on the next pattern 2)
 *   3 Kern-Umbau    the two open core doors close, the closed one opens (and back)
 *   4 Engpass       north wing side panels move in 1 m each (and back)
 *   5 Alles offen   every gate, door and panel opens until the next shift (rare)
 * The pattern sequence comes from the round seed; no pattern repeats directly and pattern 5
 * appears at most once in `all_open_max_per` shifts.
 */

#include "ShiftConfig.h"
#include "ShiftTypes.h"

#include "Core/Public/Math/Quaternion.h"

namespace Shift {

enum class SegmentGroup : uint8 {
    Toggle = 0,      // test room: flips on every shift
    NorthGates,
    SouthGates,
    MachineRoom,
    CoreDoorsNS,     // core chamber doors to the wings (start open)
    CoreDoorE,       // core chamber door to the machine room (starts closed)
    NorthPanels,     // north wing side panels (state 1 = retracted, wide corridor)
    Count
};

using GroupMask = uint32;
constexpr GroupMask GroupBit(SegmentGroup group) { return 1u << static_cast<uint32>(group); }

enum class ShiftPattern : uint8 {
    None = 0,
    GateSwap = 1,
    Rotate = 2,
    CoreRebuild = 3,
    Squeeze = 4,
    AllOpen = 5,
    ToggleAll = 6   // test room
};

enum class ShiftMode : uint8 {
    ToggleAll,   // phase 3 test room
    Patterns     // the five blueprint patterns
};

const char* PatternName(ShiftPattern pattern);

struct SegmentPose {
    Math::Vector3 center;
    float32 yaw = 0.0f;   // degrees around +Y

    Math::Quaternion Rotation() const;
};

struct MovingSegmentDef {
    String name;
    Math::Vector3 size;            // full extents (unrotated)
    SegmentGroup group = SegmentGroup::Toggle;
    Math::Vector3 center;          // pose in group state 0
    Math::Vector3 move;            // translation to state 1
    float32 rotate = 0.0f;         // or rotation to state 1 (degrees around pivot)
    Math::Vector3 pivot;

    SegmentPose PoseAt(float32 amount) const;   // 0 = state 0, 1 = state 1
    float32 TravelDistance() const;             // path length of the farthest point
};

class ShiftPlan {
public:
    static constexpr int32 kMaxPlanned = 64;

    ShiftPlan() = default;
    // `groups` = groups that exist in the level (patterns without any of them are never chosen)
    ShiftPlan(ShiftMode mode, uint32 seed, GroupMask groups, int32 all_open_max_per);

    ShiftMode GetMode() const { return mode_; }
    uint32 GetSeed() const { return seed_; }

    ShiftPattern GetPattern(int32 shift) const;            // shift >= 1
    GroupMask GetStateAfter(int32 shift_count) const;      // bit set = group in state 1
    GroupMask GetChangedGroups(int32 shift) const;         // groups that move in this shift

    static GroupMask InitialState();

private:
    ShiftMode mode_ = ShiftMode::ToggleAll;
    uint32 seed_ = 0;
    GroupMask groups_ = 0;
    Array<ShiftPattern> patterns_;   // patterns_[k - 1] = pattern of shift k
};

struct ShiftPhase {
    int32 completed = 0;           // shifts whose movement has finished
    int32 next = 1;                // index of the next (or current) shift
    float32 time_to_next = 0.0f;   // until the next shift starts moving (0 while moving)
    bool warning = false;          // siren and red walls
    bool moving = false;
    float32 move_time = 0.0f;      // seconds since the current shift started moving
};

ShiftPhase EvaluateShiftPhase(const WallShiftSettings& settings, float32 round_time);

// Movement time for a segment: the configured duration, longer if needed to respect max_speed
float32 SegmentMoveDuration(const WallShiftSettings& settings, const MovingSegmentDef& segment);

// 0 = state 0, 1 = state 1, at the given round time
float32 SegmentAmount(const WallShiftSettings& settings, const ShiftPlan& plan, const MovingSegmentDef& segment,
                      float32 round_time);

uint32 HashU32(uint32 a, uint32 b, uint32 c);

} // namespace Shift

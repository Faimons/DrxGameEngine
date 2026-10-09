#pragma once

/**
 * SHIFT - Wall shift schedule
 * Pure functions of the round time: server and clients compute identical wall positions
 * from the same time without sending them every tick.
 *
 *   shift k (k >= 1) starts at k * interval
 *   warning:  [k * interval - warning_time, k * interval)
 *   movement: [k * interval, k * interval + duration)   (duration limited by max_speed)
 */

#include "ShiftConfig.h"
#include "ShiftTypes.h"

namespace Shift {

struct MovingSegmentDef {
    String name;
    Math::Vector3 size;            // full extents
    Math::Vector3 closed_center;   // blocks the passage
    Math::Vector3 open_center;     // passage free
    bool starts_open = true;
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

// Whether the segment is open after `shift_count` shifts (phase 3: every shift toggles it)
bool SegmentOpenAfter(const MovingSegmentDef& segment, int32 shift_count);

// 0 = closed, 1 = open, at the given round time
float32 SegmentOpenAmount(const WallShiftSettings& settings, const MovingSegmentDef& segment, float32 round_time);

Math::Vector3 SegmentCenter(const MovingSegmentDef& segment, float32 open_amount);

} // namespace Shift

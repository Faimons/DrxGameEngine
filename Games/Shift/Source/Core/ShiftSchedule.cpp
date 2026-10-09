/**
 * SHIFT - Wall shift schedule
 */

#include "ShiftSchedule.h"

#include <algorithm>
#include <cmath>

namespace Shift {

ShiftPhase EvaluateShiftPhase(const WallShiftSettings& settings, float32 round_time) {
    ShiftPhase phase;
    const float32 interval = settings.interval;
    if (round_time < 0.0f) round_time = 0.0f;

    // Most recent shift that has started (0 = none yet)
    const auto started = static_cast<int32>(std::floor(round_time / interval));
    const float32 since_start = round_time - static_cast<float32>(started) * interval;

    if (started >= 1 && since_start < settings.move_duration) {
        phase.moving = true;
        phase.next = started;
        phase.completed = started - 1;
        phase.move_time = since_start;
        phase.time_to_next = 0.0f;
        return phase;
    }

    phase.completed = started;
    phase.next = started + 1;
    phase.time_to_next = static_cast<float32>(phase.next) * interval - round_time;
    phase.warning = phase.time_to_next <= settings.warning_time;
    return phase;
}

float32 SegmentMoveDuration(const WallShiftSettings& settings, const MovingSegmentDef& segment) {
    const float32 distance = (segment.open_center - segment.closed_center).Length();
    return std::max(settings.move_duration, distance / settings.max_speed);
}

bool SegmentOpenAfter(const MovingSegmentDef& segment, int32 shift_count) {
    return (shift_count % 2 == 0) ? segment.starts_open : !segment.starts_open;
}

float32 SegmentOpenAmount(const WallShiftSettings& settings, const MovingSegmentDef& segment, float32 round_time) {
    const float32 interval = settings.interval;
    if (round_time < 0.0f) round_time = 0.0f;
    const float32 duration = SegmentMoveDuration(settings, segment);

    const auto started = static_cast<int32>(std::floor(round_time / interval));
    const float32 since_start = round_time - static_cast<float32>(started) * interval;
    const bool now = SegmentOpenAfter(segment, started);
    if (started >= 1 && since_start < duration) {
        const bool before = SegmentOpenAfter(segment, started - 1);
        const float32 t = since_start / duration;   // linear: constant speed, never above max_speed
        const float32 from = before ? 1.0f : 0.0f;
        const float32 to = now ? 1.0f : 0.0f;
        return from + (to - from) * t;
    }
    return now ? 1.0f : 0.0f;
}

Math::Vector3 SegmentCenter(const MovingSegmentDef& segment, float32 open_amount) {
    return segment.closed_center + (segment.open_center - segment.closed_center) * open_amount;
}

} // namespace Shift

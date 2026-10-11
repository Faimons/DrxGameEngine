/**
 * SHIFT - Wall shift schedule
 */

#include "ShiftSchedule.h"

#include <algorithm>
#include <cmath>

namespace Shift {

namespace {

constexpr float32 kDegToRad = 3.14159265358979323846f / 180.0f;

constexpr GroupMask kGateGroups = GroupBit(SegmentGroup::NorthGates) | GroupBit(SegmentGroup::SouthGates);
constexpr GroupMask kCoreGroups = GroupBit(SegmentGroup::CoreDoorsNS) | GroupBit(SegmentGroup::CoreDoorE);
constexpr GroupMask kAllOpenGroups =
    kGateGroups | kCoreGroups | GroupBit(SegmentGroup::NorthPanels);

} // namespace

GroupMask PatternGroups(ShiftPattern pattern) {
    switch (pattern) {
        case ShiftPattern::GateSwap: return kGateGroups;
        case ShiftPattern::Rotate: return GroupBit(SegmentGroup::MachineRoom);
        case ShiftPattern::CoreRebuild: return kCoreGroups;
        case ShiftPattern::Squeeze: return GroupBit(SegmentGroup::NorthPanels);
        case ShiftPattern::AllOpen: return kAllOpenGroups;
        case ShiftPattern::ToggleAll: return ~GroupMask(0);
        case ShiftPattern::None: break;
    }
    return 0;
}

GroupMask PatternPreviewState(ShiftPattern pattern) {
    const GroupMask base = ShiftPlan::InitialState();
    if (pattern == ShiftPattern::AllOpen) return base | PatternGroups(pattern);
    return base ^ PatternGroups(pattern);
}

uint32 HashU32(uint32 a, uint32 b, uint32 c) {
    uint32 h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

const char* PatternName(ShiftPattern pattern) {
    switch (pattern) {
        case ShiftPattern::GateSwap: return "Tor-Tausch";
        case ShiftPattern::Rotate: return "Drehraum";
        case ShiftPattern::CoreRebuild: return "Kern-Umbau";
        case ShiftPattern::Squeeze: return "Engpass";
        case ShiftPattern::AllOpen: return "Alles offen";
        case ShiftPattern::ToggleAll: return "Wechsel";
        case ShiftPattern::None: break;
    }
    return "-";
}

Math::Quaternion SegmentPose::Rotation() const {
    return Math::Quaternion::FromAxisAngle(Math::Vector3::Up, yaw * kDegToRad);
}

SegmentPose MovingSegmentDef::PoseAt(float32 amount) const {
    SegmentPose pose;
    if (rotate != 0.0f) {
        pose.yaw = rotate * amount;
        pose.center = pivot + pose.Rotation().Rotate(center - pivot);
    } else {
        pose.center = center + move * amount;
    }
    return pose;
}

float32 MovingSegmentDef::TravelDistance() const {
    if (rotate == 0.0f) return move.Length();
    // Farthest corner from the pivot in the horizontal plane
    const Math::Vector3 offset = center - pivot;
    const float32 half_diagonal = std::sqrt(size.x * size.x + size.z * size.z) * 0.5f;
    const float32 radius = std::sqrt(offset.x * offset.x + offset.z * offset.z) + half_diagonal;
    return radius * std::abs(rotate) * kDegToRad;
}

ShiftPlan::ShiftPlan(ShiftMode mode, uint32 seed, GroupMask groups, int32 all_open_max_per)
    : mode_(mode), seed_(seed), groups_(groups) {
    patterns_.reserve(kMaxPlanned);
    if (mode == ShiftMode::ToggleAll) {
        patterns_.assign(kMaxPlanned, ShiftPattern::ToggleAll);
        return;
    }

    // Weighted choice; pattern 5 is rare and limited to one in `all_open_max_per` shifts
    struct Candidate {
        ShiftPattern pattern;
        uint32 weight;
    };
    const int32 spacing = std::max(1, all_open_max_per);
    ShiftPattern previous = ShiftPattern::None;
    int32 last_all_open = -spacing;
    for (int32 k = 1; k <= kMaxPlanned; ++k) {
        Candidate candidates[5];
        uint32 count = 0;
        uint32 total = 0;
        for (const ShiftPattern p : {ShiftPattern::GateSwap, ShiftPattern::Rotate, ShiftPattern::CoreRebuild,
                                     ShiftPattern::Squeeze, ShiftPattern::AllOpen}) {
            if (p == previous || (PatternGroups(p) & groups_) == 0) continue;
            uint32 weight = 4;
            if (p == ShiftPattern::AllOpen) {
                if (k - last_all_open < spacing) continue;
                weight = 1;
            }
            candidates[count++] = {p, weight};
            total += weight;
        }
        ShiftPattern chosen = ShiftPattern::None;
        if (total > 0) {
            uint32 pick = HashU32(seed_, static_cast<uint32>(k), 0x5348u) % total;
            for (uint32 i = 0; i < count; ++i) {
                if (pick < candidates[i].weight) {
                    chosen = candidates[i].pattern;
                    break;
                }
                pick -= candidates[i].weight;
            }
        }
        if (chosen == ShiftPattern::AllOpen) last_all_open = k;
        patterns_.push_back(chosen);
        previous = chosen;
    }
}

GroupMask ShiftPlan::InitialState() {
    return GroupBit(SegmentGroup::Toggle) | GroupBit(SegmentGroup::SouthGates) | GroupBit(SegmentGroup::CoreDoorsNS) |
           GroupBit(SegmentGroup::NorthPanels);
}

ShiftPattern ShiftPlan::GetPattern(int32 shift) const {
    if (shift < 1 || patterns_.empty()) return ShiftPattern::None;
    // Rounds are 8 minutes (16 shifts); beyond the plan the sequence repeats
    return patterns_[static_cast<size_t>(shift - 1) % patterns_.size()];
}

GroupMask ShiftPlan::GetStateAfter(int32 shift_count) const {
    // "Alles offen" is an overlay: the next shift continues from the state before it
    GroupMask base = InitialState();
    bool all_open = false;
    for (int32 k = 1; k <= shift_count; ++k) {
        const ShiftPattern pattern = GetPattern(k);
        if (pattern == ShiftPattern::AllOpen) {
            all_open = true;
            continue;
        }
        all_open = false;
        base ^= PatternGroups(pattern);
    }
    return all_open ? (base | kAllOpenGroups) : base;
}

GroupMask ShiftPlan::GetChangedGroups(int32 shift) const {
    if (shift < 1) return 0;
    return (GetStateAfter(shift) ^ GetStateAfter(shift - 1)) & groups_;
}

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
    return std::max(settings.move_duration, segment.TravelDistance() / settings.max_speed);
}

float32 SegmentAmount(const WallShiftSettings& settings, const ShiftPlan& plan, const MovingSegmentDef& segment,
                      float32 round_time) {
    const float32 interval = settings.interval;
    if (round_time < 0.0f) round_time = 0.0f;
    const GroupMask bit = GroupBit(segment.group);

    const auto started = static_cast<int32>(std::floor(round_time / interval));
    const float32 since_start = round_time - static_cast<float32>(started) * interval;
    const float32 now = (plan.GetStateAfter(started) & bit) ? 1.0f : 0.0f;
    if (started >= 1) {
        const float32 duration = SegmentMoveDuration(settings, segment);
        if (since_start < duration) {
            const float32 before = (plan.GetStateAfter(started - 1) & bit) ? 1.0f : 0.0f;
            // Linear: constant speed, never above max_speed
            return before + (now - before) * (since_start / duration);
        }
    }
    return now;
}

} // namespace Shift

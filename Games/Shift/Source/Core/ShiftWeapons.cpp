/**
 * SHIFT - Weapon state machine
 */

#include "ShiftWeapons.h"
#include "ShiftTypes.h"

#include <algorithm>
#include <cmath>

namespace Shift {

namespace {
constexpr float32 kDegToRad = 3.14159265358979323846f / 180.0f;
} // namespace

Math::Vector3 LookDirection(float32 yaw_degrees, float32 pitch_degrees) {
    const float32 yaw = yaw_degrees * kDegToRad;
    const float32 pitch = pitch_degrees * kDegToRad;
    return Math::Vector3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw));
}

Math::Vector3 FlatForward(float32 yaw_degrees) {
    const float32 yaw = yaw_degrees * kDegToRad;
    return Math::Vector3(std::sin(yaw), 0.0f, -std::cos(yaw));
}

Math::Vector3 FlatRight(float32 yaw_degrees) {
    const float32 yaw = yaw_degrees * kDegToRad;
    return Math::Vector3(std::cos(yaw), 0.0f, std::sin(yaw));
}

void WeaponState::Equip(const WeaponDef* weapon) {
    def = weapon;
    ammo = weapon ? weapon->magazine : 0;
    cooldown = 0.0f;
    reload_timer = 0.0f;
    trigger_down = false;
    shot_queued = false;
}

WeaponTickResult UpdateWeapon(WeaponState& w, bool trigger, bool reload, float32 dt) {
    WeaponTickResult result;
    if (!w.def) return result;

    const bool pressed = trigger && !w.trigger_down;
    w.trigger_down = trigger;
    if (pressed && !w.def->automatic) w.shot_queued = true;

    if (w.reload_timer > 0.0f) {
        w.reload_timer -= dt;
        if (w.reload_timer <= 0.0f) {
            w.reload_timer = 0.0f;
            w.ammo = w.def->magazine;
            result.reload_finished = true;
        }
        w.shot_queued = false;
        w.cooldown = std::max(0.0f, w.cooldown - dt);
        return result;
    }

    if (reload && w.ammo < w.def->magazine) {
        w.reload_timer = w.def->reload_time;
        w.shot_queued = false;
        result.reload_started = true;
        return result;
    }

    // While firing, the remainder of the cooldown carries over so fire rates that are not a multiple
    // of the tick stay exact; an idle weapon builds no credit
    const bool was_cooling = w.cooldown > 0.0f;
    w.cooldown = was_cooling ? w.cooldown - dt : 0.0f;
    const bool wants_shot = w.def->automatic ? trigger : w.shot_queued;
    if (!wants_shot) {
        w.cooldown = std::max(0.0f, w.cooldown);
        return result;
    }

    if (w.ammo <= 0) {
        if (pressed) {
            result.empty_click = true;
            w.reload_timer = w.def->reload_time;
            result.reload_started = true;
        }
        w.shot_queued = false;
        w.cooldown = std::max(0.0f, w.cooldown);
        return result;
    }

    // Tolerance absorbs float rounding (3 x 1/30 s is not exactly 0.1 s)
    constexpr float32 kEpsilon = 1e-4f;
    const float32 interval = 1.0f / w.def->fire_rate;
    while (w.cooldown <= kEpsilon && w.ammo > 0 && (w.def->automatic ? trigger : w.shot_queued)) {
        ++result.shots;
        --w.ammo;
        w.cooldown += interval;
        w.shot_queued = false;
    }
    if (w.cooldown < 0.0f) w.cooldown = 0.0f;
    return result;
}

float32 DamageFalloff(const WeaponDef& weapon, float32 distance) {
    if (weapon.falloff_end <= 0.0f || distance <= weapon.falloff_start) return 1.0f;
    if (distance >= weapon.falloff_end || weapon.falloff_end <= weapon.falloff_start) return weapon.falloff_min;
    const float32 t = (distance - weapon.falloff_start) / (weapon.falloff_end - weapon.falloff_start);
    return 1.0f + (weapon.falloff_min - 1.0f) * t;
}

} // namespace Shift

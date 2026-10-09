#pragma once

/**
 * SHIFT - Weapon state machine (tick based, deterministic)
 */

#include "ShiftConfig.h"

namespace Shift {

struct WeaponState {
    const WeaponDef* def = nullptr;
    int32 ammo = 0;
    float32 cooldown = 0.0f;       // time until the next shot is allowed
    float32 reload_timer = 0.0f;   // > 0 while reloading
    bool trigger_down = false;
    bool shot_queued = false;      // semi-automatic click buffered during the cooldown

    void Equip(const WeaponDef* weapon);
    bool IsReloading() const { return reload_timer > 0.0f; }
};

struct WeaponTickResult {
    uint32 shots = 0;          // shots fired this tick
    bool empty_click = false;  // trigger pulled with an empty magazine
    bool reload_started = false;
    bool reload_finished = false;
};

/**
 * Advances a weapon by one tick. Magazines reload automatically when empty and the trigger is
 * pulled; reserve ammunition is unlimited (blueprint version 1).
 */
WeaponTickResult UpdateWeapon(WeaponState& weapon, bool trigger, bool reload, float32 dt);

// Damage multiplier for the distance (shotgun falloff)
float32 DamageFalloff(const WeaponDef& weapon, float32 distance);

} // namespace Shift

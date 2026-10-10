#pragma once

/**
 * SHIFT - Game configuration
 * Every gameplay number lives in shift.json; the defaults below are the blueprint start values.
 */

#include "Core/Public/Config.h"
#include "Core/Public/CoreTypes.h"

namespace Shift {

using namespace DRX;

struct PlayerSettings {
    float32 max_health = 100.0f;
    float32 walk_speed = 5.5f;
    float32 carrier_speed = 4.0f;
    float32 jump_height = 1.0f;
    float32 gravity = 20.0f;
    float32 air_control = 0.3f;
    float32 radius = 0.3f;              // 0.6 m wide: crushed when less than 0.6 m remain
    float32 height = 1.8f;
    float32 eye_height = 1.65f;
    float32 head_height = 1.5f;         // hits at or above this height (from the feet) are headshots
    float32 fov = 90.0f;
    float32 respawn_time = 8.0f;
    float32 spawn_protection = 2.0f;
    float32 core_pickup_time = 1.0f;
    float32 mouse_sensitivity = 0.1f;   // degrees per mouse pixel
};

struct WeaponDef {
    String id;
    String name;
    float32 damage = 20.0f;
    float32 headshot_multiplier = 1.5f;
    uint32 pellets = 1;
    float32 fire_rate = 5.0f;           // shots per second
    bool automatic = false;
    int32 magazine = 12;
    float32 reload_time = 1.4f;
    float32 range = 150.0f;
    float32 spread = 0.0f;              // degrees, cone half angle
    float32 moving_spread = 0.0f;       // extra spread while moving
    float32 falloff_start = 0.0f;       // metres, 0 = no falloff
    float32 falloff_end = 0.0f;
    float32 falloff_min = 1.0f;         // damage factor at and beyond falloff_end
    float32 zoom = 1.0f;                // field of view divisor while aiming
};

struct WeaponSettings {
    WeaponDef rifle;
    WeaponDef shotgun;
    WeaponDef sniper;
    WeaponDef pistol;
};

struct AnchorSettings {
    float32 speed = 40.0f;
    float32 freeze_time = 10.0f;
    float32 cooldown = 20.0f;
    int32 max_active_per_team = 2;
};

struct RoundSettings {
    int32 rounds_to_win = 2;            // best of 3
    float32 round_time = 480.0f;
    float32 freeze_time = 10.0f;        // nothing moves at round start
    float32 core_return_time = 20.0f;
    float32 core_pickup_range = 1.6f;    // metres from the core (horizontal)
    float32 round_end_delay = 6.0f;      // result screen before the next round
    float32 match_end_delay = 12.0f;     // result screen before a new match
    String map = "arena";               // "arena" (six rooms) or "testroom"
};

struct WallShiftSettings {
    float32 interval = 30.0f;
    float32 warning_time = 5.0f;
    float32 move_duration = 4.0f;
    float32 max_speed = 1.5f;
    float32 crush_gap = 0.6f;
    int32 all_open_max_per = 6;         // pattern 5 at most once in this many shifts
};

struct DroneSettings {
    int32 count = 3;
    float32 health = 40.0f;
    float32 damage = 6.0f;
    float32 fire_rate = 2.0f;
    float32 speed = 3.0f;
    float32 attack_radius = 12.0f;
    float32 return_time = 5.0f;
    float32 respawn_time = 30.0f;
};

struct NetSettings {
    int32 tick_rate = 30;
    int32 port = 27015;
    float32 lag_compensation_max_ms = 400.0f;   // ping + interpolation delay must fit
    float32 interpolation_delay_ms = 100.0f;
    float32 wall_correction_interval = 1.0f;
    float32 simulated_latency_ms = 0.0f;
    float32 simulated_jitter_ms = 0.0f;
    float32 simulated_loss = 0.0f;
    String server_address = "127.0.0.1";   // used by "Beitreten"
};

struct ShiftConfig {
    PlayerSettings player;
    WeaponSettings weapons;
    AnchorSettings anchor;
    RoundSettings round;
    WallShiftSettings shifts;
    DroneSettings drones;
    NetSettings net;

    ShiftConfig();

    // Reads every value (missing keys keep their defaults); invalid values are clamped with a warning
    void Load(const Config& config);
    float32 GetTickInterval() const { return 1.0f / static_cast<float32>(net.tick_rate); }
    const WeaponDef* FindWeapon(const String& id) const;
};

} // namespace Shift

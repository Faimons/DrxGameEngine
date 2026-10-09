/**
 * SHIFT - Game configuration
 */

#include "ShiftConfig.h"

#include "Core/Public/Log.h"

#include <algorithm>

namespace Shift {

namespace {

class Reader {
public:
    explicit Reader(const Config& config) : config_(config) {}

    // Positive or zero number; negative values fall back to the default
    void Number(const String& key, float32& value, float32 min = 0.0f, float32 max = 1.0e6f) {
        const float32 read = config_.GetFloat(key, value);
        const float32 clamped = std::clamp(read, min, max);
        if (clamped != read) DRX_LOG_WARNING("shift config '{}' = {} out of range [{}, {}], using {}", key, read, min, max, clamped);
        value = clamped;
    }

    void Integer(const String& key, int32& value, int32 min, int32 max) {
        const int32 read = config_.GetInt(key, value);
        const int32 clamped = std::clamp(read, min, max);
        if (clamped != read) DRX_LOG_WARNING("shift config '{}' = {} out of range [{}, {}], using {}", key, read, min, max, clamped);
        value = clamped;
    }

    void Flag(const String& key, bool& value) { value = config_.GetBool(key, value); }

    void Weapon(const String& prefix, WeaponDef& w) {
        w.name = config_.GetString(prefix + ".name", w.name);
        Number(prefix + ".damage", w.damage, 0.0f, 1000.0f);
        Number(prefix + ".headshot_multiplier", w.headshot_multiplier, 1.0f, 10.0f);
        int32 pellets = static_cast<int32>(w.pellets);
        Integer(prefix + ".pellets", pellets, 1, 32);
        w.pellets = static_cast<uint32>(pellets);
        Number(prefix + ".fire_rate", w.fire_rate, 0.05f, 100.0f);
        Flag(prefix + ".automatic", w.automatic);
        Integer(prefix + ".magazine", w.magazine, 1, 1000);
        Number(prefix + ".reload_time", w.reload_time, 0.0f, 30.0f);
        Number(prefix + ".range", w.range, 1.0f, 2000.0f);
        Number(prefix + ".spread", w.spread, 0.0f, 45.0f);
        Number(prefix + ".moving_spread", w.moving_spread, 0.0f, 45.0f);
        Number(prefix + ".falloff_start", w.falloff_start, 0.0f, 2000.0f);
        Number(prefix + ".falloff_end", w.falloff_end, 0.0f, 2000.0f);
        Number(prefix + ".falloff_min", w.falloff_min, 0.0f, 1.0f);
        Number(prefix + ".zoom", w.zoom, 1.0f, 10.0f);
        if (w.falloff_end < w.falloff_start) w.falloff_end = w.falloff_start;
    }

private:
    const Config& config_;
};

WeaponDef MakeWeapon(const char* id, const char* name, float32 damage, float32 head, uint32 pellets, float32 rate,
                     bool automatic, int32 magazine, float32 reload) {
    WeaponDef w;
    w.id = id;
    w.name = name;
    w.damage = damage;
    w.headshot_multiplier = head;
    w.pellets = pellets;
    w.fire_rate = rate;
    w.automatic = automatic;
    w.magazine = magazine;
    w.reload_time = reload;
    return w;
}

} // namespace

ShiftConfig::ShiftConfig() {
    // Blueprint section 5
    weapons.rifle = MakeWeapon("rifle", "Sturmgewehr", 14.0f, 1.5f, 1, 10.0f, true, 30, 2.0f);
    weapons.rifle.spread = 0.6f;

    weapons.shotgun = MakeWeapon("shotgun", "Schrotflinte", 9.0f, 1.5f, 8, 1.2f, false, 6, 2.5f);
    weapons.shotgun.spread = 4.0f;
    weapons.shotgun.range = 60.0f;
    weapons.shotgun.falloff_start = 8.0f;
    weapons.shotgun.falloff_end = 20.0f;
    weapons.shotgun.falloff_min = 0.3f;

    weapons.sniper = MakeWeapon("sniper", "Praezisionsgewehr", 70.0f, 1.5f, 1, 0.8f, false, 5, 2.8f);
    weapons.sniper.range = 300.0f;
    weapons.sniper.moving_spread = 3.0f;
    weapons.sniper.zoom = 3.0f;

    weapons.pistol = MakeWeapon("pistol", "Pistole", 20.0f, 1.5f, 1, 5.0f, false, 12, 1.4f);
    weapons.pistol.spread = 0.3f;
}

void ShiftConfig::Load(const Config& config) {
    Reader r(config);

    r.Number("player.max_health", player.max_health, 1.0f, 10000.0f);
    r.Number("player.walk_speed", player.walk_speed, 0.1f, 50.0f);
    r.Number("player.carrier_speed", player.carrier_speed, 0.1f, 50.0f);
    r.Number("player.jump_height", player.jump_height, 0.0f, 10.0f);
    r.Number("player.gravity", player.gravity, 0.1f, 100.0f);
    r.Number("player.air_control", player.air_control, 0.0f, 1.0f);
    r.Number("player.radius", player.radius, 0.1f, 1.0f);
    r.Number("player.height", player.height, 2.0f * player.radius, 4.0f);
    r.Number("player.eye_height", player.eye_height, 0.1f, player.height);
    r.Number("player.head_height", player.head_height, 0.1f, player.height);
    r.Number("player.fov", player.fov, 40.0f, 130.0f);
    r.Number("player.respawn_time", player.respawn_time, 0.0f, 120.0f);
    r.Number("player.spawn_protection", player.spawn_protection, 0.0f, 30.0f);
    r.Number("player.core_pickup_time", player.core_pickup_time, 0.0f, 30.0f);
    r.Number("player.mouse_sensitivity", player.mouse_sensitivity, 0.001f, 10.0f);

    r.Weapon("weapons.rifle", weapons.rifle);
    r.Weapon("weapons.shotgun", weapons.shotgun);
    r.Weapon("weapons.sniper", weapons.sniper);
    r.Weapon("weapons.pistol", weapons.pistol);

    r.Number("anchor.speed", anchor.speed, 1.0f, 500.0f);
    r.Number("anchor.freeze_time", anchor.freeze_time, 0.0f, 120.0f);
    r.Number("anchor.cooldown", anchor.cooldown, 0.0f, 300.0f);
    r.Integer("anchor.max_active_per_team", anchor.max_active_per_team, 0, 16);

    r.Integer("round.rounds_to_win", round.rounds_to_win, 1, 10);
    r.Number("round.round_time", round.round_time, 10.0f, 7200.0f);
    r.Number("round.freeze_time", round.freeze_time, 0.0f, 120.0f);
    r.Number("round.core_return_time", round.core_return_time, 0.0f, 600.0f);

    r.Number("shifts.interval", shifts.interval, 5.0f, 600.0f);
    r.Number("shifts.warning_time", shifts.warning_time, 0.0f, shifts.interval);
    r.Number("shifts.move_duration", shifts.move_duration, 0.1f, shifts.interval);
    r.Number("shifts.max_speed", shifts.max_speed, 0.1f, 20.0f);
    r.Number("shifts.crush_gap", shifts.crush_gap, 0.1f, 5.0f);
    r.Integer("shifts.all_open_max_per", shifts.all_open_max_per, 2, 100);

    r.Integer("drones.count", drones.count, 0, 32);
    r.Number("drones.health", drones.health, 1.0f, 10000.0f);
    r.Number("drones.damage", drones.damage, 0.0f, 1000.0f);
    r.Number("drones.fire_rate", drones.fire_rate, 0.05f, 50.0f);
    r.Number("drones.speed", drones.speed, 0.1f, 50.0f);
    r.Number("drones.attack_radius", drones.attack_radius, 0.0f, 500.0f);
    r.Number("drones.return_time", drones.return_time, 0.0f, 120.0f);
    r.Number("drones.respawn_time", drones.respawn_time, 0.0f, 600.0f);

    r.Integer("net.tick_rate", net.tick_rate, 10, 128);
    r.Integer("net.port", net.port, 1, 65535);
    r.Number("net.lag_compensation_max_ms", net.lag_compensation_max_ms, 0.0f, 1000.0f);
    r.Number("net.interpolation_delay_ms", net.interpolation_delay_ms, 0.0f, 1000.0f);
    r.Number("net.wall_correction_interval", net.wall_correction_interval, 0.1f, 60.0f);
    r.Number("net.simulated_latency_ms", net.simulated_latency_ms, 0.0f, 2000.0f);
    r.Number("net.simulated_jitter_ms", net.simulated_jitter_ms, 0.0f, 1000.0f);
    r.Number("net.simulated_loss", net.simulated_loss, 0.0f, 1.0f);
}

const WeaponDef* ShiftConfig::FindWeapon(const String& id) const {
    for (const WeaponDef* w : {&weapons.rifle, &weapons.shotgun, &weapons.sniper, &weapons.pistol}) {
        if (w->id == id) return w;
    }
    return nullptr;
}

} // namespace Shift

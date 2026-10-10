#pragma once

/**
 * SHIFT - Shared simulation types (identical on server and client)
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"

namespace Shift {

using namespace DRX;

using PlayerId = uint32;
constexpr PlayerId kInvalidPlayer = 0;

enum class Team : uint8 { A = 0, B = 1 };

inline Team Opponent(Team team) { return team == Team::A ? Team::B : Team::A; }

enum InputButton : uint16 {
    kButtonJump = 1 << 0,
    kButtonFire = 1 << 1,
    kButtonReload = 1 << 2,
    kButtonInteract = 1 << 3,
    kButtonAim = 1 << 4,
};

/**
 * One tick of player input. This is all a client sends; the server simulates the rest.
 * Look angles in degrees: yaw 0 looks down -Z, positive pitch looks up.
 */
struct PlayerInput {
    uint32 tick = 0;
    float32 move_x = 0.0f;   // right
    float32 move_y = 0.0f;   // forward
    float32 yaw = 0.0f;
    float32 pitch = 0.0f;
    uint16 buttons = 0;
    float32 view_tick = 0.0f;   // server tick the client was showing when it fired (0 = now)

    bool Has(InputButton button) const { return (buttons & button) != 0; }
};

Math::Vector3 LookDirection(float32 yaw_degrees, float32 pitch_degrees);
Math::Vector3 FlatForward(float32 yaw_degrees);
Math::Vector3 FlatRight(float32 yaw_degrees);

} // namespace Shift

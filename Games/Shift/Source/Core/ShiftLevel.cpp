/**
 * SHIFT - Level description
 */

#include "ShiftLevel.h"

namespace Shift {

void Level::AddBox(const Math::Vector3& center, const Math::Vector3& size, Surface surface) {
    LevelBox box;
    box.center = center;
    box.size = size;
    box.surface = surface;
    // The ceiling would put the whole room into the sun shadow
    box.casts_shadow = surface != Surface::Ceiling;
    boxes.push_back(box);
}

void Level::Apply(ShiftWorld& world) const {
    for (const LevelBox& box : boxes) {
        if (box.collision) world.AddStaticBox(box.center, box.size);
    }
    world.SetSpawn(Team::A, spawns[0]);
    world.SetSpawn(Team::B, spawns[1]);
}

Level BuildTestRoom() {
    using Math::Vector3;
    constexpr float32 kSize = 12.0f;
    constexpr float32 kHeight = 4.0f;
    constexpr float32 kWall = 0.5f;
    const float32 half = kSize * 0.5f;

    Level level;
    level.name = "Testraum";

    level.AddBox({0, -0.25f, 0}, {kSize + 2 * kWall, 0.5f, kSize + 2 * kWall}, Surface::Floor);
    level.AddBox({0, kHeight + 0.25f, 0}, {kSize + 2 * kWall, 0.5f, kSize + 2 * kWall}, Surface::Ceiling);
    level.AddBox({0, kHeight * 0.5f, -half - kWall * 0.5f}, {kSize + 2 * kWall, kHeight, kWall}, Surface::Wall);
    level.AddBox({0, kHeight * 0.5f, half + kWall * 0.5f}, {kSize + 2 * kWall, kHeight, kWall}, Surface::Wall);
    level.AddBox({-half - kWall * 0.5f, kHeight * 0.5f, 0}, {kWall, kHeight, kSize}, Surface::Wall);
    level.AddBox({half + kWall * 0.5f, kHeight * 0.5f, 0}, {kWall, kHeight, kSize}, Surface::Wall);

    // Cover blocks (waist and head height), a jumpable ledge (0.9 m) and a step row (0.25 m each)
    level.AddBox({-3.0f, 0.5f, 0.0f}, {1.5f, 1.0f, 0.6f}, Surface::Cover);
    level.AddBox({3.0f, 0.9f, -1.5f}, {0.6f, 1.8f, 1.5f}, Surface::Cover);
    level.AddBox({-4.5f, 0.45f, -4.0f}, {2.0f, 0.9f, 2.0f}, Surface::Cover);
    for (int i = 0; i < 4; ++i) {
        const float32 h = 0.25f * static_cast<float32>(i + 1);
        level.AddBox({4.6f, h * 0.5f, 2.0f + 0.4f * static_cast<float32>(i)}, {1.6f, h, 0.4f}, Surface::Step);
    }

    // Spawn markers (no collision)
    LevelBox marker_a{{0, 0.01f, 4.5f}, {1.5f, 0.02f, 1.5f}, Surface::SpawnA, false, false};
    LevelBox marker_b{{0, 0.01f, -4.5f}, {1.5f, 0.02f, 1.5f}, Surface::SpawnB, false, false};
    level.boxes.push_back(marker_a);
    level.boxes.push_back(marker_b);

    level.spawns[0] = {Vector3(0, 0, 4.5f), 0.0f};      // looks north (-Z)
    level.spawns[1] = {Vector3(0, 0, -4.5f), 180.0f};   // looks south
    return level;
}

} // namespace Shift

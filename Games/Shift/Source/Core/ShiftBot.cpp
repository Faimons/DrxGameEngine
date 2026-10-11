/**
 * SHIFT - Computer opponents
 */

#include "ShiftBot.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace Shift {

namespace {

constexpr float32 kRadToDeg = 180.0f / 3.14159265358979323846f;
constexpr float32 kDegToRad = 3.14159265358979323846f / 180.0f;
constexpr Physics::CollisionLayer kWorldMask = Physics::CollisionLayer::Static | Physics::CollisionLayer::Kinematic;
constexpr float32 kMaxStep = 0.4f;          // height difference between neighbouring cells
constexpr float32 kProbeTop = 3.2f;         // ground probe starts below the 4 m ceilings
constexpr float32 kMaxGround = 2.0f;
constexpr float32 kClearance = 0.35f;       // body clearance starts above steps
constexpr int32 kRefreshTicks = 5;

Physics::QueryFilter WorldFilter() {
    Physics::QueryFilter filter;
    filter.mask = kWorldMask;
    return filter;
}

float32 FlatDistanceSquared(const Math::Vector3& a, const Math::Vector3& b) {
    const float32 dx = a.x - b.x;
    const float32 dz = a.z - b.z;
    return dx * dx + dz * dz;
}

Math::Vector3 Flat(const Math::Vector3& v) { return Math::Vector3(v.x, 0.0f, v.z); }

float32 YawOf(const Math::Vector3& direction) { return std::atan2(direction.x, -direction.z) * kRadToDeg; }

float32 WrapAngle(float32 degrees) {
    degrees = std::fmod(degrees + 180.0f, 360.0f);
    if (degrees < 0.0f) degrees += 360.0f;
    return degrees - 180.0f;
}

// Deterministic noise in [-1, 1]
float32 Noise(uint32 a, uint32 b, uint32 c) {
    return static_cast<float32>(HashU32(a, b, c) & 0xFFFFu) / 32767.5f - 1.0f;
}

} // namespace

// ============================================================================
// NavGrid
// ============================================================================

void NavGrid::Build(ShiftWorld& world, const Level& level, float32 cell) {
    cell_ = cell;
    radius_ = world.GetConfig().player.radius;
    height_ = world.GetConfig().player.height;

    Math::Vector3 min(std::numeric_limits<float32>::max());
    Math::Vector3 max(std::numeric_limits<float32>::lowest());
    for (const LevelBox& box : level.boxes) {
        if (!box.collision) continue;
        min = Math::Vector3::Min(min, box.center - box.size * 0.5f);
        max = Math::Vector3::Max(max, box.center + box.size * 0.5f);
    }
    if (min.x > max.x) {
        width_ = depth_ = 0;
        return;
    }
    origin_ = Math::Vector3(min.x, 0.0f, min.z);
    width_ = std::max(1, static_cast<int32>(std::ceil((max.x - min.x) / cell_)));
    depth_ = std::max(1, static_cast<int32>(std::ceil((max.z - min.z) / cell_)));
    const size_t count = static_cast<size_t>(width_) * static_cast<size_t>(depth_);
    ground_.assign(count, 0.0f);
    walkable_.assign(count, 0);
    for (int32 i = 0; i < width_ * depth_; ++i) SampleCell(world, i);

    // Cells any moving segment can reach (swept over its whole travel)
    dynamic_cells_.clear();
    Array<uint8> marked(count, 0);
    const float32 margin = radius_ + cell_;
    for (const SimSegment& segment : world.GetSegments()) {
        Math::Vector3 lo(std::numeric_limits<float32>::max());
        Math::Vector3 hi(std::numeric_limits<float32>::lowest());
        const Math::Vector3 half = segment.def.size * 0.5f;
        for (int32 step = 0; step <= 16; ++step) {
            const SegmentPose pose = segment.def.PoseAt(static_cast<float32>(step) / 16.0f);
            const Math::Quaternion rotation = pose.Rotation();
            for (int32 corner = 0; corner < 4; ++corner) {
                const Math::Vector3 local((corner & 1) ? half.x : -half.x, 0.0f, (corner & 2) ? half.z : -half.z);
                const Math::Vector3 p = pose.center + rotation.Rotate(local);
                lo = Math::Vector3::Min(lo, p);
                hi = Math::Vector3::Max(hi, p);
            }
        }
        int32 x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        CellOf(lo - Math::Vector3(margin, 0.0f, margin), x0, z0);
        CellOf(hi + Math::Vector3(margin, 0.0f, margin), x1, z1);
        for (int32 z = z0; z <= z1; ++z) {
            for (int32 x = x0; x <= x1; ++x) {
                const int32 i = Index(x, z);
                if (!marked[static_cast<size_t>(i)]) {
                    marked[static_cast<size_t>(i)] = 1;
                    dynamic_cells_.push_back(i);
                }
            }
        }
    }
    segment_amounts_.clear();
    for (const SimSegment& segment : world.GetSegments()) segment_amounts_.push_back(segment.amount);
}

void NavGrid::Refresh(ShiftWorld& world) {
    const Array<SimSegment>& segments = world.GetSegments();
    bool moved = segments.size() != segment_amounts_.size();
    for (size_t i = 0; !moved && i < segments.size(); ++i) {
        moved = std::abs(segments[i].amount - segment_amounts_[i]) > 1.0e-3f;
    }
    if (!moved) return;
    segment_amounts_.clear();
    for (const SimSegment& segment : segments) segment_amounts_.push_back(segment.amount);
    for (const int32 i : dynamic_cells_) SampleCell(world, i);
}

bool NavGrid::CellOf(const Math::Vector3& point, int32& x, int32& z) const {
    const int32 cx = static_cast<int32>(std::floor((point.x - origin_.x) / cell_));
    const int32 cz = static_cast<int32>(std::floor((point.z - origin_.z) / cell_));
    x = std::clamp(cx, 0, width_ - 1);
    z = std::clamp(cz, 0, depth_ - 1);
    return cx == x && cz == z;
}

Math::Vector3 NavGrid::CellCenter(int32 x, int32 z) const {
    return Math::Vector3(origin_.x + (static_cast<float32>(x) + 0.5f) * cell_, ground_[static_cast<size_t>(Index(x, z))],
                         origin_.z + (static_cast<float32>(z) + 0.5f) * cell_);
}

void NavGrid::SampleCell(ShiftWorld& world, int32 index) {
    const size_t i = static_cast<size_t>(index);
    const float32 x = origin_.x + (static_cast<float32>(index % width_) + 0.5f) * cell_;
    const float32 z = origin_.z + (static_cast<float32>(index / width_) + 0.5f) * cell_;
    const Physics::QueryFilter filter = WorldFilter();
    const Physics::RaycastResult ground =
        world.GetPhysics().Raycast(Math::Vector3(x, kProbeTop, z), Math::Vector3(x, -1.0f, z), filter);
    if (!ground.hit || ground.point.y > kMaxGround) {
        walkable_[i] = 0;
        ground_[i] = 0.0f;
        return;
    }
    ground_[i] = ground.point.y;
    const float32 half_height = (height_ - kClearance) * 0.5f;
    const Math::Vector3 center(x, ground.point.y + kClearance + half_height, z);
    walkable_[i] = world.GetPhysics()
                       .OverlapBox(center, Math::Vector3(radius_, half_height, radius_), Math::Quaternion::Identity(), filter)
                       .empty()
                       ? 1
                       : 0;
}

bool NavGrid::IsWalkable(const Math::Vector3& point) const {
    if (!IsValid()) return false;
    int32 x = 0, z = 0;
    return CellOf(point, x, z) && walkable_[static_cast<size_t>(Index(x, z))] != 0;
}

int32 NavGrid::NearestWalkable(int32 x, int32 z, int32 radius) const {
    int32 best = -1;
    int32 best_distance = std::numeric_limits<int32>::max();
    for (int32 dz = -radius; dz <= radius; ++dz) {
        for (int32 dx = -radius; dx <= radius; ++dx) {
            const int32 cx = x + dx;
            const int32 cz = z + dz;
            if (cx < 0 || cz < 0 || cx >= width_ || cz >= depth_) continue;
            const int32 i = Index(cx, cz);
            const int32 distance = dx * dx + dz * dz;
            if (walkable_[static_cast<size_t>(i)] && distance < best_distance) {
                best = i;
                best_distance = distance;
            }
        }
    }
    return best;
}

bool NavGrid::IsLineWalkable(const Math::Vector3& from, const Math::Vector3& to) const {
    if (!IsValid()) return false;
    const float32 length = std::sqrt(FlatDistanceSquared(from, to));
    const int32 steps = std::max(1, static_cast<int32>(std::ceil(length / (cell_ * 0.5f))));
    int32 x = 0, z = 0;
    CellOf(from, x, z);
    float32 previous = ground_[static_cast<size_t>(Index(x, z))];
    for (int32 s = 1; s <= steps; ++s) {
        const float32 t = static_cast<float32>(s) / static_cast<float32>(steps);
        if (!CellOf(from + (to - from) * t, x, z)) return false;
        const size_t i = static_cast<size_t>(Index(x, z));
        if (!walkable_[i] || std::abs(ground_[i] - previous) > kMaxStep) return false;
        previous = ground_[i];
    }
    return true;
}

bool NavGrid::FindPath(const Math::Vector3& from, const Math::Vector3& to, Array<Math::Vector3>& path) const {
    path.clear();
    if (!IsValid()) return false;
    int32 sx = 0, sz = 0, gx = 0, gz = 0;
    CellOf(from, sx, sz);
    CellOf(to, gx, gz);
    // The bot may stand on a blocked cell (wall edge) and goals may lie next to walls
    const int32 start = walkable_[static_cast<size_t>(Index(sx, sz))] ? Index(sx, sz) : NearestWalkable(sx, sz, 2);
    const int32 goal = walkable_[static_cast<size_t>(Index(gx, gz))] ? Index(gx, gz) : NearestWalkable(gx, gz, 6);
    if (start < 0 || goal < 0) return false;

    const size_t count = walkable_.size();
    Array<float32> cost(count, std::numeric_limits<float32>::max());
    Array<int32> parent(count, -1);
    Array<uint8> closed(count, 0);
    using Entry = std::pair<float32, int32>;
    std::priority_queue<Entry, Array<Entry>, std::greater<Entry>> open;

    const int32 goal_x = goal % width_;
    const int32 goal_z = goal / width_;
    const auto heuristic = [&](int32 i) {
        const float32 dx = static_cast<float32>(std::abs(i % width_ - goal_x));
        const float32 dz = static_cast<float32>(std::abs(i / width_ - goal_z));
        return std::max(dx, dz) + 0.41421356f * std::min(dx, dz);
    };
    cost[static_cast<size_t>(start)] = 0.0f;
    open.push({heuristic(start), start});

    static constexpr int32 kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static constexpr int32 kDz[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    bool found = false;
    while (!open.empty()) {
        const int32 current = open.top().second;
        open.pop();
        const size_t c = static_cast<size_t>(current);
        if (closed[c]) continue;
        closed[c] = 1;
        if (current == goal) {
            found = true;
            break;
        }
        const int32 cx = current % width_;
        const int32 cz = current / width_;
        for (int32 n = 0; n < 8; ++n) {
            const int32 nx = cx + kDx[n];
            const int32 nz = cz + kDz[n];
            if (nx < 0 || nz < 0 || nx >= width_ || nz >= depth_) continue;
            const size_t ni = static_cast<size_t>(Index(nx, nz));
            if (!walkable_[ni] || closed[ni] || std::abs(ground_[ni] - ground_[c]) > kMaxStep) continue;
            // Diagonals must not cut corners
            if (n >= 4 && (!walkable_[static_cast<size_t>(Index(nx, cz))] || !walkable_[static_cast<size_t>(Index(cx, nz))])) continue;
            const float32 next_cost = cost[c] + (n >= 4 ? 1.41421356f : 1.0f);
            if (next_cost < cost[ni]) {
                cost[ni] = next_cost;
                parent[ni] = current;
                open.push({next_cost + heuristic(static_cast<int32>(ni)), static_cast<int32>(ni)});
            }
        }
    }
    if (!found) return false;

    for (int32 i = goal; i >= 0 && i != start; i = parent[static_cast<size_t>(i)]) {
        path.push_back(CellCenter(i % width_, i / width_));
    }
    std::reverse(path.begin(), path.end());
    if (path.empty()) path.push_back(CellCenter(goal_x, goal_z));
    return true;
}

// ============================================================================
// BotDirector
// ============================================================================

void BotDirector::Build(ShiftWorld& world, const Level& level) {
    grid_.Build(world, level);
    refresh_counter_ = 0;
}

PlayerId BotDirector::AddBot(ShiftWorld& world, Team team) {
    const PlayerId id = world.AddPlayer(team);
    if (id == kInvalidPlayer) return id;
    Bot bot;
    bot.id = id;
    bot.yaw = world.GetSpawn(team).yaw;
    bots_.push_back(bot);
    return id;
}

void BotDirector::Update(ShiftWorld& world) {
    if (bots_.empty()) return;
    if (++refresh_counter_ >= kRefreshTicks) {
        refresh_counter_ = 0;
        grid_.Refresh(world);
    }
    for (Bot& bot : bots_) world.SetInput(bot.id, Think(world, bot));
}

const SimPlayer* BotDirector::FindTarget(ShiftWorld& world, const SimPlayer& self) const {
    const BotSettings& settings = world.GetConfig().bot;
    const Math::Vector3 eye = self.GetFeet() + Math::Vector3(0.0f, world.GetConfig().player.eye_height, 0.0f);
    const Bot* bot = nullptr;
    for (const Bot& b : bots_) {
        if (b.id == self.id) bot = &b;
    }
    const SimPlayer* best = nullptr;
    float32 best_distance = settings.view_range * settings.view_range;
    for (const auto& [id, other] : world.GetPlayers()) {
        if (other->team == self.team || !other->alive) continue;
        const Math::Vector3 chest = other->GetFeet() + Math::Vector3(0.0f, 1.25f, 0.0f);
        const Math::Vector3 delta = chest - eye;
        const float32 distance = delta.LengthSquared();
        if (distance >= best_distance) continue;
        // Field of view 140 degrees, except for the current target and enemies right next to the bot
        const bool tracked = bot && bot->target == id;
        if (!tracked && distance > 25.0f && bot) {
            const Math::Vector3 flat = Flat(delta).Normalized();
            if (Math::Vector3::Dot(flat, FlatForward(bot->yaw)) < 0.34f) continue;
        }
        if (world.GetPhysics().Raycast(eye, chest, WorldFilter()).hit) continue;
        best = other.get();
        best_distance = distance;
    }
    return best;
}

Math::Vector3 BotDirector::ChooseGoal(const ShiftWorld& world, const SimPlayer& self) const {
    if (world.HasCore()) {
        const CoreState& core = world.GetCore();
        if (core.location != CoreLocation::Carried) return core.position;
        if (core.carrier == self.id) return world.GetExit(self.team).center;
        if (const SimPlayer* carrier = world.GetPlayer(core.carrier)) return carrier->GetFeet();
    }
    // No core (test room) or nothing else to do: hunt the nearest enemy
    const SimPlayer* nearest = nullptr;
    float32 best = std::numeric_limits<float32>::max();
    for (const auto& [id, other] : world.GetPlayers()) {
        if (other->team == self.team || !other->alive) continue;
        const float32 d = FlatDistanceSquared(other->GetFeet(), self.GetFeet());
        if (d < best) {
            best = d;
            nearest = other.get();
        }
    }
    return nearest ? nearest->GetFeet() : world.GetSpawn(Opponent(self.team)).position;
}

void BotDirector::FollowPath(ShiftWorld& world, Bot& bot, const SimPlayer& self, const Math::Vector3& goal,
                             Math::Vector3& direction) {
    const Math::Vector3 feet = self.GetFeet();
    bot.repath_timer -= world.GetTickInterval();
    if (bot.repath_timer <= 0.0f || bot.next >= bot.path.size() || FlatDistanceSquared(goal, bot.path_goal) > 1.0f) {
        grid_.FindPath(feet, goal, bot.path);
        bot.next = 0;
        bot.path_goal = goal;
        bot.repath_timer = 0.6f;
    }
    while (bot.next < bot.path.size() && FlatDistanceSquared(feet, bot.path[bot.next]) < 0.35f * 0.35f) ++bot.next;
    // Cut corners where the straight line is free
    for (size_t k = bot.next + 1; k < bot.path.size() && k < bot.next + 10; ++k) {
        if (!grid_.IsLineWalkable(feet, bot.path[k])) break;
        bot.next = k;
    }
    const Math::Vector3 waypoint = bot.next < bot.path.size() ? bot.path[bot.next] : goal;
    const Math::Vector3 flat = Flat(waypoint - feet);
    direction = flat.LengthSquared() > 1.0e-4f ? flat.Normalized() : Math::Vector3::Zero;
}

PlayerInput BotDirector::Think(ShiftWorld& world, Bot& bot) {
    const ShiftConfig& config = world.GetConfig();
    const BotSettings& settings = config.bot;
    const float32 dt = world.GetTickInterval();
    PlayerInput input;
    input.tick = world.GetTick() + 1;
    input.loadout = static_cast<uint8>(settings.loadout);

    const SimPlayer* self = world.GetPlayer(bot.id);
    if (!self || !self->alive) {
        bot.path.clear();
        bot.target = kInvalidPlayer;
        bot.seen_time = 0.0f;
        bot.spawned = false;
        input.yaw = bot.yaw;
        return input;
    }
    if (!bot.spawned) {
        bot.spawned = true;
        bot.yaw = world.GetSpawn(self->team).yaw;
        bot.pitch = 0.0f;
        bot.stuck_check = self->GetFeet();
        bot.stuck_timer = 0.0f;
    }

    const Math::Vector3 feet = self->GetFeet();
    const Math::Vector3 eye = feet + Math::Vector3(0.0f, config.player.eye_height, 0.0f);
    const bool carrier = world.IsCarrier(self->id);
    const uint32 tick = world.GetTick();

    // Perception
    const SimPlayer* target = FindTarget(world, *self);
    if (target) {
        if (target->id != bot.target) bot.seen_time = 0.0f;
        bot.target = target->id;
        bot.seen_time += dt;
    } else {
        bot.target = kInvalidPlayer;
        bot.seen_time = 0.0f;
    }
    const bool engaging = target && bot.seen_time >= settings.reaction_time;

    // Movement
    Math::Vector3 direction = Math::Vector3::Zero;
    const CoreState& core = world.GetCore();
    const float32 reach = config.round.core_pickup_range * 0.7f;
    const bool at_core = world.HasCore() && core.location != CoreLocation::Carried &&
                         FlatDistanceSquared(core.position, feet) < reach * reach;
    if (at_core) {
        input.buttons |= kButtonInteract;
    } else {
        const Math::Vector3 goal = ChooseGoal(world, *self);
        const bool hunting = !world.HasCore() || (core.location == CoreLocation::Carried && !carrier &&
                                                  world.GetPlayer(core.carrier) &&
                                                  world.GetPlayer(core.carrier)->team != self->team);
        if (target && hunting && FlatDistanceSquared(target->GetFeet(), feet) < 7.0f * 7.0f) {
            // Close to the enemy: strafe instead of running into him
            const float32 side = (tick / 45u) % 2u == 0u ? 1.0f : -1.0f;
            direction = FlatRight(bot.yaw) * side;
            if (!grid_.IsWalkable(feet + direction * 0.8f)) direction = -direction;
        } else {
            FollowPath(world, bot, *self, goal, direction);
        }
    }

    // Aim
    float32 desired_yaw = bot.yaw;
    float32 desired_pitch = 0.0f;
    if (target && bot.seen_time >= settings.reaction_time * 0.5f) {
        bot.error_timer -= dt;
        if (bot.error_timer <= 0.0f) {
            bot.error_timer = 0.4f;
            bot.error_yaw = Noise(tick, bot.id, 1u) * settings.aim_error;
            bot.error_pitch = Noise(tick, bot.id, 2u) * settings.aim_error * 0.5f;
        }
        const Math::Vector3 delta = target->GetFeet() + Math::Vector3(0.0f, 1.25f, 0.0f) - eye;
        desired_yaw = YawOf(delta) + bot.error_yaw;
        desired_pitch = std::atan2(delta.y, std::sqrt(delta.x * delta.x + delta.z * delta.z)) * kRadToDeg + bot.error_pitch;
    } else if (direction.LengthSquared() > 0.0f) {
        desired_yaw = YawOf(direction);
    }
    const float32 turn = settings.turn_speed * dt;
    bot.yaw = WrapAngle(bot.yaw + std::clamp(WrapAngle(desired_yaw - bot.yaw), -turn, turn));
    bot.pitch = std::clamp(bot.pitch + std::clamp(desired_pitch - bot.pitch, -turn, turn), -80.0f, 80.0f);
    input.yaw = bot.yaw;
    input.pitch = bot.pitch;
    input.move_y = Math::Vector3::Dot(direction, FlatForward(bot.yaw));
    input.move_x = Math::Vector3::Dot(direction, FlatRight(bot.yaw));

    // Weapon
    const WeaponState& weapon = carrier ? self->pistol : self->ActiveWeapon();
    bool trigger = false;
    if (engaging && weapon.def) {
        const Math::Vector3 delta = target->GetFeet() + Math::Vector3(0.0f, 1.0f, 0.0f) - eye;
        const float32 distance = std::max(0.5f, delta.Length());
        const float32 off = std::acos(std::clamp(Math::Vector3::Dot(LookDirection(bot.yaw, bot.pitch), delta / distance), -1.0f, 1.0f));
        const float32 tolerance = std::max(settings.aim_error * kDegToRad * 1.5f, std::atan2(0.6f, distance));
        // Semi-automatic weapons need a fresh click per shot
        if (off <= tolerance) trigger = weapon.def->automatic || !bot.trigger;
    }
    bot.trigger = trigger;
    if (trigger) input.buttons |= kButtonFire;
    if (weapon.def && !weapon.IsReloading() &&
        (weapon.ammo == 0 || (!target && weapon.ammo < weapon.def->magazine / 2))) {
        input.buttons |= kButtonReload;
    }
    if (!carrier && self->primary.def && self->active_slot != WeaponSlot::Primary) input.buttons |= kButtonSlotPrimary;

    // Stuck on an edge or a closing wall: jump and plan again
    if (direction.LengthSquared() > 0.0f && !world.IsFrozen()) {
        bot.stuck_timer += dt;
        if (bot.stuck_timer >= 1.0f) {
            if (FlatDistanceSquared(feet, bot.stuck_check) < 0.4f * 0.4f) {
                bot.jump = true;
                bot.repath_timer = 0.0f;
            }
            bot.stuck_timer = 0.0f;
            bot.stuck_check = feet;
        }
    } else {
        bot.stuck_timer = 0.0f;
        bot.stuck_check = feet;
    }
    if (bot.jump) {
        input.buttons |= kButtonJump;
        bot.jump = false;
    }
    return input;
}

} // namespace Shift

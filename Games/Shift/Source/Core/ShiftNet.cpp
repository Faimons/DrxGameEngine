/**
 * SHIFT - Network play
 */

#include "ShiftNet.h"

#include "Core/Public/Log.h"

#include <algorithm>
#include <cmath>

namespace Shift {

using Network::Delivery;
using Network::NetReader;
using Network::NetWriter;

namespace {

enum class MessageType : uint8 { Welcome = 1, Input = 2, Snapshot = 3, Events = 4 };

constexpr uint32 kInputRedundancy = 8;     // every input packet repeats the last inputs (loss)
constexpr size_t kEventsPerMessage = 14;
constexpr uint16 kOneShotButtons = kButtonJump | kButtonReload | kButtonSlotPrimary | kButtonSlotPistol;

void WriteVec3(NetWriter& w, const Math::Vector3& v) {
    w.WriteF32(v.x);
    w.WriteF32(v.y);
    w.WriteF32(v.z);
}

bool ReadVec3(NetReader& r, Math::Vector3& v) {
    return r.ReadF32(v.x) && r.ReadF32(v.y) && r.ReadF32(v.z);
}

bool ReadTeam(NetReader& r, Team& team) {
    uint8 v = 0;
    if (!r.ReadU8(v) || v > 1) return false;
    team = static_cast<Team>(v);
    return true;
}

void WriteInput(NetWriter& w, const PlayerInput& in) {
    w.WriteU32(in.tick);
    w.WriteF32(in.move_x);
    w.WriteF32(in.move_y);
    w.WriteF32(in.yaw);
    w.WriteF32(in.pitch);
    w.WriteU16(in.buttons);
    w.WriteF32(in.view_tick);
    w.WriteU8(in.loadout);
}

bool ReadInput(NetReader& r, PlayerInput& in) {
    if (!(r.ReadU32(in.tick) && r.ReadF32(in.move_x) && r.ReadF32(in.move_y) && r.ReadF32(in.yaw) &&
          r.ReadF32(in.pitch) && r.ReadU16(in.buttons) && r.ReadF32(in.view_tick) && r.ReadU8(in.loadout))) {
        return false;
    }
    // Never trust client numbers
    if (!std::isfinite(in.move_x) || !std::isfinite(in.move_y) || !std::isfinite(in.yaw) || !std::isfinite(in.pitch) ||
        !std::isfinite(in.view_tick)) {
        return false;
    }
    in.move_x = std::clamp(in.move_x, -1.0f, 1.0f);
    in.move_y = std::clamp(in.move_y, -1.0f, 1.0f);
    in.pitch = std::clamp(in.pitch, -89.0f, 89.0f);
    in.yaw = std::fmod(in.yaw, 360.0f);
    if (in.loadout >= kLoadoutCount) in.loadout = kNoLoadout;
    return true;
}

void WritePlayer(NetWriter& w, const SimPlayer& p) {
    w.WriteU32(p.id);
    w.WriteU8(static_cast<uint8>(p.team));
    w.WriteU8(static_cast<uint8>((p.alive ? 1 : 0) | (p.IsGrounded() ? 2 : 0) | (p.pistol.trigger_down ? 4 : 0) |
                                 (p.pistol.shot_queued ? 8 : 0)));
    const Physics::CharacterState& state = p.controller->GetState();
    WriteVec3(w, state.position);
    WriteVec3(w, state.velocity);
    w.WriteF32(p.yaw);
    w.WriteF32(p.pitch);
    w.WriteF32(p.health);
    w.WriteF32(p.respawn_timer);
    w.WriteF32(p.protection_timer);
    w.WriteF32(p.pickup_progress);
    w.WriteI32(p.pistol.ammo);
    w.WriteF32(p.pistol.cooldown);
    w.WriteF32(p.pistol.reload_timer);
    w.WriteU8(static_cast<uint8>(p.active_slot));
    w.WriteU8(p.loadout);
    w.WriteU8(p.primary_index);
    w.WriteI32(p.primary.ammo);
    w.WriteF32(p.primary.cooldown);
    w.WriteF32(p.primary.reload_timer);
    w.WriteU8(static_cast<uint8>((p.primary.trigger_down ? 1 : 0) | (p.primary.shot_queued ? 2 : 0)));
}

bool ReadPlayer(NetReader& r, RemotePlayerState& p) {
    uint8 flags = 0;
    if (!(r.ReadU32(p.id) && ReadTeam(r, p.team) && r.ReadU8(flags) && ReadVec3(r, p.feet) && ReadVec3(r, p.velocity) &&
          r.ReadF32(p.yaw) && r.ReadF32(p.pitch) && r.ReadF32(p.health) && r.ReadF32(p.respawn_timer) &&
          r.ReadF32(p.protection_timer) && r.ReadF32(p.pickup_progress) && r.ReadI32(p.ammo) && r.ReadF32(p.cooldown) &&
          r.ReadF32(p.reload_timer))) {
        return false;
    }
    uint8 primary_flags = 0;
    if (!(r.ReadU8(p.slot) && p.slot <= 1 && r.ReadU8(p.loadout) && r.ReadU8(p.primary_index) &&
          r.ReadI32(p.primary_ammo) && r.ReadF32(p.primary_cooldown) && r.ReadF32(p.primary_reload) &&
          r.ReadU8(primary_flags))) {
        return false;
    }
    p.primary_trigger_down = (primary_flags & 1) != 0;
    p.primary_shot_queued = (primary_flags & 2) != 0;
    p.alive = (flags & 1) != 0;
    p.grounded = (flags & 2) != 0;
    p.trigger_down = (flags & 4) != 0;
    p.shot_queued = (flags & 8) != 0;
    return true;
}

void WriteEvent(NetWriter& w, const WorldEvent& e) {
    w.WriteU8(static_cast<uint8>(e.type));
    w.WriteU32(e.tick);
    w.WriteU32(e.source);
    w.WriteU32(e.target);
    WriteVec3(w, e.from);
    WriteVec3(w, e.to);
    WriteVec3(w, e.normal);
    w.WriteF32(e.amount);
    w.WriteU8(static_cast<uint8>(e.team));
    w.WriteU8(static_cast<uint8>((e.headshot ? 1 : 0) | (e.hit ? 2 : 0) | (e.crushed ? 4 : 0)));
    w.WriteU8(e.weapon);
}

bool ReadEvent(NetReader& r, WorldEvent& e) {
    uint8 type = 0;
    uint8 flags = 0;
    if (!(r.ReadU8(type) && type <= static_cast<uint8>(WorldEventType::MatchEnd) && r.ReadU32(e.tick) &&
          r.ReadU32(e.source) && r.ReadU32(e.target) && ReadVec3(r, e.from) && ReadVec3(r, e.to) &&
          ReadVec3(r, e.normal) && r.ReadF32(e.amount) && ReadTeam(r, e.team) && r.ReadU8(flags) &&
          r.ReadU8(e.weapon))) {
        return false;
    }
    e.type = static_cast<WorldEventType>(type);
    e.headshot = (flags & 1) != 0;
    e.hit = (flags & 2) != 0;
    e.crushed = (flags & 4) != 0;
    return true;
}

float32 LerpAngle(float32 a, float32 b, float32 t) {
    float32 d = std::fmod(b - a, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return a + d * t;
}

} // namespace

// ============================================================================
// Server
// ============================================================================

ShiftServer::~ShiftServer() {
    Stop();
}

bool ShiftServer::Start(ShiftWorld& world, const String& map, uint16 port,
                        const Network::ConditionerSettings& conditioner) {
    Stop();
    world_ = &world;
    map_ = map;
    net_.SetConditioner(conditioner);
    if (!net_.Start(port, 15)) {
        DRX_LOG_ERROR("SHIFT server: cannot open UDP port {}", port);
        return false;
    }
    return true;
}

void ShiftServer::Stop() {
    if (world_) {
        for (const auto& [client_id, client] : clients_) world_->RemovePlayer(client.player);
    }
    clients_.clear();
    net_.Stop();
}

Team ShiftServer::PickTeam() const {
    int32 count[2] = {0, 0};
    for (const auto& [id, player] : world_->GetPlayers()) {
        if (!player->is_dummy) ++count[static_cast<size_t>(player->team)];
    }
    return count[0] < count[1] ? Team::A : Team::B;
}

void ShiftServer::Receive(float64 now) {
    if (!world_) return;
    net_.Update(now);
    Network::NetEvent event;
    while (net_.PollEvent(event)) {
        switch (event.type) {
            case Network::NetEventType::Connected: {
                const PlayerId player = world_->AddPlayer(PickTeam());
                if (player == kInvalidPlayer) {
                    net_.Disconnect(event.client_id, "server full");
                    break;
                }
                clients_[event.client_id].player = player;
                Array<uint8> data;
                NetWriter w(data);
                w.WriteU8(static_cast<uint8>(MessageType::Welcome));
                w.WriteU32(player);
                w.WriteU8(static_cast<uint8>(world_->GetPlayer(player)->team));
                w.WriteString(map_);
                net_.Send(event.client_id, Delivery::Reliable, data.data(), data.size());
                DRX_LOG_INFO("SHIFT server: client {} joined as player {}", event.client_id, player);
                if (on_player_change) on_player_change(player, true);
                break;
            }
            case Network::NetEventType::Disconnected: {
                const auto it = clients_.find(event.client_id);
                if (it == clients_.end()) break;
                const PlayerId player = it->second.player;
                world_->RemovePlayer(player);
                clients_.erase(it);
                DRX_LOG_INFO("SHIFT server: player {} left ({})", player, event.reason);
                if (on_player_change) on_player_change(player, false);
                break;
            }
            case Network::NetEventType::Message:
                HandleMessage(event.client_id, event.data);
                break;
            case Network::NetEventType::ConnectionFailed:
                break;
        }
    }
    ApplyInputs();
}

void ShiftServer::HandleMessage(uint32 client_id, const Array<uint8>& data) {
    const auto it = clients_.find(client_id);
    if (it == clients_.end()) return;
    RemoteClient& client = it->second;
    NetReader r(data.data(), data.size());
    uint8 type = 0;
    if (!r.ReadU8(type) || type != static_cast<uint8>(MessageType::Input)) return;
    uint8 count = 0;
    if (!r.ReadU8(count) || count > kInputRedundancy) return;
    const uint32 now_tick = world_->GetTick();
    for (uint8 i = 0; i < count; ++i) {
        PlayerInput in;
        if (!ReadInput(r, in)) return;
        // Ignore inputs already simulated and inputs absurdly far ahead
        if (in.tick <= client.last_applied || in.tick > now_tick + 120) continue;
        client.inputs[in.tick] = in;
    }
    while (client.inputs.size() > 128) client.inputs.erase(client.inputs.begin());
}

void ShiftServer::ApplyInputs() {
    const uint32 tick = world_->GetTick() + 1;
    for (auto& [client_id, client] : clients_) {
        PlayerInput input;
        const auto found = client.inputs.find(tick);
        if (found != client.inputs.end()) {
            input = found->second;
            client.last = input;
        } else {
            // Late or lost: keep moving and aiming as before, never repeat one-shot actions
            input = client.last;
            input.tick = tick;
            input.buttons &= static_cast<uint16>(~kOneShotButtons);
            if (client.last_applied > 0) ++stats_.missing_inputs;
        }
        client.inputs.erase(client.inputs.begin(), client.inputs.upper_bound(tick));
        client.last_applied = tick;
        world_->SetInput(client.player, input);
    }
}

void ShiftServer::Send(const Array<WorldEvent>& events, float64 now) {
    if (!world_) return;
    // Events (reliable, in order)
    for (size_t first = 0; first < events.size(); first += kEventsPerMessage) {
        const size_t count = std::min(kEventsPerMessage, events.size() - first);
        Array<uint8> data;
        NetWriter w(data);
        w.WriteU8(static_cast<uint8>(MessageType::Events));
        w.WriteU8(static_cast<uint8>(count));
        for (size_t i = 0; i < count; ++i) WriteEvent(w, events[first + i]);
        net_.Broadcast(Delivery::Reliable, data.data(), data.size());
    }

    // Snapshot (unreliable, every tick)
    Array<uint8> data;
    NetWriter w(data);
    w.WriteU8(static_cast<uint8>(MessageType::Snapshot));
    w.WriteU32(world_->GetTick());
    w.WriteU32(world_->GetRoundStartTick());
    w.WriteU32(world_->GetRoundSeed());
    const MatchState& match = world_->GetMatch();
    w.WriteU8(static_cast<uint8>(match.phase));
    w.WriteI32(match.round);
    w.WriteI32(match.score[0]);
    w.WriteI32(match.score[1]);
    w.WriteU32(match.phase_end_tick);
    w.WriteBool(match.round_has_winner);
    w.WriteU8(static_cast<uint8>(match.round_winner));
    w.WriteU8(static_cast<uint8>(match.match_winner));
    w.WriteU32(match.match_seed);
    const CoreState& core = world_->GetCore();
    w.WriteU8(static_cast<uint8>(core.location));
    WriteVec3(w, core.position);
    w.WriteU32(core.carrier);
    w.WriteF32(core.idle_time);
    w.WriteBool(core.has_last_team);
    w.WriteU8(static_cast<uint8>(core.last_team));
    const auto& players = world_->GetPlayers();
    w.WriteU8(static_cast<uint8>(std::min<size_t>(players.size(), 16)));
    size_t written = 0;
    for (const auto& [id, player] : players) {
        if (written++ >= 16) break;
        WritePlayer(w, *player);
    }
    net_.Broadcast(Delivery::Unreliable, data.data(), data.size());
    net_.Update(now);

    float32 rtt = 0.0f;
    for (const uint32 id : net_.GetClientIds()) {
        if (const Network::ConnectionStats* s = net_.GetStats(id)) rtt = std::max(rtt, s->rtt_ms);
    }
    stats_.rtt_ms = rtt;
}

// ============================================================================
// Client
// ============================================================================

ShiftClient::~ShiftClient() {
    if (net_.GetState() != Network::ClientState::Disconnected) net_.Disconnect(0.0);
    world_.Shutdown();
}

bool ShiftClient::Connect(const ShiftConfig& config, const Network::Address& server, float64 now,
                          LevelFactory level_factory, const Network::ConditionerSettings& conditioner) {
    config_ = config;
    level_factory_ = std::move(level_factory);
    welcomed_ = ready_ = failed_ = false;
    has_clock_ = false;
    predicted_tick_ = 0;
    frames_.Clear();
    remotes_.clear();
    latest_remotes_.clear();
    net_.SetConditioner(conditioner);
    if (!net_.Connect(server, now)) {
        failed_ = true;
        fail_reason_ = "Verbindung nicht moeglich";
        return false;
    }
    return true;
}

void ShiftClient::Disconnect(float64 now) {
    net_.Disconnect(now);
    ready_ = false;
}

void ShiftClient::Receive(float64 now) {
    net_.Update(now);
    Network::NetEvent event;
    while (net_.PollEvent(event)) {
        switch (event.type) {
            case Network::NetEventType::Message:
                HandleMessage(event.data, now);
                break;
            case Network::NetEventType::ConnectionFailed:
            case Network::NetEventType::Disconnected:
                failed_ = true;
                ready_ = false;
                fail_reason_ = event.reason.empty() ? String("Verbindung getrennt") : event.reason;
                DRX_LOG_WARNING("SHIFT client: {}", fail_reason_);
                break;
            case Network::NetEventType::Connected:
                DRX_LOG_INFO("SHIFT client: connected");
                break;
        }
    }
    stats_.rtt_ms = net_.GetStats().rtt_ms;
}

void ShiftClient::Flush(float64 now) {
    net_.Update(now);
}

void ShiftClient::HandleMessage(const Array<uint8>& data, float64 now) {
    NetReader r(data.data(), data.size());
    uint8 type = 0;
    if (!r.ReadU8(type)) return;
    switch (static_cast<MessageType>(type)) {
        case MessageType::Welcome: HandleWelcome(r); break;
        case MessageType::Snapshot: if (welcomed_) HandleSnapshot(r, now); break;
        case MessageType::Events: if (welcomed_) HandleEvents(r); break;
        case MessageType::Input: break;
    }
}

void ShiftClient::HandleWelcome(NetReader& r) {
    PlayerId id = kInvalidPlayer;
    Team team = Team::A;
    String map;
    if (!(r.ReadU32(id) && ReadTeam(r, team) && r.ReadString(map, 64))) return;

    world_.Shutdown();
    if (!world_.Initialize(config_, false)) {
        failed_ = true;
        fail_reason_ = "Physik konnte nicht starten";
        return;
    }
    level_ = level_factory_(map);
    level_.Apply(world_);
    local_id_ = world_.AddPlayer(team, false, nullptr, id);
    local_team_ = team;
    map_ = map;
    welcomed_ = local_id_ != kInvalidPlayer;
    DRX_LOG_INFO("SHIFT client: joined as player {} (team {}) on '{}'", id, team == Team::A ? "A" : "B", map);
}

void ShiftClient::HandleSnapshot(NetReader& r, float64 now) {
    uint32 tick = 0, round_start = 0, round_seed = 0;
    MatchState match;
    CoreState core;
    uint8 phase = 0, location = 0, count = 0;
    if (!(r.ReadU32(tick) && r.ReadU32(round_start) && r.ReadU32(round_seed) && r.ReadU8(phase) && phase <= 3 &&
          r.ReadI32(match.round) && r.ReadI32(match.score[0]) && r.ReadI32(match.score[1]) &&
          r.ReadU32(match.phase_end_tick) && r.ReadBool(match.round_has_winner) && ReadTeam(r, match.round_winner) &&
          ReadTeam(r, match.match_winner) && r.ReadU32(match.match_seed) && r.ReadU8(location) && location <= 2 &&
          ReadVec3(r, core.position) && r.ReadU32(core.carrier) && r.ReadF32(core.idle_time) &&
          r.ReadBool(core.has_last_team) && ReadTeam(r, core.last_team) && r.ReadU8(count))) {
        return;
    }
    match.phase = static_cast<RoundPhase>(phase);
    core.location = static_cast<CoreLocation>(location);
    Array<RemotePlayerState> players(count);
    for (RemotePlayerState& p : players) {
        if (!ReadPlayer(r, p)) return;
    }
    if (ready_ && tick <= latest_server_tick_) return;   // late duplicate or reordered
    latest_server_tick_ = tick;

    // Clock: server tick = now / dt + offset; the fastest arrivals give the best estimate
    const float64 dt = config_.GetTickInterval();
    const float64 sample = static_cast<float64>(tick) - now / dt;
    if (!has_clock_ || sample > clock_offset_) clock_offset_ = sample;
    else clock_offset_ += (sample - clock_offset_) * 0.02;
    has_clock_ = true;

    if (round_start != world_.GetRoundStartTick() || round_seed != world_.GetRoundSeed()) {
        world_.SetRoundClock(round_start, round_seed);
    }
    world_.SetMatchState(match);
    world_.SetCoreState(core);

    Array<PlayerId> seen;
    for (const RemotePlayerState& p : players) {
        seen.push_back(p.id);
        if (p.id == local_id_) {
            Reconcile(p, tick);
            continue;
        }
        remotes_[p.id].Store(tick, p);
        latest_remotes_[p.id] = p;
    }
    for (auto it = latest_remotes_.begin(); it != latest_remotes_.end();) {
        if (std::find(seen.begin(), seen.end(), it->first) == seen.end()) {
            remotes_.erase(it->first);
            it = latest_remotes_.erase(it);
        } else {
            ++it;
        }
    }
    ready_ = true;
}

void ShiftClient::HandleEvents(NetReader& r) {
    uint8 count = 0;
    if (!r.ReadU8(count)) return;
    for (uint8 i = 0; i < count; ++i) {
        WorldEvent e;
        if (!ReadEvent(r, e)) return;
        // The local player's shots, clicks, reloads and respawn are already shown by the prediction
        const bool predicted = (e.source == local_id_ &&
                                (e.type == WorldEventType::Shot || e.type == WorldEventType::EmptyClick ||
                                 e.type == WorldEventType::ReloadStart || e.type == WorldEventType::ReloadDone)) ||
                               (e.type == WorldEventType::Respawn && e.target == local_id_);
        if (!predicted) events_.push_back(e);
    }
}

void ShiftClient::Reconcile(const RemotePlayerState& server, uint32 server_tick) {
    SimPlayer* me = world_.GetPlayer(local_id_);
    if (!me) return;
    me->health = server.health;   // damage is decided by the server only

    const PredictedFrame* frame = frames_.Find(server_tick);
    const bool ahead = server_tick <= predicted_tick_;
    if (ahead && frame && frame->alive == server.alive && frame->ammo == server.ammo &&
        frame->reloading == (server.reload_timer > 0.0f) && frame->primary_ammo == server.primary_ammo &&
        frame->slot == server.slot && me->primary_index == server.primary_index &&
        (frame->state.position - server.feet).Length() < 0.03f) {
        return;   // prediction was right
    }
    if (ahead && predicted_tick_ > 0) ++stats_.corrections;

    Physics::CharacterState state;
    state.position = server.feet;
    state.velocity = server.velocity;
    state.grounded = server.grounded;
    me->controller->SetState(state);
    me->controller->SetEnabled(server.alive);
    me->alive = server.alive;
    me->respawn_timer = server.respawn_timer;
    me->protection_timer = server.protection_timer;
    me->pickup_progress = server.pickup_progress;
    me->pistol.ammo = server.ammo;
    me->pistol.cooldown = server.cooldown;
    me->pistol.reload_timer = server.reload_timer;
    me->pistol.trigger_down = server.trigger_down;
    me->pistol.shot_queued = server.shot_queued;
    if (server.primary_index != me->primary_index) world_.EquipLoadout(*me, server.primary_index);
    me->loadout = server.loadout;
    me->primary.ammo = server.primary_ammo;
    me->primary.cooldown = server.primary_cooldown;
    me->primary.reload_timer = server.primary_reload;
    me->primary.trigger_down = server.primary_trigger_down;
    me->primary.shot_queued = server.primary_shot_queued;
    me->active_slot = server.slot == 0 && me->primary.def ? WeaponSlot::Primary : WeaponSlot::Pistol;

    if (!ahead) {
        // The server is ahead of the prediction (start or a long stall): continue from its state
        predicted_tick_ = server_tick;
        world_.SetTick(server_tick);
        frames_.Clear();
        return;
    }

    // Re-simulate the inputs the server has not processed yet
    world_.SetEventsEnabled(false);
    world_.SetTick(server_tick);
    for (uint32 t = server_tick + 1; t <= predicted_tick_; ++t) {
        PredictedFrame* f = frames_.Find(t);
        if (!f) break;
        world_.SetInput(local_id_, f->input);
        world_.StepSingle(local_id_, t);
        f->state = me->controller->GetState();
        f->alive = me->alive;
        f->ammo = me->pistol.ammo;
        f->reloading = me->pistol.IsReloading();
        f->primary_ammo = me->primary.ammo;
        f->slot = static_cast<uint8>(me->active_slot);
    }
    world_.SetEventsEnabled(true);
    world_.SetTick(predicted_tick_);
}

void ShiftClient::PredictStep(const PlayerInput& input) {
    const uint32 tick = predicted_tick_ + 1;
    PlayerInput in = input;
    in.tick = tick;
    world_.SetInput(local_id_, in);
    world_.StepSingle(local_id_, tick);
    predicted_tick_ = tick;

    const SimPlayer* me = world_.GetPlayer(local_id_);
    PredictedFrame frame;
    frame.input = in;
    frame.state = me->controller->GetState();
    frame.alive = me->alive;
    frame.ammo = me->pistol.ammo;
    frame.reloading = me->pistol.IsReloading();
    frame.primary_ammo = me->primary.ammo;
    frame.slot = static_cast<uint8>(me->active_slot);
    frames_.Store(tick, frame);

    unsent_.push_back(in);
    if (unsent_.size() > kInputRedundancy) unsent_.erase(unsent_.begin());
    for (const WorldEvent& e : world_.TakeEvents()) events_.push_back(e);
}

void ShiftClient::Tick(const PlayerInput& input, float64 now) {
    if (!ready_ || !net_.IsConnected()) return;

    // Inputs must reach the server before it simulates their tick: run ahead by the round trip
    const float64 dt = config_.GetTickInterval();
    const float64 rtt_ticks = static_cast<float64>(net_.GetStats().rtt_ms) * 0.001 / dt;
    const auto target = static_cast<int64>(std::ceil(GetServerTick(now) + rtt_ticks + 2.0));
    const int64 drift = target - static_cast<int64>(predicted_tick_);

    if (drift > 15 || drift < -15) {
        // Far off (start, stall): jump to the target and let the next snapshot correct the state
        predicted_tick_ = static_cast<uint32>(std::max<int64>(target - 1, 0));
        world_.SetTick(predicted_tick_);
        frames_.Clear();
    }
    uint32 steps = 1;
    if (drift > 2) steps = 2;          // behind the server clock: catch up
    else if (drift < 0) steps = 0;     // ahead: wait a tick

    PlayerInput in = input;
    in.view_tick = static_cast<float32>(GetRenderTick(now));
    for (uint32 i = 0; i < steps; ++i) PredictStep(in);
    if (steps > 0) SendInputs();
}

void ShiftClient::SendInputs() {
    Array<uint8> data;
    NetWriter w(data);
    w.WriteU8(static_cast<uint8>(MessageType::Input));
    w.WriteU8(static_cast<uint8>(unsent_.size()));
    for (const PlayerInput& in : unsent_) WriteInput(w, in);
    net_.Send(Delivery::Unreliable, data.data(), data.size());
}

float64 ShiftClient::GetServerTick(float64 now) const {
    if (!has_clock_) return static_cast<float64>(latest_server_tick_);
    return now / static_cast<float64>(config_.GetTickInterval()) + clock_offset_;
}

float64 ShiftClient::GetRenderTick(float64 now) const {
    const float64 delay = static_cast<float64>(config_.net.interpolation_delay_ms) * 0.001 /
                          static_cast<float64>(config_.GetTickInterval());
    return std::max(0.0, GetServerTick(now) - delay);
}

bool ShiftClient::GetRemotePlayer(PlayerId id, float64 render_tick, RemotePlayerState& out) const {
    const auto history = remotes_.find(id);
    if (history == remotes_.end()) return false;
    const RemotePlayerState* from = nullptr;
    const RemotePlayerState* to = nullptr;
    float32 alpha = 0.0f;
    if (!history->second.FindBracket(render_tick, from, to, alpha)) {
        const RemotePlayerState* latest = GetLatestRemote(id);
        if (!latest) return false;
        out = *latest;
        return true;
    }
    out = *to;
    if (from->alive && to->alive && (to->feet - from->feet).LengthSquared() < 4.0f) {
        out.feet = from->feet + (to->feet - from->feet) * alpha;
        out.yaw = LerpAngle(from->yaw, to->yaw, alpha);
        out.pitch = from->pitch + (to->pitch - from->pitch) * alpha;
    } else if (alpha < 0.5f) {
        out = *from;
    }
    return true;
}

Array<PlayerId> ShiftClient::GetRemotePlayerIds() const {
    Array<PlayerId> ids;
    for (const auto& [id, state] : latest_remotes_) ids.push_back(id);
    return ids;
}

const RemotePlayerState* ShiftClient::GetLatestRemote(PlayerId id) const {
    const auto it = latest_remotes_.find(id);
    return it != latest_remotes_.end() ? &it->second : nullptr;
}

Array<WorldEvent> ShiftClient::TakeEvents() {
    Array<WorldEvent> events;
    events.swap(events_);
    return events;
}

} // namespace Shift

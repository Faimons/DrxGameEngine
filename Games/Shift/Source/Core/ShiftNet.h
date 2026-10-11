#pragma once

/**
 * SHIFT - Network play (blueprint section 9)
 *
 * Server (authority): runs the ShiftWorld, applies client inputs by tick, sends a snapshot every
 * tick (unreliable) and world events (reliable). Wall positions are not sent: snapshots carry the
 * round seed and start tick, clients compute the walls themselves.
 *
 * Client: predicts its own player in a local ShiftWorld (same level, same code), keeps its inputs
 * and predicted states per tick and re-simulates from the server state when they differ.
 * Other players are shown `interpolation_delay_ms` in the past; shots carry that view time so the
 * server checks hits against the positions the shooter saw (lag compensation).
 *
 * Both sides must use the same shift.json gameplay values.
 */

#include "ShiftLevel.h"
#include "ShiftWorld.h"

#include "Network/Public/NetworkSystem.h"
#include "Network/Public/TickHistory.h"

#include <functional>
#include <map>

namespace Shift {

using LevelFactory = std::function<Level(const String& map)>;

struct RemotePlayerState {
    PlayerId id = kInvalidPlayer;
    Team team = Team::A;
    bool alive = false;
    bool grounded = false;
    Math::Vector3 feet;
    Math::Vector3 velocity;
    float32 yaw = 0.0f;
    float32 pitch = 0.0f;
    float32 health = 0.0f;
    float32 respawn_timer = 0.0f;
    float32 protection_timer = 0.0f;
    float32 pickup_progress = 0.0f;
    int32 ammo = 0;
    float32 cooldown = 0.0f;
    float32 reload_timer = 0.0f;
    bool trigger_down = false;
    bool shot_queued = false;
    // Primary weapon and the selected slot
    uint8 slot = 1;
    uint8 loadout = kNoLoadout;
    uint8 primary_index = kNoLoadout;
    int32 primary_ammo = 0;
    float32 primary_cooldown = 0.0f;
    float32 primary_reload = 0.0f;
    bool primary_trigger_down = false;
    bool primary_shot_queued = false;
};

struct NetStats {
    uint32 corrections = 0;       // client: re-simulations after a mismatch
    uint32 missing_inputs = 0;    // server: ticks without a client input (input repeated)
    float32 rtt_ms = 0.0f;
};

class ShiftServer {
public:
    ShiftServer() = default;
    ~ShiftServer();

    // The world must be initialised with the level `map`; `port` 0 picks a free port
    bool Start(ShiftWorld& world, const String& map, uint16 port, const Network::ConditionerSettings& conditioner = {});
    void Stop();
    bool IsRunning() const { return net_.IsRunning(); }
    uint16 GetPort() const { return net_.GetPort(); }

    // Per tick: Receive -> world.Step() -> Send(events)
    void Receive(float64 now);
    void Send(const Array<WorldEvent>& events, float64 now);

    uint32 GetClientCount() const { return static_cast<uint32>(clients_.size()); }
    const NetStats& GetStats() const { return stats_; }

    // Called when a player joins or leaves (the host restarts the match)
    std::function<void(PlayerId, bool joined)> on_player_change;

private:
    struct RemoteClient {
        PlayerId player = kInvalidPlayer;
        std::map<uint32, PlayerInput> inputs;   // by tick
        PlayerInput last;
        uint32 last_applied = 0;
    };
    void HandleMessage(uint32 client_id, const Array<uint8>& data);
    void ApplyInputs();
    Team PickTeam() const;

    ShiftWorld* world_ = nullptr;
    String map_;
    Network::NetServer net_;
    std::map<uint32, RemoteClient> clients_;
    NetStats stats_;
};

class ShiftClient {
public:
    ShiftClient() = default;
    ~ShiftClient();

    bool Connect(const ShiftConfig& config, const Network::Address& server, float64 now,
                 LevelFactory level_factory = [](const String& map) { return BuildLevel(map); },
                 const Network::ConditionerSettings& conditioner = {});
    void Disconnect(float64 now);

    // Per tick: Receive -> Tick(input) -> Flush
    void Receive(float64 now);
    // Predicts one (or, to catch up with the server clock, a few) ticks with this input
    void Tick(const PlayerInput& input, float64 now);
    void Flush(float64 now);

    bool IsConnected() const { return net_.IsConnected(); }
    bool IsReady() const { return ready_; }
    bool HasFailed() const { return failed_; }
    const String& GetFailReason() const { return fail_reason_; }

    PlayerId GetLocalId() const { return local_id_; }
    const String& GetMap() const { return map_; }
    const Level& GetLevel() const { return level_; }
    ShiftWorld& GetWorld() { return world_; }
    const ShiftWorld& GetWorld() const { return world_; }

    // Server tick estimate (as the server was `latency` ago) and the time other players are shown at
    float64 GetServerTick(float64 now) const;
    float64 GetRenderTick(float64 now) const;
    uint32 GetLatestServerTick() const { return latest_server_tick_; }

    // Other players at the given server tick (interpolated); false when unknown
    bool GetRemotePlayer(PlayerId id, float64 render_tick, RemotePlayerState& out) const;
    Array<PlayerId> GetRemotePlayerIds() const;
    const RemotePlayerState* GetLatestRemote(PlayerId id) const;

    // Events from the server and from the local prediction, in presentation order
    Array<WorldEvent> TakeEvents();
    const NetStats& GetStats() const { return stats_; }

private:
    struct PredictedFrame {
        PlayerInput input;
        Physics::CharacterState state;
        bool alive = false;
        int32 ammo = 0;
        bool reloading = false;
        int32 primary_ammo = 0;
        uint8 slot = 1;
    };
    using RemoteHistory = Network::TickHistory<RemotePlayerState, 64>;

    void HandleMessage(const Array<uint8>& data, float64 now);
    void HandleWelcome(Network::NetReader& reader);
    void HandleSnapshot(Network::NetReader& reader, float64 now);
    void HandleEvents(Network::NetReader& reader);
    void Reconcile(const RemotePlayerState& server, uint32 server_tick);
    void PredictStep(const PlayerInput& input);
    void SendInputs();

    ShiftConfig config_;
    LevelFactory level_factory_;
    Network::NetClient net_;
    ShiftWorld world_;
    Level level_;
    String map_;
    PlayerId local_id_ = kInvalidPlayer;
    Team local_team_ = Team::A;
    bool welcomed_ = false;
    bool ready_ = false;
    bool failed_ = false;
    String fail_reason_;

    uint32 predicted_tick_ = 0;
    Network::TickHistory<PredictedFrame, 128> frames_;
    Array<PlayerInput> unsent_;   // recent inputs, resent for loss tolerance

    uint32 latest_server_tick_ = 0;
    float64 clock_offset_ = 0.0;   // server tick = now / dt + offset (as received)
    bool has_clock_ = false;
    std::map<PlayerId, RemoteHistory> remotes_;
    std::map<PlayerId, RemotePlayerState> latest_remotes_;
    Array<WorldEvent> events_;
    NetStats stats_;
};

} // namespace Shift

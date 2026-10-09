/**
 * DRX Engine - Network System
 * Client/server UDP transport for real-time games.
 *
 * - Single threaded: call Update() from the game loop (typically once per tick)
 * - Two delivery modes per message: Unreliable (latest state, inputs) and Reliable (ordered, resent)
 * - Connection handshake, keep-alive, timeouts, RTT and packet loss statistics
 * - Optional network conditioner (latency, jitter, loss, duplicates) for testing
 */

#pragma once

#include "Core/Public/CoreTypes.h"

#include <deque>
#include <map>
#include <random>

namespace DRX::Network {

// ============================================================================
// Addresses and sockets
// ============================================================================

struct Address {
    uint32 ip = 0;     // IPv4, host byte order
    uint16 port = 0;

    static Address FromIPv4(uint8 a, uint8 b, uint8 c, uint8 d, uint16 port);
    static Address Loopback(uint16 port) { return FromIPv4(127, 0, 0, 1, port); }
    // "host:port" or "a.b.c.d:port"; host names are resolved (IPv4)
    static bool Parse(const String& text, Address& out);

    String ToString() const;
    bool IsValid() const { return ip != 0 && port != 0; }
    bool operator==(const Address& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const Address& o) const { return !(*this == o); }
    bool operator<(const Address& o) const { return ip != o.ip ? ip < o.ip : port < o.port; }
};

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Port 0 picks a free port; the socket is non-blocking
    bool Open(uint16 port = 0);
    void Close();
    bool IsOpen() const;
    uint16 GetPort() const { return port_; }

    bool SendTo(const Address& to, const void* data, size_t size);
    // Returns the datagram size, 0 when nothing is pending, -1 on error
    int32 ReceiveFrom(Address& from, void* buffer, size_t capacity);

private:
    uint64 handle_ = ~0ull;
    uint16 port_ = 0;
};

/**
 * Simulates bad networks on the sending side (applied to every outgoing datagram)
 */
struct ConditionerSettings {
    float32 latency_ms = 0.0f;   // one-way delay added to every packet
    float32 jitter_ms = 0.0f;    // +- random extra delay
    float32 loss = 0.0f;         // 0..1 probability to drop
    float32 duplicate = 0.0f;    // 0..1 probability to send twice
    uint32 seed = 1;

    bool IsActive() const { return latency_ms > 0.0f || jitter_ms > 0.0f || loss > 0.0f || duplicate > 0.0f; }
};

// ============================================================================
// Serialization (little endian, bounds checked)
// ============================================================================

class NetWriter {
public:
    explicit NetWriter(Array<uint8>& buffer) : buffer_(buffer) {}

    void WriteU8(uint8 v) { buffer_.push_back(v); }
    void WriteU16(uint16 v);
    void WriteU32(uint32 v);
    void WriteU64(uint64 v);
    void WriteI32(int32 v) { WriteU32(static_cast<uint32>(v)); }
    void WriteF32(float32 v);
    void WriteBool(bool v) { WriteU8(v ? 1 : 0); }
    void WriteString(const String& v);   // u16 length + bytes
    void WriteBytes(const void* data, size_t size);
    // Float in [min, max] stored in `bits` bits (rounded); for positions, angles, ...
    void WriteQuantized(float32 v, float32 min, float32 max, uint32 bits);

    size_t GetSize() const { return buffer_.size(); }

private:
    Array<uint8>& buffer_;
};

class NetReader {
public:
    NetReader(const uint8* data, size_t size) : data_(data), size_(size) {}

    bool ReadU8(uint8& v);
    bool ReadU16(uint16& v);
    bool ReadU32(uint32& v);
    bool ReadU64(uint64& v);
    bool ReadI32(int32& v);
    bool ReadF32(float32& v);
    bool ReadBool(bool& v);
    bool ReadString(String& v, size_t max_length = 4096);
    bool ReadBytes(void* out, size_t size);
    bool ReadQuantized(float32& v, float32 min, float32 max, uint32 bits);
    bool Skip(size_t size);

    size_t GetPosition() const { return position_; }
    size_t GetRemaining() const { return size_ - position_; }
    const uint8* GetCurrent() const { return data_ + position_; }

private:
    const uint8* data_;
    size_t size_;
    size_t position_ = 0;
};

// ============================================================================
// Connection (reliability layer)
// ============================================================================

enum class Delivery : uint8 {
    Unreliable = 0,   // sent once; may be lost or arrive out of order
    Reliable = 1      // resent until acknowledged; delivered exactly once, in order
};

struct Message {
    Delivery delivery = Delivery::Unreliable;
    Array<uint8> data;
};

struct ConnectionStats {
    float32 rtt_ms = 0.0f;
    float32 packet_loss = 0.0f;   // 0..1, smoothed
    uint64 packets_sent = 0;
    uint64 packets_received = 0;
    uint64 bytes_sent = 0;
    uint64 bytes_received = 0;
    uint32 reliable_pending = 0;
};

class Connection {
public:
    static constexpr size_t kMaxPacketSize = 1200;
    static constexpr size_t kMaxMessageSize = 1024;
    static constexpr uint32 kWindow = 1024;   // sequence history

    Connection();

    bool QueueMessage(Delivery delivery, const void* data, size_t size);

    // Builds the next packet (header + queued messages). Returns false when there is nothing to
    // send and no keep-alive is due.
    bool WritePacket(float64 now, Array<uint8>& out);
    // Reads a payload packet; delivered messages are appended (reliable ones in order)
    bool ReadPacket(float64 now, const uint8* data, size_t size, Array<Message>& delivered);

    const ConnectionStats& GetStats() const { return stats_; }
    float64 GetLastReceiveTime() const { return last_receive_time_; }
    void SetLastReceiveTime(float64 t) { last_receive_time_ = t; }

    float32 keep_alive_interval = 0.1f;   // seconds without traffic before an empty packet is sent

private:
    struct SentPacket {
        uint16 sequence = 0;
        bool valid = false;
        bool acked = false;
        float64 send_time = 0.0;
        Array<uint16> reliable_ids;
    };
    struct PendingReliable {
        uint16 id;
        Array<uint8> data;
        float64 last_send_time = -1.0;
        bool acked = false;
    };

    void ProcessAck(uint16 ack, uint32 ack_bits, float64 now);
    void OnPacketAcked(SentPacket& packet, float64 now);
    void AccountLoss(bool lost);

    uint16 local_sequence_ = 0;
    uint16 remote_sequence_ = 0;
    uint32 received_bits_ = 0;    // packets received before remote_sequence_
    bool received_any_ = false;
    Array<SentPacket> sent_;      // ring buffer indexed by sequence % kWindow
    Array<int32> received_;       // sequence per slot, -1 = none (duplicate detection)

    std::deque<PendingReliable> reliable_out_;
    uint16 next_reliable_id_ = 0;
    uint16 next_expected_id_ = 0;
    std::map<uint16, Array<uint8>> reliable_in_;   // out of order arrivals
    std::deque<Array<uint8>> unreliable_out_;

    float64 last_send_time_ = -1.0;
    float64 last_receive_time_ = 0.0;
    ConnectionStats stats_;
};

// ============================================================================
// Server and client
// ============================================================================

enum class NetEventType : uint8 {
    Connected,
    Disconnected,
    Message,
    ConnectionFailed
};

struct NetEvent {
    NetEventType type = NetEventType::Message;
    uint32 client_id = 0;   // server side; 0 on the client
    Delivery delivery = Delivery::Unreliable;
    Array<uint8> data;
    String reason;
};

struct NetConfig {
    uint32 protocol_id = 0x44525831;   // packets with a different id are ignored ("DRX1")
    float32 timeout_seconds = 10.0f;
    float32 connect_timeout_seconds = 5.0f;
    float32 connect_retry_seconds = 0.1f;
    ConditionerSettings conditioner;
};

class NetEndpoint {
public:
    explicit NetEndpoint(const NetConfig& config);
    virtual ~NetEndpoint() = default;

    bool PollEvent(NetEvent& event);
    void SetConditioner(const ConditionerSettings& settings);
    uint16 GetPort() const { return socket_.GetPort(); }

protected:
    void SendRaw(const Address& to, const Array<uint8>& packet, float64 now);
    void FlushDelayed(float64 now);
    void PushEvent(NetEvent event) { events_.push_back(std::move(event)); }
    void WriteHeader(Array<uint8>& packet, uint8 type) const;
    bool ReadHeader(NetReader& reader, uint8& type) const;

    NetConfig config_;
    UdpSocket socket_;
    std::deque<NetEvent> events_;

private:
    struct Delayed {
        float64 release_time;
        Address to;
        Array<uint8> data;
    };
    std::deque<Delayed> delayed_;
    std::mt19937 rng_;
};

class NetServer : public NetEndpoint {
public:
    explicit NetServer(const NetConfig& config = {}) : NetEndpoint(config) {}
    ~NetServer() override;

    bool Start(uint16 port, uint32 max_clients = 16);
    void Stop();
    bool IsRunning() const { return socket_.IsOpen(); }

    // Receives, handles timeouts and sends one packet per client (messages or keep-alive)
    void Update(float64 now);

    bool Send(uint32 client_id, Delivery delivery, const void* data, size_t size);
    void Broadcast(Delivery delivery, const void* data, size_t size);
    void Disconnect(uint32 client_id, const String& reason = "kicked");

    uint32 GetClientCount() const { return static_cast<uint32>(clients_.size()); }
    Array<uint32> GetClientIds() const;
    const ConnectionStats* GetStats(uint32 client_id) const;
    bool GetClientAddress(uint32 client_id, Address& out) const;

private:
    struct Client {
        uint32 id;
        Address address;
        uint64 nonce;
        Connection connection;
    };
    Client* FindByAddress(const Address& address);
    void RemoveClient(uint32 client_id, const String& reason, bool notify_remote);
    void HandlePacket(const Address& from, const uint8* data, size_t size, float64 now);

    std::map<uint32, Client> clients_;
    uint32 next_client_id_ = 1;
    uint32 max_clients_ = 16;
};

enum class ClientState : uint8 {
    Disconnected,
    Connecting,
    Connected
};

class NetClient : public NetEndpoint {
public:
    explicit NetClient(const NetConfig& config = {});
    ~NetClient() override;

    bool Connect(const Address& server, float64 now);
    void Disconnect(float64 now);
    void Update(float64 now);

    bool Send(Delivery delivery, const void* data, size_t size);

    ClientState GetState() const { return state_; }
    bool IsConnected() const { return state_ == ClientState::Connected; }
    uint32 GetClientId() const { return client_id_; }   // assigned by the server
    const ConnectionStats& GetStats() const { return connection_.GetStats(); }

private:
    void HandlePacket(const uint8* data, size_t size, float64 now);
    void Fail(const String& reason);

    Address server_;
    ClientState state_ = ClientState::Disconnected;
    Connection connection_;
    uint64 nonce_ = 0;
    uint32 client_id_ = 0;
    float64 connect_start_ = 0.0;
    float64 last_request_ = -1.0;
};

/**
 * Wrap-safe 16-bit sequence comparison: true when a is newer than b
 */
inline bool SequenceGreater(uint16 a, uint16 b) {
    return ((a > b) && (a - b <= 32768)) || ((a < b) && (b - a > 32768));
}

} // namespace DRX::Network

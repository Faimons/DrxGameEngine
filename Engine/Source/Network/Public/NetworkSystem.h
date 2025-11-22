/**
 * DRX Engine - Network System
 * UDP/TCP networking for multiplayer games
 */

#pragma once

#include "Core/Public/CoreTypes.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace DRX::Network {

/**
 * Network address
 */
struct NetworkAddress {
    String host;
    uint16 port = 0;

    bool operator==(const NetworkAddress& other) const {
        return host == other.host && port == other.port;
    }

    String ToString() const { return host + ":" + std::to_string(port); }

    static NetworkAddress Parse(const String& str);
};

/**
 * Connection state
 */
enum class ConnectionState : uint32 {
    Disconnected,
    Connecting,
    Connected,
    Disconnecting
};

/**
 * Packet reliability
 */
enum class PacketReliability : uint8 {
    Unreliable,           // Fire and forget
    UnreliableSequenced,  // Latest packet only
    Reliable,             // Guaranteed delivery
    ReliableOrdered,      // Guaranteed and ordered
    ReliableSequenced     // Guaranteed, latest only
};

/**
 * Network packet
 */
struct Packet {
    Array<uint8> data;
    NetworkAddress sender;
    uint32 channel = 0;
    PacketReliability reliability = PacketReliability::Unreliable;
    uint64 timestamp = 0;
};

/**
 * Connection info
 */
struct ConnectionInfo {
    uint32 id = 0;
    NetworkAddress address;
    ConnectionState state = ConnectionState::Disconnected;
    uint64 connected_time = 0;
    uint64 last_receive_time = 0;
    float32 rtt = 0.0f;  // Round-trip time in ms
    float32 packet_loss = 0.0f;
    uint64 bytes_sent = 0;
    uint64 bytes_received = 0;
    uint64 packets_sent = 0;
    uint64 packets_received = 0;
};

/**
 * Network event types
 */
enum class NetworkEventType : uint32 {
    Connected,
    Disconnected,
    DataReceived,
    ConnectionFailed,
    Timeout
};

/**
 * Network event
 */
struct NetworkEvent {
    NetworkEventType type;
    uint32 connection_id;
    Packet packet;
    String message;
};

/**
 * Network callbacks
 */
using OnConnectedCallback = std::function<void(uint32 connection_id)>;
using OnDisconnectedCallback = std::function<void(uint32 connection_id, const String& reason)>;
using OnDataReceivedCallback = std::function<void(uint32 connection_id, const Packet& packet)>;

/**
 * Network channel configuration
 */
struct ChannelConfig {
    uint32 id;
    PacketReliability default_reliability = PacketReliability::ReliableOrdered;
    uint32 max_bandwidth = 0;  // 0 = unlimited
};

/**
 * Socket base class
 */
class ISocket {
public:
    virtual ~ISocket() = default;

    virtual bool Bind(uint16 port) = 0;
    virtual bool Connect(const NetworkAddress& address) = 0;
    virtual void Close() = 0;

    virtual int32 Send(const void* data, uint32 size) = 0;
    virtual int32 SendTo(const void* data, uint32 size, const NetworkAddress& address) = 0;
    virtual int32 Receive(void* buffer, uint32 buffer_size, NetworkAddress& sender) = 0;

    virtual bool IsValid() const = 0;
    virtual uint16 GetLocalPort() const = 0;
};

/**
 * UDP Socket
 */
class UDPSocket : public ISocket {
public:
    UDPSocket();
    ~UDPSocket() override;

    bool Bind(uint16 port) override;
    bool Connect(const NetworkAddress& address) override;
    void Close() override;

    int32 Send(const void* data, uint32 size) override;
    int32 SendTo(const void* data, uint32 size, const NetworkAddress& address) override;
    int32 Receive(void* buffer, uint32 buffer_size, NetworkAddress& sender) override;

    bool IsValid() const override { return socket_fd_ >= 0; }
    uint16 GetLocalPort() const override { return local_port_; }

    void SetNonBlocking(bool non_blocking);
    void SetBroadcast(bool enable);

private:
    int32 socket_fd_ = -1;
    uint16 local_port_ = 0;
};

/**
 * TCP Socket
 */
class TCPSocket : public ISocket {
public:
    TCPSocket();
    ~TCPSocket() override;

    bool Bind(uint16 port) override;
    bool Connect(const NetworkAddress& address) override;
    void Close() override;

    bool Listen(int32 backlog = 10);
    SharedPtr<TCPSocket> Accept();

    int32 Send(const void* data, uint32 size) override;
    int32 SendTo(const void* data, uint32 size, const NetworkAddress& address) override;
    int32 Receive(void* buffer, uint32 buffer_size, NetworkAddress& sender) override;

    bool IsValid() const override { return socket_fd_ >= 0; }
    uint16 GetLocalPort() const override { return local_port_; }

    void SetNonBlocking(bool non_blocking);
    void SetNoDelay(bool no_delay);

private:
    int32 socket_fd_ = -1;
    uint16 local_port_ = 0;
};

/**
 * Network peer - manages a single connection with reliability
 */
class NetworkPeer {
public:
    NetworkPeer(uint32 id, const NetworkAddress& address);
    ~NetworkPeer();

    void Update(float32 delta_time);

    void Send(const void* data, uint32 size, uint32 channel, PacketReliability reliability);
    void SendImmediate(const void* data, uint32 size);

    void OnPacketReceived(const uint8* data, uint32 size);

    uint32 GetId() const { return id_; }
    const NetworkAddress& GetAddress() const { return address_; }
    ConnectionState GetState() const { return state_; }
    const ConnectionInfo& GetInfo() const { return info_; }

    void SetState(ConnectionState state) { state_ = state; }

private:
    void ProcessAcks();
    void ResendUnacked();
    void UpdateRTT(float32 rtt);

    uint32 id_;
    NetworkAddress address_;
    ConnectionState state_ = ConnectionState::Disconnected;
    ConnectionInfo info_;

    // Reliability tracking
    uint32 local_sequence_ = 0;
    uint32 remote_sequence_ = 0;
    uint32 ack_bits_ = 0;

    struct PendingPacket {
        Array<uint8> data;
        uint32 sequence;
        uint64 send_time;
        uint32 resend_count;
    };
    Array<PendingPacket> pending_acks_;

    // RTT calculation
    float32 rtt_smoothed_ = 100.0f;
    float32 rtt_variance_ = 50.0f;
};

/**
 * Network Server
 */
class NetworkServer {
public:
    NetworkServer();
    ~NetworkServer();

    bool Start(uint16 port, uint32 max_clients = 32);
    void Stop();
    void Update(float32 delta_time);

    void Broadcast(const void* data, uint32 size, uint32 channel = 0,
                   PacketReliability reliability = PacketReliability::ReliableOrdered);
    void Send(uint32 client_id, const void* data, uint32 size, uint32 channel = 0,
              PacketReliability reliability = PacketReliability::ReliableOrdered);
    void Disconnect(uint32 client_id, const String& reason = "");

    bool IsRunning() const { return running_; }
    uint32 GetClientCount() const;
    Array<ConnectionInfo> GetConnections() const;
    ConnectionInfo* GetConnection(uint32 client_id);

    // Callbacks
    void SetOnClientConnected(OnConnectedCallback callback) { on_connected_ = callback; }
    void SetOnClientDisconnected(OnDisconnectedCallback callback) { on_disconnected_ = callback; }
    void SetOnDataReceived(OnDataReceivedCallback callback) { on_data_received_ = callback; }

    // Events (polling alternative)
    bool PollEvent(NetworkEvent& event);

private:
    void ReceiveThread();
    void ProcessPacket(const uint8* data, uint32 size, const NetworkAddress& sender);
    uint32 AddClient(const NetworkAddress& address);
    void RemoveClient(uint32 client_id);

    UniquePtr<UDPSocket> socket_;
    HashMap<uint32, SharedPtr<NetworkPeer>> clients_;
    HashMap<String, uint32> address_to_client_;
    uint32 next_client_id_ = 1;
    uint32 max_clients_ = 32;

    std::atomic<bool> running_{false};
    std::thread receive_thread_;

    std::queue<NetworkEvent> events_;
    std::mutex events_mutex_;

    OnConnectedCallback on_connected_;
    OnDisconnectedCallback on_disconnected_;
    OnDataReceivedCallback on_data_received_;
};

/**
 * Network Client
 */
class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    bool Connect(const NetworkAddress& server, uint32 timeout_ms = 5000);
    void Disconnect(const String& reason = "");
    void Update(float32 delta_time);

    void Send(const void* data, uint32 size, uint32 channel = 0,
              PacketReliability reliability = PacketReliability::ReliableOrdered);

    bool IsConnected() const { return state_ == ConnectionState::Connected; }
    ConnectionState GetState() const { return state_; }
    const ConnectionInfo& GetConnectionInfo() const;
    float32 GetRTT() const;

    // Callbacks
    void SetOnConnected(OnConnectedCallback callback) { on_connected_ = callback; }
    void SetOnDisconnected(OnDisconnectedCallback callback) { on_disconnected_ = callback; }
    void SetOnDataReceived(OnDataReceivedCallback callback) { on_data_received_ = callback; }

    // Events (polling alternative)
    bool PollEvent(NetworkEvent& event);

private:
    void ReceiveThread();
    void ProcessPacket(const uint8* data, uint32 size);

    UniquePtr<UDPSocket> socket_;
    SharedPtr<NetworkPeer> server_peer_;
    NetworkAddress server_address_;
    ConnectionState state_ = ConnectionState::Disconnected;

    std::atomic<bool> running_{false};
    std::thread receive_thread_;

    std::queue<NetworkEvent> events_;
    std::mutex events_mutex_;

    OnConnectedCallback on_connected_;
    OnDisconnectedCallback on_disconnected_;
    OnDataReceivedCallback on_data_received_;
};

/**
 * RPC (Remote Procedure Call) System
 */
class RPCManager {
public:
    static RPCManager& Instance();

    using RPCHandler = std::function<void(uint32 sender_id, const uint8* data, uint32 size)>;

    void RegisterRPC(const String& name, RPCHandler handler);
    void UnregisterRPC(const String& name);

    void CallServer(const String& name, const void* data, uint32 size);
    void CallClient(uint32 client_id, const String& name, const void* data, uint32 size);
    void CallAll(const String& name, const void* data, uint32 size);

    void ProcessRPC(uint32 sender_id, const uint8* data, uint32 size);

private:
    RPCManager() = default;

    HashMap<String, RPCHandler> handlers_;
    HashMap<uint32, String> rpc_id_to_name_;
    HashMap<String, uint32> rpc_name_to_id_;
    uint32 next_rpc_id_ = 1;
};

/**
 * Network Serialization helpers
 */
class NetworkWriter {
public:
    NetworkWriter(Array<uint8>& buffer) : buffer_(buffer) {}

    void Write(bool value);
    void Write(int8 value);
    void Write(uint8 value);
    void Write(int16 value);
    void Write(uint16 value);
    void Write(int32 value);
    void Write(uint32 value);
    void Write(int64 value);
    void Write(uint64 value);
    void Write(float32 value);
    void Write(float64 value);
    void Write(const String& value);
    void Write(const void* data, uint32 size);

    template<typename T>
    void Write(const Array<T>& arr) {
        Write(static_cast<uint32>(arr.size()));
        for (const auto& item : arr) {
            Write(item);
        }
    }

private:
    Array<uint8>& buffer_;
};

class NetworkReader {
public:
    NetworkReader(const uint8* data, uint32 size) : data_(data), size_(size) {}

    bool Read(bool& value);
    bool Read(int8& value);
    bool Read(uint8& value);
    bool Read(int16& value);
    bool Read(uint16& value);
    bool Read(int32& value);
    bool Read(uint32& value);
    bool Read(int64& value);
    bool Read(uint64& value);
    bool Read(float32& value);
    bool Read(float64& value);
    bool Read(String& value);
    bool Read(void* data, uint32 size);

    template<typename T>
    bool Read(Array<T>& arr) {
        uint32 count;
        if (!Read(count)) return false;
        arr.resize(count);
        for (auto& item : arr) {
            if (!Read(item)) return false;
        }
        return true;
    }

    uint32 GetPosition() const { return position_; }
    uint32 GetRemaining() const { return size_ - position_; }

private:
    const uint8* data_;
    uint32 size_;
    uint32 position_ = 0;
};

} // namespace DRX::Network

/**
 * DRX Engine - Network System Implementation
 */

#include "Network/Public/NetworkSystem.h"
#include "Core/Public/Log.h"

#include <cstring>
#include <chrono>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <netdb.h>
    #define closesocket close
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

namespace DRX::Network {

// Helper to get current time in ms
static uint64 GetTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

// ============================================================================
// NetworkAddress
// ============================================================================

NetworkAddress NetworkAddress::Parse(const String& str) {
    NetworkAddress addr;
    size_t colon = str.rfind(':');
    if (colon != String::npos) {
        addr.host = str.substr(0, colon);
        addr.port = static_cast<uint16>(std::stoi(str.substr(colon + 1)));
    } else {
        addr.host = str;
    }
    return addr;
}

// ============================================================================
// UDPSocket Implementation
// ============================================================================

UDPSocket::UDPSocket() {
#ifdef _WIN32
    static bool wsa_initialized = false;
    if (!wsa_initialized) {
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
        wsa_initialized = true;
    }
#endif

    socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

UDPSocket::~UDPSocket() {
    Close();
}

bool UDPSocket::Bind(uint16 port) {
    if (socket_fd_ < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        return false;
    }

    local_port_ = port;
    return true;
}

bool UDPSocket::Connect(const NetworkAddress& address) {
    if (socket_fd_ < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(address.port);
    inet_pton(AF_INET, address.host.c_str(), &addr.sin_addr);

    return connect(socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != SOCKET_ERROR;
}

void UDPSocket::Close() {
    if (socket_fd_ >= 0) {
        closesocket(socket_fd_);
        socket_fd_ = -1;
    }
}

int32 UDPSocket::Send(const void* data, uint32 size) {
    return send(socket_fd_, static_cast<const char*>(data), size, 0);
}

int32 UDPSocket::SendTo(const void* data, uint32 size, const NetworkAddress& address) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(address.port);
    inet_pton(AF_INET, address.host.c_str(), &addr.sin_addr);

    return sendto(socket_fd_, static_cast<const char*>(data), size, 0,
                  reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

int32 UDPSocket::Receive(void* buffer, uint32 buffer_size, NetworkAddress& sender) {
    sockaddr_in addr{};
    socklen_t addr_len = sizeof(addr);

    int32 received = recvfrom(socket_fd_, static_cast<char*>(buffer), buffer_size, 0,
                              reinterpret_cast<sockaddr*>(&addr), &addr_len);

    if (received > 0) {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip, INET_ADDRSTRLEN);
        sender.host = ip;
        sender.port = ntohs(addr.sin_port);
    }

    return received;
}

void UDPSocket::SetNonBlocking(bool non_blocking) {
#ifdef _WIN32
    u_long mode = non_blocking ? 1 : 0;
    ioctlsocket(socket_fd_, FIONBIO, &mode);
#else
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (non_blocking) {
        fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
    } else {
        fcntl(socket_fd_, F_SETFL, flags & ~O_NONBLOCK);
    }
#endif
}

void UDPSocket::SetBroadcast(bool enable) {
    int opt = enable ? 1 : 0;
    setsockopt(socket_fd_, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&opt), sizeof(opt));
}

// ============================================================================
// TCPSocket Implementation
// ============================================================================

TCPSocket::TCPSocket() {
#ifdef _WIN32
    static bool wsa_initialized = false;
    if (!wsa_initialized) {
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
        wsa_initialized = true;
    }
#endif

    socket_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

TCPSocket::~TCPSocket() {
    Close();
}

bool TCPSocket::Bind(uint16 port) {
    if (socket_fd_ < 0) return false;

    int opt = 1;
    setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        return false;
    }

    local_port_ = port;
    return true;
}

bool TCPSocket::Connect(const NetworkAddress& address) {
    if (socket_fd_ < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(address.port);
    inet_pton(AF_INET, address.host.c_str(), &addr.sin_addr);

    return connect(socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != SOCKET_ERROR;
}

void TCPSocket::Close() {
    if (socket_fd_ >= 0) {
        closesocket(socket_fd_);
        socket_fd_ = -1;
    }
}

bool TCPSocket::Listen(int32 backlog) {
    return listen(socket_fd_, backlog) != SOCKET_ERROR;
}

SharedPtr<TCPSocket> TCPSocket::Accept() {
    sockaddr_in addr{};
    socklen_t addr_len = sizeof(addr);

    int client_fd = accept(socket_fd_, reinterpret_cast<sockaddr*>(&addr), &addr_len);
    if (client_fd < 0) return nullptr;

    auto client = MakeShared<TCPSocket>();
    client->socket_fd_ = client_fd;
    return client;
}

int32 TCPSocket::Send(const void* data, uint32 size) {
    return send(socket_fd_, static_cast<const char*>(data), size, 0);
}

int32 TCPSocket::SendTo(const void* data, uint32 size, const NetworkAddress& address) {
    return Send(data, size);  // TCP is connection-oriented
}

int32 TCPSocket::Receive(void* buffer, uint32 buffer_size, NetworkAddress& sender) {
    return recv(socket_fd_, static_cast<char*>(buffer), buffer_size, 0);
}

void TCPSocket::SetNonBlocking(bool non_blocking) {
#ifdef _WIN32
    u_long mode = non_blocking ? 1 : 0;
    ioctlsocket(socket_fd_, FIONBIO, &mode);
#else
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (non_blocking) {
        fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
    } else {
        fcntl(socket_fd_, F_SETFL, flags & ~O_NONBLOCK);
    }
#endif
}

void TCPSocket::SetNoDelay(bool no_delay) {
    int opt = no_delay ? 1 : 0;
    setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&opt), sizeof(opt));
}

// ============================================================================
// NetworkPeer Implementation
// ============================================================================

NetworkPeer::NetworkPeer(uint32 id, const NetworkAddress& address)
    : id_(id), address_(address) {
    info_.id = id;
    info_.address = address;
}

NetworkPeer::~NetworkPeer() = default;

void NetworkPeer::Update(float32 delta_time) {
    ResendUnacked();
}

void NetworkPeer::Send(const void* data, uint32 size, uint32 channel, PacketReliability reliability) {
    // Add packet header and queue for sending
    Array<uint8> packet;
    packet.reserve(size + 12);

    // Header: sequence(4) + ack(4) + ack_bits(4) + data
    uint32 seq = local_sequence_++;

    packet.push_back((seq >> 0) & 0xFF);
    packet.push_back((seq >> 8) & 0xFF);
    packet.push_back((seq >> 16) & 0xFF);
    packet.push_back((seq >> 24) & 0xFF);

    packet.push_back((remote_sequence_ >> 0) & 0xFF);
    packet.push_back((remote_sequence_ >> 8) & 0xFF);
    packet.push_back((remote_sequence_ >> 16) & 0xFF);
    packet.push_back((remote_sequence_ >> 24) & 0xFF);

    packet.push_back((ack_bits_ >> 0) & 0xFF);
    packet.push_back((ack_bits_ >> 8) & 0xFF);
    packet.push_back((ack_bits_ >> 16) & 0xFF);
    packet.push_back((ack_bits_ >> 24) & 0xFF);

    const uint8* src = static_cast<const uint8*>(data);
    packet.insert(packet.end(), src, src + size);

    if (reliability >= PacketReliability::Reliable) {
        PendingPacket pending;
        pending.data = packet;
        pending.sequence = seq;
        pending.send_time = GetTimeMs();
        pending.resend_count = 0;
        pending_acks_.push_back(pending);
    }

    info_.packets_sent++;
    info_.bytes_sent += packet.size();
}

void NetworkPeer::OnPacketReceived(const uint8* data, uint32 size) {
    if (size < 12) return;

    // Parse header
    uint32 seq = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    uint32 ack = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    uint32 ack_bits = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);

    // Update remote sequence
    if (seq > remote_sequence_) {
        uint32 diff = seq - remote_sequence_;
        ack_bits_ = (ack_bits_ << diff) | 1;
        remote_sequence_ = seq;
    } else if (seq < remote_sequence_) {
        uint32 diff = remote_sequence_ - seq;
        if (diff < 32) {
            ack_bits_ |= (1 << diff);
        }
    }

    // Process acks
    for (auto it = pending_acks_.begin(); it != pending_acks_.end();) {
        bool acked = false;
        if (it->sequence == ack) {
            acked = true;
        } else if (it->sequence < ack) {
            uint32 diff = ack - it->sequence;
            if (diff < 32 && (ack_bits & (1 << diff))) {
                acked = true;
            }
        }

        if (acked) {
            float32 rtt = static_cast<float32>(GetTimeMs() - it->send_time);
            UpdateRTT(rtt);
            it = pending_acks_.erase(it);
        } else {
            ++it;
        }
    }

    info_.last_receive_time = GetTimeMs();
    info_.packets_received++;
    info_.bytes_received += size;
}

void NetworkPeer::ResendUnacked() {
    uint64 now = GetTimeMs();
    float32 timeout = rtt_smoothed_ + 4 * rtt_variance_;

    for (auto& pending : pending_acks_) {
        if (now - pending.send_time > static_cast<uint64>(timeout)) {
            // Resend
            pending.send_time = now;
            pending.resend_count++;
        }
    }
}

void NetworkPeer::UpdateRTT(float32 rtt) {
    const float32 alpha = 0.125f;
    const float32 beta = 0.25f;

    float32 diff = std::abs(rtt - rtt_smoothed_);
    rtt_variance_ = (1 - beta) * rtt_variance_ + beta * diff;
    rtt_smoothed_ = (1 - alpha) * rtt_smoothed_ + alpha * rtt;

    info_.rtt = rtt_smoothed_;
}

// ============================================================================
// NetworkServer Implementation
// ============================================================================

NetworkServer::NetworkServer() = default;

NetworkServer::~NetworkServer() {
    Stop();
}

bool NetworkServer::Start(uint16 port, uint32 max_clients) {
    max_clients_ = max_clients;

    socket_ = MakeUnique<UDPSocket>();
    if (!socket_->Bind(port)) {
        DRX_LOG_ERROR("Failed to bind server to port {}", port);
        return false;
    }

    socket_->SetNonBlocking(true);

    running_ = true;
    receive_thread_ = std::thread(&NetworkServer::ReceiveThread, this);

    DRX_LOG_INFO("Server started on port {}", port);
    return true;
}

void NetworkServer::Stop() {
    running_ = false;

    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }

    clients_.clear();
    address_to_client_.clear();

    if (socket_) {
        socket_->Close();
        socket_.reset();
    }

    DRX_LOG_INFO("Server stopped");
}

void NetworkServer::Update(float32 delta_time) {
    for (auto& [id, peer] : clients_) {
        peer->Update(delta_time);
    }
}

void NetworkServer::Broadcast(const void* data, uint32 size, uint32 channel,
                               PacketReliability reliability) {
    for (auto& [id, peer] : clients_) {
        peer->Send(data, size, channel, reliability);
    }
}

void NetworkServer::Send(uint32 client_id, const void* data, uint32 size, uint32 channel,
                          PacketReliability reliability) {
    auto it = clients_.find(client_id);
    if (it != clients_.end()) {
        it->second->Send(data, size, channel, reliability);
    }
}

void NetworkServer::Disconnect(uint32 client_id, const String& reason) {
    RemoveClient(client_id);

    NetworkEvent event;
    event.type = NetworkEventType::Disconnected;
    event.connection_id = client_id;
    event.message = reason;

    std::lock_guard<std::mutex> lock(events_mutex_);
    events_.push(event);
}

uint32 NetworkServer::GetClientCount() const {
    return static_cast<uint32>(clients_.size());
}

Array<ConnectionInfo> NetworkServer::GetConnections() const {
    Array<ConnectionInfo> result;
    for (const auto& [id, peer] : clients_) {
        result.push_back(peer->GetInfo());
    }
    return result;
}

ConnectionInfo* NetworkServer::GetConnection(uint32 client_id) {
    auto it = clients_.find(client_id);
    if (it != clients_.end()) {
        return const_cast<ConnectionInfo*>(&it->second->GetInfo());
    }
    return nullptr;
}

bool NetworkServer::PollEvent(NetworkEvent& event) {
    std::lock_guard<std::mutex> lock(events_mutex_);
    if (events_.empty()) return false;

    event = events_.front();
    events_.pop();
    return true;
}

void NetworkServer::ReceiveThread() {
    Array<uint8> buffer(65536);

    while (running_) {
        NetworkAddress sender;
        int32 received = socket_->Receive(buffer.data(), buffer.size(), sender);

        if (received > 0) {
            ProcessPacket(buffer.data(), received, sender);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void NetworkServer::ProcessPacket(const uint8* data, uint32 size, const NetworkAddress& sender) {
    String addr_str = sender.ToString();

    auto it = address_to_client_.find(addr_str);
    uint32 client_id;

    if (it == address_to_client_.end()) {
        // New client
        if (clients_.size() >= max_clients_) {
            return;  // Server full
        }

        client_id = AddClient(sender);
    } else {
        client_id = it->second;
    }

    clients_[client_id]->OnPacketReceived(data, size);

    // Create data event
    NetworkEvent event;
    event.type = NetworkEventType::DataReceived;
    event.connection_id = client_id;
    event.packet.data.assign(data + 12, data + size);  // Skip header
    event.packet.sender = sender;

    std::lock_guard<std::mutex> lock(events_mutex_);
    events_.push(event);

    if (on_data_received_) {
        on_data_received_(client_id, event.packet);
    }
}

uint32 NetworkServer::AddClient(const NetworkAddress& address) {
    uint32 id = next_client_id_++;
    auto peer = MakeShared<NetworkPeer>(id, address);
    peer->SetState(ConnectionState::Connected);

    clients_[id] = peer;
    address_to_client_[address.ToString()] = id;

    NetworkEvent event;
    event.type = NetworkEventType::Connected;
    event.connection_id = id;

    {
        std::lock_guard<std::mutex> lock(events_mutex_);
        events_.push(event);
    }

    if (on_connected_) {
        on_connected_(id);
    }

    DRX_LOG_INFO("Client {} connected from {}", id, address.ToString());
    return id;
}

void NetworkServer::RemoveClient(uint32 client_id) {
    auto it = clients_.find(client_id);
    if (it != clients_.end()) {
        address_to_client_.erase(it->second->GetAddress().ToString());
        clients_.erase(it);

        if (on_disconnected_) {
            on_disconnected_(client_id, "");
        }

        DRX_LOG_INFO("Client {} disconnected", client_id);
    }
}

// ============================================================================
// NetworkClient Implementation
// ============================================================================

NetworkClient::NetworkClient() = default;

NetworkClient::~NetworkClient() {
    Disconnect();
}

bool NetworkClient::Connect(const NetworkAddress& server, uint32 timeout_ms) {
    server_address_ = server;
    state_ = ConnectionState::Connecting;

    socket_ = MakeUnique<UDPSocket>();
    if (!socket_->Bind(0)) {
        state_ = ConnectionState::Disconnected;
        return false;
    }

    socket_->SetNonBlocking(true);

    server_peer_ = MakeShared<NetworkPeer>(0, server);

    running_ = true;
    receive_thread_ = std::thread(&NetworkClient::ReceiveThread, this);

    // Send connection request
    uint8 connect_packet[4] = {0x01, 0x00, 0x00, 0x00};  // Connection request
    socket_->SendTo(connect_packet, 4, server);

    // Wait for connection
    uint64 start = GetTimeMs();
    while (state_ == ConnectionState::Connecting) {
        if (GetTimeMs() - start > timeout_ms) {
            Disconnect("Connection timeout");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return state_ == ConnectionState::Connected;
}

void NetworkClient::Disconnect(const String& reason) {
    if (state_ == ConnectionState::Disconnected) return;

    state_ = ConnectionState::Disconnecting;
    running_ = false;

    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }

    if (socket_) {
        socket_->Close();
        socket_.reset();
    }

    server_peer_.reset();
    state_ = ConnectionState::Disconnected;

    if (on_disconnected_) {
        on_disconnected_(0, reason);
    }

    DRX_LOG_INFO("Disconnected: {}", reason);
}

void NetworkClient::Update(float32 delta_time) {
    if (server_peer_) {
        server_peer_->Update(delta_time);
    }
}

void NetworkClient::Send(const void* data, uint32 size, uint32 channel,
                          PacketReliability reliability) {
    if (server_peer_ && state_ == ConnectionState::Connected) {
        server_peer_->Send(data, size, channel, reliability);
    }
}

const ConnectionInfo& NetworkClient::GetConnectionInfo() const {
    static ConnectionInfo empty;
    return server_peer_ ? server_peer_->GetInfo() : empty;
}

float32 NetworkClient::GetRTT() const {
    return server_peer_ ? server_peer_->GetInfo().rtt : 0.0f;
}

bool NetworkClient::PollEvent(NetworkEvent& event) {
    std::lock_guard<std::mutex> lock(events_mutex_);
    if (events_.empty()) return false;

    event = events_.front();
    events_.pop();
    return true;
}

void NetworkClient::ReceiveThread() {
    Array<uint8> buffer(65536);

    while (running_) {
        NetworkAddress sender;
        int32 received = socket_->Receive(buffer.data(), buffer.size(), sender);

        if (received > 0) {
            ProcessPacket(buffer.data(), received);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void NetworkClient::ProcessPacket(const uint8* data, uint32 size) {
    if (state_ == ConnectionState::Connecting && size >= 4 && data[0] == 0x02) {
        // Connection accepted
        state_ = ConnectionState::Connected;
        server_peer_->SetState(ConnectionState::Connected);

        NetworkEvent event;
        event.type = NetworkEventType::Connected;
        event.connection_id = 0;

        {
            std::lock_guard<std::mutex> lock(events_mutex_);
            events_.push(event);
        }

        if (on_connected_) {
            on_connected_(0);
        }

        DRX_LOG_INFO("Connected to server");
        return;
    }

    if (server_peer_) {
        server_peer_->OnPacketReceived(data, size);

        NetworkEvent event;
        event.type = NetworkEventType::DataReceived;
        event.connection_id = 0;
        event.packet.data.assign(data + 12, data + size);

        std::lock_guard<std::mutex> lock(events_mutex_);
        events_.push(event);

        if (on_data_received_) {
            on_data_received_(0, event.packet);
        }
    }
}

// ============================================================================
// NetworkWriter/Reader Implementation
// ============================================================================

void NetworkWriter::Write(bool value) { buffer_.push_back(value ? 1 : 0); }
void NetworkWriter::Write(int8 value) { buffer_.push_back(static_cast<uint8>(value)); }
void NetworkWriter::Write(uint8 value) { buffer_.push_back(value); }

void NetworkWriter::Write(int16 value) {
    buffer_.push_back(value & 0xFF);
    buffer_.push_back((value >> 8) & 0xFF);
}

void NetworkWriter::Write(uint16 value) {
    buffer_.push_back(value & 0xFF);
    buffer_.push_back((value >> 8) & 0xFF);
}

void NetworkWriter::Write(int32 value) {
    buffer_.push_back(value & 0xFF);
    buffer_.push_back((value >> 8) & 0xFF);
    buffer_.push_back((value >> 16) & 0xFF);
    buffer_.push_back((value >> 24) & 0xFF);
}

void NetworkWriter::Write(uint32 value) {
    buffer_.push_back(value & 0xFF);
    buffer_.push_back((value >> 8) & 0xFF);
    buffer_.push_back((value >> 16) & 0xFF);
    buffer_.push_back((value >> 24) & 0xFF);
}

void NetworkWriter::Write(float32 value) {
    uint32 bits;
    std::memcpy(&bits, &value, sizeof(float32));
    Write(bits);
}

void NetworkWriter::Write(float64 value) {
    uint64 bits;
    std::memcpy(&bits, &value, sizeof(float64));
    Write(bits);
}

void NetworkWriter::Write(int64 value) {
    Write(static_cast<uint32>(value & 0xFFFFFFFF));
    Write(static_cast<uint32>((value >> 32) & 0xFFFFFFFF));
}

void NetworkWriter::Write(uint64 value) {
    Write(static_cast<uint32>(value & 0xFFFFFFFF));
    Write(static_cast<uint32>((value >> 32) & 0xFFFFFFFF));
}

void NetworkWriter::Write(const String& value) {
    Write(static_cast<uint32>(value.size()));
    Write(value.data(), static_cast<uint32>(value.size()));
}

void NetworkWriter::Write(const void* data, uint32 size) {
    const uint8* bytes = static_cast<const uint8*>(data);
    buffer_.insert(buffer_.end(), bytes, bytes + size);
}

bool NetworkReader::Read(bool& value) {
    if (position_ >= size_) return false;
    value = data_[position_++] != 0;
    return true;
}

bool NetworkReader::Read(int8& value) {
    if (position_ >= size_) return false;
    value = static_cast<int8>(data_[position_++]);
    return true;
}

bool NetworkReader::Read(uint8& value) {
    if (position_ >= size_) return false;
    value = data_[position_++];
    return true;
}

bool NetworkReader::Read(int16& value) {
    if (position_ + 2 > size_) return false;
    value = data_[position_] | (data_[position_ + 1] << 8);
    position_ += 2;
    return true;
}

bool NetworkReader::Read(uint16& value) {
    if (position_ + 2 > size_) return false;
    value = data_[position_] | (data_[position_ + 1] << 8);
    position_ += 2;
    return true;
}

bool NetworkReader::Read(int32& value) {
    if (position_ + 4 > size_) return false;
    value = data_[position_] | (data_[position_ + 1] << 8) |
            (data_[position_ + 2] << 16) | (data_[position_ + 3] << 24);
    position_ += 4;
    return true;
}

bool NetworkReader::Read(uint32& value) {
    if (position_ + 4 > size_) return false;
    value = data_[position_] | (data_[position_ + 1] << 8) |
            (data_[position_ + 2] << 16) | (data_[position_ + 3] << 24);
    position_ += 4;
    return true;
}

bool NetworkReader::Read(float32& value) {
    uint32 bits;
    if (!Read(bits)) return false;
    std::memcpy(&value, &bits, sizeof(float32));
    return true;
}

bool NetworkReader::Read(float64& value) {
    uint64 bits;
    if (!Read(bits)) return false;
    std::memcpy(&value, &bits, sizeof(float64));
    return true;
}

bool NetworkReader::Read(int64& value) {
    uint32 low, high;
    if (!Read(low) || !Read(high)) return false;
    value = static_cast<int64>(low) | (static_cast<int64>(high) << 32);
    return true;
}

bool NetworkReader::Read(uint64& value) {
    uint32 low, high;
    if (!Read(low) || !Read(high)) return false;
    value = static_cast<uint64>(low) | (static_cast<uint64>(high) << 32);
    return true;
}

bool NetworkReader::Read(String& value) {
    uint32 len;
    if (!Read(len)) return false;
    if (position_ + len > size_) return false;
    value.assign(reinterpret_cast<const char*>(data_ + position_), len);
    position_ += len;
    return true;
}

bool NetworkReader::Read(void* data, uint32 size) {
    if (position_ + size > size_) return false;
    std::memcpy(data, data_ + position_, size);
    position_ += size;
    return true;
}

// ============================================================================
// RPCManager Implementation
// ============================================================================

RPCManager& RPCManager::Instance() {
    static RPCManager instance;
    return instance;
}

void RPCManager::RegisterRPC(const String& name, RPCHandler handler) {
    uint32 id = next_rpc_id_++;
    handlers_[name] = handler;
    rpc_id_to_name_[id] = name;
    rpc_name_to_id_[name] = id;
}

void RPCManager::UnregisterRPC(const String& name) {
    auto it = rpc_name_to_id_.find(name);
    if (it != rpc_name_to_id_.end()) {
        rpc_id_to_name_.erase(it->second);
        rpc_name_to_id_.erase(it);
        handlers_.erase(name);
    }
}

void RPCManager::ProcessRPC(uint32 sender_id, const uint8* data, uint32 size) {
    if (size < 4) return;

    uint32 rpc_id = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);

    auto it = rpc_id_to_name_.find(rpc_id);
    if (it == rpc_id_to_name_.end()) return;

    auto handler_it = handlers_.find(it->second);
    if (handler_it != handlers_.end()) {
        handler_it->second(sender_id, data + 4, size - 4);
    }
}

} // namespace DRX::Network

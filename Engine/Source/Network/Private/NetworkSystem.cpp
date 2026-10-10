/**
 * DRX Engine - Network System Implementation
 */

#include "Network/Public/NetworkSystem.h"
#include "Core/Public/Log.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socklen_t = int;
#else
    #include <arpa/inet.h>
    #include <cerrno>
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace DRX::Network {

namespace {

enum PacketType : uint8 {
    kConnectRequest = 1,
    kConnectAccept = 2,
    kConnectDenied = 3,
    kDisconnect = 4,
    kPayload = 5
};

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

SocketHandle ToHandle(uint64 h) { return static_cast<SocketHandle>(h); }

// Reference-counted platform socket initialization (WSAStartup on Windows)
bool AcquireSockets() {
#ifdef _WIN32
    static std::mutex mutex;
    static int users = 0;
    std::lock_guard<std::mutex> lock(mutex);
    if (users++ == 0) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            --users;
            return false;
        }
    }
#endif
    return true;
}

void ReleaseSockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

sockaddr_in ToSockaddr(const Address& address) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(address.ip);
    addr.sin_port = htons(address.port);
    return addr;
}

uint64 RandomNonce() {
    static std::atomic<uint64> counter{0};
    std::random_device device;
    return (static_cast<uint64>(device()) << 32) ^ device() ^ (++counter * 0x9E3779B97F4A7C15ull);
}

} // namespace

// ============================================================================
// Address
// ============================================================================

Address Address::FromIPv4(uint8 a, uint8 b, uint8 c, uint8 d, uint16 port) {
    Address address;
    address.ip = (static_cast<uint32>(a) << 24) | (static_cast<uint32>(b) << 16) | (static_cast<uint32>(c) << 8) | d;
    address.port = port;
    return address;
}

bool Address::Parse(const String& text, Address& out) {
    const size_t colon = text.rfind(':');
    if (colon == String::npos || colon == 0 || colon + 1 >= text.size()) return false;

    const String host = text.substr(0, colon);
    const String port_text = text.substr(colon + 1);
    char* end = nullptr;
    const unsigned long port = std::strtoul(port_text.c_str(), &end, 10);
    if (*end != '\0' || port == 0 || port > 65535) return false;

    in_addr parsed{};
    if (inet_pton(AF_INET, host.c_str(), &parsed) == 1) {
        out.ip = ntohl(parsed.s_addr);
        out.port = static_cast<uint16>(port);
        return true;
    }

    if (!AcquireSockets()) return false;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    const bool ok = getaddrinfo(host.c_str(), nullptr, &hints, &result) == 0 && result;
    if (ok) {
        out.ip = ntohl(reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr.s_addr);
        out.port = static_cast<uint16>(port);
        freeaddrinfo(result);
    }
    ReleaseSockets();
    return ok;
}

String Address::ToString() const {
    return fmt::format("{}.{}.{}.{}:{}", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, port);
}

// ============================================================================
// UdpSocket
// ============================================================================

UdpSocket::~UdpSocket() {
    Close();
}

bool UdpSocket::Open(uint16 port) {
    Close();
    if (!AcquireSockets()) {
        DRX_LOG_ERROR("Socket subsystem unavailable");
        return false;
    }

    const SocketHandle handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == kInvalidSocket) {
        ReleaseSockets();
        DRX_LOG_ERROR("Failed to create UDP socket");
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(handle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        DRX_LOG_ERROR("Failed to bind UDP port {}", port);
#ifdef _WIN32
        closesocket(handle);
#else
        close(handle);
#endif
        ReleaseSockets();
        return false;
    }

#ifdef _WIN32
    u_long non_blocking = 1;
    ioctlsocket(handle, FIONBIO, &non_blocking);
#else
    fcntl(handle, F_SETFL, fcntl(handle, F_GETFL, 0) | O_NONBLOCK);
#endif

    sockaddr_in bound{};
    socklen_t length = sizeof(bound);
    getsockname(handle, reinterpret_cast<sockaddr*>(&bound), &length);
    port_ = ntohs(bound.sin_port);
    handle_ = static_cast<uint64>(handle);
    return true;
}

void UdpSocket::Close() {
    if (!IsOpen()) return;
#ifdef _WIN32
    closesocket(ToHandle(handle_));
#else
    close(ToHandle(handle_));
#endif
    handle_ = ~0ull;
    port_ = 0;
    ReleaseSockets();
}

bool UdpSocket::IsOpen() const {
    return handle_ != ~0ull;
}

bool UdpSocket::SendTo(const Address& to, const void* data, size_t size) {
    if (!IsOpen()) return false;
    const sockaddr_in addr = ToSockaddr(to);
    const auto sent = sendto(ToHandle(handle_), static_cast<const char*>(data), static_cast<int>(size), 0,
                             reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    return sent >= 0 && static_cast<size_t>(sent) == size;
}

int32 UdpSocket::ReceiveFrom(Address& from, void* buffer, size_t capacity) {
    if (!IsOpen()) return -1;
    sockaddr_in addr{};
    socklen_t length = sizeof(addr);
    const auto received = recvfrom(ToHandle(handle_), static_cast<char*>(buffer), static_cast<int>(capacity), 0,
                                   reinterpret_cast<sockaddr*>(&addr), &length);
    if (received < 0) {
#ifdef _WIN32
        const int error = WSAGetLastError();
        // Windows reports ICMP "port unreachable" from earlier sends as WSAECONNRESET; not fatal for UDP
        if (error == WSAEWOULDBLOCK || error == WSAECONNRESET || error == WSAEMSGSIZE) return 0;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED) return 0;
#endif
        return -1;
    }
    from.ip = ntohl(addr.sin_addr.s_addr);
    from.port = ntohs(addr.sin_port);
    return static_cast<int32>(received);
}

// ============================================================================
// NetWriter / NetReader
// ============================================================================

void NetWriter::WriteU16(uint16 v) {
    buffer_.push_back(static_cast<uint8>(v));
    buffer_.push_back(static_cast<uint8>(v >> 8));
}

void NetWriter::WriteU32(uint32 v) {
    for (int i = 0; i < 4; ++i) buffer_.push_back(static_cast<uint8>(v >> (8 * i)));
}

void NetWriter::WriteU64(uint64 v) {
    for (int i = 0; i < 8; ++i) buffer_.push_back(static_cast<uint8>(v >> (8 * i)));
}

void NetWriter::WriteF32(float32 v) {
    uint32 bits;
    std::memcpy(&bits, &v, sizeof(bits));
    WriteU32(bits);
}

void NetWriter::WriteString(const String& v) {
    const auto length = static_cast<uint16>(std::min<size_t>(v.size(), 65535));
    WriteU16(length);
    WriteBytes(v.data(), length);
}

void NetWriter::WriteBytes(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8*>(data);
    buffer_.insert(buffer_.end(), bytes, bytes + size);
}

void NetWriter::WriteQuantized(float32 v, float32 min, float32 max, uint32 bits) {
    bits = std::clamp(bits, 1u, 32u);
    const uint64 steps = (1ull << bits) - 1;
    const float32 t = max > min ? (std::clamp(v, min, max) - min) / (max - min) : 0.0f;
    const auto q = static_cast<uint32>(std::llround(static_cast<float64>(t) * static_cast<float64>(steps)));
    if (bits <= 8) WriteU8(static_cast<uint8>(q));
    else if (bits <= 16) WriteU16(static_cast<uint16>(q));
    else WriteU32(q);
}

bool NetReader::ReadU8(uint8& v) {
    if (GetRemaining() < 1) return false;
    v = data_[position_++];
    return true;
}

bool NetReader::ReadU16(uint16& v) {
    if (GetRemaining() < 2) return false;
    v = static_cast<uint16>(data_[position_] | (data_[position_ + 1] << 8));
    position_ += 2;
    return true;
}

bool NetReader::ReadU32(uint32& v) {
    if (GetRemaining() < 4) return false;
    v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32>(data_[position_ + i]) << (8 * i);
    position_ += 4;
    return true;
}

bool NetReader::ReadU64(uint64& v) {
    if (GetRemaining() < 8) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64>(data_[position_ + i]) << (8 * i);
    position_ += 8;
    return true;
}

bool NetReader::ReadI32(int32& v) {
    uint32 u;
    if (!ReadU32(u)) return false;
    v = static_cast<int32>(u);
    return true;
}

bool NetReader::ReadF32(float32& v) {
    uint32 bits;
    if (!ReadU32(bits)) return false;
    std::memcpy(&v, &bits, sizeof(v));
    return true;
}

bool NetReader::ReadBool(bool& v) {
    uint8 b;
    if (!ReadU8(b) || b > 1) return false;
    v = b == 1;
    return true;
}

bool NetReader::ReadString(String& v, size_t max_length) {
    uint16 length;
    if (!ReadU16(length) || length > max_length || GetRemaining() < length) return false;
    v.assign(reinterpret_cast<const char*>(data_ + position_), length);
    position_ += length;
    return true;
}

bool NetReader::ReadBytes(void* out, size_t size) {
    if (GetRemaining() < size) return false;
    std::memcpy(out, data_ + position_, size);
    position_ += size;
    return true;
}

bool NetReader::ReadQuantized(float32& v, float32 min, float32 max, uint32 bits) {
    bits = std::clamp(bits, 1u, 32u);
    uint32 q = 0;
    if (bits <= 8) {
        uint8 b;
        if (!ReadU8(b)) return false;
        q = b;
    } else if (bits <= 16) {
        uint16 s;
        if (!ReadU16(s)) return false;
        q = s;
    } else if (!ReadU32(q)) {
        return false;
    }
    const uint64 steps = (1ull << bits) - 1;
    if (q > steps) return false;
    v = min + (max - min) * static_cast<float32>(static_cast<float64>(q) / static_cast<float64>(steps));
    return true;
}

bool NetReader::Skip(size_t size) {
    if (GetRemaining() < size) return false;
    position_ += size;
    return true;
}

// ============================================================================
// Connection
// ============================================================================

namespace {
constexpr size_t kPacketHeaderSize = 2 + 2 + 4 + 1;      // seq, ack, ack bits, message count
constexpr size_t kMessageHeaderSize = 1 + 2 + 2;         // delivery, reliable id, length
constexpr float64 kMinResendInterval = 0.05;
constexpr uint16 kReliableWindow = 512;                  // messages in flight
} // namespace

Connection::Connection() : sent_(kWindow), received_(kWindow, -1) {}

bool Connection::QueueMessage(Delivery delivery, const void* data, size_t size) {
    if (size > kMaxMessageSize) {
        DRX_LOG_ERROR("Network message of {} bytes exceeds the limit of {}", size, kMaxMessageSize);
        return false;
    }
    const auto* bytes = static_cast<const uint8*>(data);
    if (delivery == Delivery::Reliable) {
        if (reliable_out_.size() >= kReliableWindow) {
            DRX_LOG_ERROR("Reliable send window full ({} messages unacknowledged)", reliable_out_.size());
            return false;
        }
        reliable_out_.push_back({next_reliable_id_++, Array<uint8>(bytes, bytes + size)});
    } else {
        unreliable_out_.emplace_back(bytes, bytes + size);
        if (unreliable_out_.size() > 256) unreliable_out_.pop_front();   // stale state is worthless
    }
    stats_.reliable_pending = static_cast<uint32>(reliable_out_.size());
    return true;
}

bool Connection::WritePacket(float64 now, Array<uint8>& out) {
    const float64 resend_interval = std::max(kMinResendInterval, stats_.rtt_ms * 0.0015);

    // Collect what fits: reliable messages due for (re)sending first, then unreliable ones
    Array<PendingReliable*> reliable;
    size_t size = kPacketHeaderSize;
    for (PendingReliable& message : reliable_out_) {
        if (message.acked || (message.last_send_time >= 0.0 && now - message.last_send_time < resend_interval)) continue;
        if (size + kMessageHeaderSize + message.data.size() > kMaxPacketSize || reliable.size() >= 255) break;
        size += kMessageHeaderSize + message.data.size();
        reliable.push_back(&message);
    }
    size_t unreliable_count = 0;
    for (const auto& message : unreliable_out_) {
        if (size + kMessageHeaderSize + message.size() > kMaxPacketSize || reliable.size() + unreliable_count >= 255) break;
        size += kMessageHeaderSize + message.size();
        ++unreliable_count;
    }

    const bool keep_alive_due = last_send_time_ < 0.0 || now - last_send_time_ >= keep_alive_interval;
    if (reliable.empty() && unreliable_count == 0 && !keep_alive_due) return false;

    const uint16 sequence = local_sequence_++;
    SentPacket& record = sent_[sequence % kWindow];
    if (record.valid && !record.acked) AccountLoss(true);   // overwritten without an ack
    record = {};
    record.sequence = sequence;
    record.valid = true;
    record.send_time = now;

    NetWriter writer(out);
    writer.WriteU16(sequence);
    writer.WriteU16(remote_sequence_);
    writer.WriteU32(received_bits_);
    writer.WriteU8(static_cast<uint8>(reliable.size() + unreliable_count));
    for (PendingReliable* message : reliable) {
        writer.WriteU8(static_cast<uint8>(Delivery::Reliable));
        writer.WriteU16(message->id);
        writer.WriteU16(static_cast<uint16>(message->data.size()));
        writer.WriteBytes(message->data.data(), message->data.size());
        message->last_send_time = now;
        record.reliable_ids.push_back(message->id);
    }
    for (size_t i = 0; i < unreliable_count; ++i) {
        const Array<uint8>& message = unreliable_out_.front();
        writer.WriteU8(static_cast<uint8>(Delivery::Unreliable));
        writer.WriteU16(0);
        writer.WriteU16(static_cast<uint16>(message.size()));
        writer.WriteBytes(message.data(), message.size());
        unreliable_out_.pop_front();
    }

    last_send_time_ = now;
    ++stats_.packets_sent;
    stats_.bytes_sent += out.size();
    return true;
}

bool Connection::ReadPacket(float64 now, const uint8* data, size_t size, Array<Message>& delivered) {
    NetReader reader(data, size);
    uint16 sequence, ack;
    uint32 ack_bits;
    uint8 count;
    if (!reader.ReadU16(sequence) || !reader.ReadU16(ack) || !reader.ReadU32(ack_bits) || !reader.ReadU8(count)) {
        return false;
    }

    // Validate the whole packet before applying anything
    struct Parsed {
        Delivery delivery;
        uint16 id;
        const uint8* bytes;
        uint16 length;
    };
    Array<Parsed> messages;
    messages.reserve(count);
    for (uint8 i = 0; i < count; ++i) {
        uint8 delivery;
        uint16 id, length;
        if (!reader.ReadU8(delivery) || delivery > 1 || !reader.ReadU16(id) || !reader.ReadU16(length) ||
            length > kMaxMessageSize || reader.GetRemaining() < length) {
            return false;
        }
        messages.push_back({static_cast<Delivery>(delivery), id, reader.GetCurrent(), length});
        reader.Skip(length);
    }
    if (reader.GetRemaining() != 0) return false;

    // Duplicate packets are acknowledged again but their messages are not delivered twice
    int32& slot = received_[sequence % kWindow];
    const bool duplicate = slot == static_cast<int32>(sequence);
    const bool too_old = received_any_ && SequenceGreater(remote_sequence_, sequence) &&
                         static_cast<uint16>(remote_sequence_ - sequence) >= kWindow;
    if (too_old) return false;
    slot = sequence;

    if (!received_any_) {
        remote_sequence_ = sequence;
        received_bits_ = 0;
        received_any_ = true;
    } else if (SequenceGreater(sequence, remote_sequence_)) {
        const uint16 shift = static_cast<uint16>(sequence - remote_sequence_);
        received_bits_ = shift >= 32 ? 0 : (received_bits_ << shift);
        if (shift <= 32) received_bits_ |= 1u << (shift - 1);   // previous remote_sequence_
        remote_sequence_ = sequence;
    } else if (sequence != remote_sequence_) {
        const uint16 behind = static_cast<uint16>(remote_sequence_ - sequence);
        if (behind <= 32) received_bits_ |= 1u << (behind - 1);
    }

    ProcessAck(ack, ack_bits, now);
    last_receive_time_ = now;
    ++stats_.packets_received;
    stats_.bytes_received += size;
    if (duplicate) return true;

    for (const Parsed& m : messages) {
        if (m.delivery == Delivery::Unreliable) {
            delivered.push_back({Delivery::Unreliable, Array<uint8>(m.bytes, m.bytes + m.length)});
            continue;
        }
        // Reliable: ignore already delivered ids, buffer early ones, release in order
        if (m.id != next_expected_id_ && !SequenceGreater(m.id, next_expected_id_)) continue;
        if (static_cast<uint16>(m.id - next_expected_id_) >= kReliableWindow) continue;
        reliable_in_.emplace(m.id, Array<uint8>(m.bytes, m.bytes + m.length));
        for (auto it = reliable_in_.find(next_expected_id_); it != reliable_in_.end();
             it = reliable_in_.find(next_expected_id_)) {
            delivered.push_back({Delivery::Reliable, std::move(it->second)});
            reliable_in_.erase(it);
            ++next_expected_id_;
        }
    }
    return true;
}

void Connection::ProcessAck(uint16 ack, uint32 ack_bits, float64 now) {
    auto try_ack = [&](uint16 sequence) {
        SentPacket& packet = sent_[sequence % kWindow];
        if (packet.valid && !packet.acked && packet.sequence == sequence) OnPacketAcked(packet, now);
    };
    try_ack(ack);
    for (uint32 i = 0; i < 32; ++i) {
        if (ack_bits & (1u << i)) try_ack(static_cast<uint16>(ack - 1 - i));
    }

    // Unacknowledged packets older than a second count as lost
    for (SentPacket& packet : sent_) {
        if (packet.valid && !packet.acked && now - packet.send_time > 1.0) {
            packet.valid = false;
            AccountLoss(true);
        }
    }

    while (!reliable_out_.empty() && reliable_out_.front().acked) reliable_out_.pop_front();
    stats_.reliable_pending = static_cast<uint32>(reliable_out_.size());
}

void Connection::OnPacketAcked(SentPacket& packet, float64 now) {
    packet.acked = true;
    AccountLoss(false);

    const auto rtt = static_cast<float32>((now - packet.send_time) * 1000.0);
    stats_.rtt_ms = stats_.rtt_ms <= 0.0f ? rtt : stats_.rtt_ms + (rtt - stats_.rtt_ms) * 0.1f;

    for (uint16 id : packet.reliable_ids) {
        for (PendingReliable& message : reliable_out_) {
            if (message.id == id) {
                message.acked = true;
                break;
            }
        }
    }
}

void Connection::AccountLoss(bool lost) {
    stats_.packet_loss += ((lost ? 1.0f : 0.0f) - stats_.packet_loss) * 0.05f;
}

// ============================================================================
// NetEndpoint
// ============================================================================

NetEndpoint::NetEndpoint(const NetConfig& config) : config_(config), rng_(config.conditioner.seed) {}

bool NetEndpoint::PollEvent(NetEvent& event) {
    if (events_.empty()) return false;
    event = std::move(events_.front());
    events_.pop_front();
    return true;
}

void NetEndpoint::SetConditioner(const ConditionerSettings& settings) {
    config_.conditioner = settings;
    rng_.seed(settings.seed);
}

void NetEndpoint::WriteHeader(Array<uint8>& packet, uint8 type) const {
    NetWriter writer(packet);
    writer.WriteU32(config_.protocol_id);
    writer.WriteU8(type);
}

bool NetEndpoint::ReadHeader(NetReader& reader, uint8& type) const {
    uint32 protocol = 0;
    return reader.ReadU32(protocol) && protocol == config_.protocol_id && reader.ReadU8(type);
}

void NetEndpoint::SendRaw(const Address& to, const Array<uint8>& packet, float64 now) {
    const ConditionerSettings& c = config_.conditioner;
    if (!c.IsActive()) {
        socket_.SendTo(to, packet.data(), packet.size());
        return;
    }

    std::uniform_real_distribution<float32> unit(0.0f, 1.0f);
    if (unit(rng_) < c.loss) return;
    const int copies = unit(rng_) < c.duplicate ? 2 : 1;
    for (int i = 0; i < copies; ++i) {
        const float32 jitter = c.jitter_ms > 0.0f ? (unit(rng_) * 2.0f - 1.0f) * c.jitter_ms : 0.0f;
        const float64 delay = std::max(0.0f, c.latency_ms + jitter) / 1000.0;
        Delayed entry{now + delay, to, packet};
        // Keep the queue sorted by release time (jitter may reorder packets, like real networks)
        auto pos = std::upper_bound(delayed_.begin(), delayed_.end(), entry.release_time,
                                    [](float64 t, const Delayed& d) { return t < d.release_time; });
        delayed_.insert(pos, std::move(entry));
    }
}

void NetEndpoint::FlushDelayed(float64 now) {
    while (!delayed_.empty() && delayed_.front().release_time <= now) {
        const Delayed& d = delayed_.front();
        socket_.SendTo(d.to, d.data.data(), d.data.size());
        delayed_.pop_front();
    }
}

// ============================================================================
// NetServer
// ============================================================================

NetServer::~NetServer() {
    Stop();
}

bool NetServer::Start(uint16 port, uint32 max_clients) {
    Stop();
    if (!socket_.Open(port)) return false;
    max_clients_ = std::max(1u, max_clients);
    DRX_LOG_INFO("Server listening on UDP port {}", socket_.GetPort());
    return true;
}

void NetServer::Stop() {
    if (!socket_.IsOpen()) return;
    Array<uint32> ids = GetClientIds();
    for (uint32 id : ids) RemoveClient(id, "server shutdown", true);
    socket_.Close();
    events_.clear();
}

Array<uint32> NetServer::GetClientIds() const {
    Array<uint32> ids;
    for (const auto& [id, client] : clients_) ids.push_back(id);
    return ids;
}

const ConnectionStats* NetServer::GetStats(uint32 client_id) const {
    const auto it = clients_.find(client_id);
    return it != clients_.end() ? &it->second.connection.GetStats() : nullptr;
}

bool NetServer::GetClientAddress(uint32 client_id, Address& out) const {
    const auto it = clients_.find(client_id);
    if (it == clients_.end()) return false;
    out = it->second.address;
    return true;
}

NetServer::Client* NetServer::FindByAddress(const Address& address) {
    for (auto& [id, client] : clients_) {
        if (client.address == address) return &client;
    }
    return nullptr;
}

bool NetServer::Send(uint32 client_id, Delivery delivery, const void* data, size_t size) {
    const auto it = clients_.find(client_id);
    return it != clients_.end() && it->second.connection.QueueMessage(delivery, data, size);
}

void NetServer::Broadcast(Delivery delivery, const void* data, size_t size) {
    for (auto& [id, client] : clients_) client.connection.QueueMessage(delivery, data, size);
}

void NetServer::Disconnect(uint32 client_id, const String& reason) {
    RemoveClient(client_id, reason, true);
}

void NetServer::RemoveClient(uint32 client_id, const String& reason, bool notify_remote) {
    const auto it = clients_.find(client_id);
    if (it == clients_.end()) return;
    if (notify_remote) {
        Array<uint8> packet;
        WriteHeader(packet, kDisconnect);
        // Sent directly (bypassing the conditioner) a few times; the client may never see it otherwise
        for (int i = 0; i < 3; ++i) socket_.SendTo(it->second.address, packet.data(), packet.size());
    }
    PushEvent({NetEventType::Disconnected, client_id, Delivery::Reliable, {}, reason});
    clients_.erase(it);
}

void NetServer::HandlePacket(const Address& from, const uint8* data, size_t size, float64 now) {
    NetReader reader(data, size);
    uint8 type;
    if (!ReadHeader(reader, type)) return;

    Client* client = FindByAddress(from);
    switch (type) {
        case kConnectRequest: {
            uint64 nonce;
            if (!reader.ReadU64(nonce)) return;
            if (client && client->nonce != nonce) {
                // Same address, new session: the old one is gone
                RemoveClient(client->id, "reconnected", false);
                client = nullptr;
            }
            Array<uint8> reply;
            if (!client && clients_.size() >= max_clients_) {
                WriteHeader(reply, kConnectDenied);
                NetWriter(reply).WriteU64(nonce);
                SendRaw(from, reply, now);
                return;
            }
            if (!client) {
                const uint32 id = next_client_id_++;
                Client& added = clients_.emplace(id, Client{id, from, nonce, Connection()}).first->second;
                added.connection.SetLastReceiveTime(now);
                client = &added;
                PushEvent({NetEventType::Connected, id, Delivery::Reliable, {}, from.ToString()});
            }
            client->connection.SetLastReceiveTime(now);
            WriteHeader(reply, kConnectAccept);
            NetWriter writer(reply);
            writer.WriteU64(nonce);
            writer.WriteU32(client->id);
            SendRaw(from, reply, now);
            break;
        }
        case kDisconnect:
            if (client) RemoveClient(client->id, "client disconnected", false);
            break;
        case kPayload: {
            if (!client) return;
            Array<Message> messages;
            if (!client->connection.ReadPacket(now, reader.GetCurrent(), reader.GetRemaining(), messages)) return;
            for (Message& m : messages) {
                PushEvent({NetEventType::Message, client->id, m.delivery, std::move(m.data), {}});
            }
            break;
        }
        default:
            break;
    }
}

void NetServer::Update(float64 now) {
    if (!socket_.IsOpen()) return;

    uint8 buffer[Connection::kMaxPacketSize + 64];
    Address from;
    int32 size;
    while ((size = socket_.ReceiveFrom(from, buffer, sizeof(buffer))) > 0) {
        HandlePacket(from, buffer, static_cast<size_t>(size), now);
    }

    Array<uint32> timed_out;
    for (auto& [id, client] : clients_) {
        if (now - client.connection.GetLastReceiveTime() > config_.timeout_seconds) timed_out.push_back(id);
    }
    for (uint32 id : timed_out) RemoveClient(id, "timeout", true);

    for (auto& [id, client] : clients_) {
        Array<uint8> packet;
        WriteHeader(packet, kPayload);
        if (client.connection.WritePacket(now, packet)) SendRaw(client.address, packet, now);
    }
    FlushDelayed(now);
}

// ============================================================================
// NetClient
// ============================================================================

NetClient::NetClient(const NetConfig& config) : NetEndpoint(config) {}

NetClient::~NetClient() {
    if (state_ != ClientState::Disconnected) Disconnect(0.0);
}

bool NetClient::Connect(const Address& server, float64 now) {
    if (state_ != ClientState::Disconnected) Disconnect(now);
    if (!server.IsValid() || !socket_.Open(0)) return false;

    server_ = server;
    state_ = ClientState::Connecting;
    connection_ = Connection();
    nonce_ = RandomNonce();
    client_id_ = 0;
    connect_start_ = now;
    last_request_ = -1.0;
    return true;
}

void NetClient::Disconnect(float64 /*now*/) {
    if (state_ == ClientState::Disconnected) return;
    if (state_ == ClientState::Connected) {
        Array<uint8> packet;
        WriteHeader(packet, kDisconnect);
        for (int i = 0; i < 3; ++i) socket_.SendTo(server_, packet.data(), packet.size());
        PushEvent({NetEventType::Disconnected, 0, Delivery::Reliable, {}, "disconnected"});
    }
    socket_.Close();
    state_ = ClientState::Disconnected;
}

void NetClient::Fail(const String& reason) {
    const bool was_connected = state_ == ClientState::Connected;
    socket_.Close();
    state_ = ClientState::Disconnected;
    PushEvent({was_connected ? NetEventType::Disconnected : NetEventType::ConnectionFailed, 0, Delivery::Reliable, {},
               reason});
}

bool NetClient::Send(Delivery delivery, const void* data, size_t size) {
    return state_ == ClientState::Connected && connection_.QueueMessage(delivery, data, size);
}

void NetClient::HandlePacket(const uint8* data, size_t size, float64 now) {
    NetReader reader(data, size);
    uint8 type;
    if (!ReadHeader(reader, type)) return;

    switch (type) {
        case kConnectAccept: {
            uint64 nonce;
            uint32 id;
            if (!reader.ReadU64(nonce) || !reader.ReadU32(id) || nonce != nonce_) return;
            if (state_ == ClientState::Connecting) {
                state_ = ClientState::Connected;
                client_id_ = id;
                connection_.SetLastReceiveTime(now);
                PushEvent({NetEventType::Connected, 0, Delivery::Reliable, {}, server_.ToString()});
            }
            break;
        }
        case kConnectDenied: {
            uint64 nonce;
            if (reader.ReadU64(nonce) && nonce == nonce_ && state_ == ClientState::Connecting) Fail("server full");
            break;
        }
        case kDisconnect:
            if (state_ == ClientState::Connected) Fail("disconnected by server");
            break;
        case kPayload: {
            if (state_ != ClientState::Connected) return;
            Array<Message> messages;
            if (!connection_.ReadPacket(now, reader.GetCurrent(), reader.GetRemaining(), messages)) return;
            for (Message& m : messages) PushEvent({NetEventType::Message, 0, m.delivery, std::move(m.data), {}});
            break;
        }
        default:
            break;
    }
}

void NetClient::Update(float64 now) {
    if (state_ == ClientState::Disconnected) return;

    uint8 buffer[Connection::kMaxPacketSize + 64];
    Address from;
    int32 size;
    while (socket_.IsOpen() && (size = socket_.ReceiveFrom(from, buffer, sizeof(buffer))) > 0) {
        if (from == server_) HandlePacket(buffer, static_cast<size_t>(size), now);
    }
    if (state_ == ClientState::Disconnected) return;

    if (state_ == ClientState::Connecting) {
        if (now - connect_start_ > config_.connect_timeout_seconds) {
            Fail("connection timed out");
            return;
        }
        if (last_request_ < 0.0 || now - last_request_ >= config_.connect_retry_seconds) {
            Array<uint8> request;
            WriteHeader(request, kConnectRequest);
            NetWriter(request).WriteU64(nonce_);
            SendRaw(server_, request, now);
            last_request_ = now;
        }
    } else {
        if (now - connection_.GetLastReceiveTime() > config_.timeout_seconds) {
            Fail("timeout");
            return;
        }
        Array<uint8> packet;
        WriteHeader(packet, kPayload);
        if (connection_.WritePacket(now, packet)) SendRaw(server_, packet, now);
    }
    FlushDelayed(now);
}

} // namespace DRX::Network

/**
 * DRX Engine - Network tests (serialization, reliability layer, loopback client/server)
 */

#include <gtest/gtest.h>
#include "Network/Public/NetworkSystem.h"
#include "Network/Public/TickHistory.h"

#include <random>

using namespace DRX;
using namespace DRX::Network;

namespace {

Array<uint8> Bytes(uint32 value) {
    Array<uint8> data;
    NetWriter(data).WriteU32(value);
    return data;
}

uint32 Value(const Array<uint8>& data) {
    NetReader reader(data.data(), data.size());
    uint32 v = 0;
    EXPECT_TRUE(reader.ReadU32(v));
    return v;
}

} // namespace

TEST(NetSerializationTest, RoundTripAndBounds) {
    Array<uint8> buffer;
    NetWriter writer(buffer);
    writer.WriteU8(200);
    writer.WriteU16(65000);
    writer.WriteU32(0xDEADBEEF);
    writer.WriteU64(0x0123456789ABCDEFull);
    writer.WriteI32(-42);
    writer.WriteF32(3.25f);
    writer.WriteBool(true);
    writer.WriteString("hello");
    writer.WriteQuantized(12.34f, -100.0f, 100.0f, 16);

    NetReader reader(buffer.data(), buffer.size());
    uint8 u8; uint16 u16; uint32 u32; uint64 u64; int32 i32; float32 f32, q; bool b; String s;
    ASSERT_TRUE(reader.ReadU8(u8) && reader.ReadU16(u16) && reader.ReadU32(u32) && reader.ReadU64(u64));
    ASSERT_TRUE(reader.ReadI32(i32) && reader.ReadF32(f32) && reader.ReadBool(b) && reader.ReadString(s));
    ASSERT_TRUE(reader.ReadQuantized(q, -100.0f, 100.0f, 16));
    EXPECT_EQ(u8, 200);
    EXPECT_EQ(u16, 65000);
    EXPECT_EQ(u32, 0xDEADBEEFu);
    EXPECT_EQ(u64, 0x0123456789ABCDEFull);
    EXPECT_EQ(i32, -42);
    EXPECT_FLOAT_EQ(f32, 3.25f);
    EXPECT_TRUE(b);
    EXPECT_EQ(s, "hello");
    EXPECT_NEAR(q, 12.34f, 200.0f / 65535.0f);
    EXPECT_EQ(reader.GetRemaining(), 0u);
    EXPECT_FALSE(reader.ReadU8(u8));   // past the end

    const uint8 truncated[] = {5, 0, 'a', 'b'};   // claims 5 bytes, has 2
    NetReader bad(truncated, sizeof(truncated));
    EXPECT_FALSE(bad.ReadString(s));
}

TEST(NetSequenceTest, WrapAroundComparison) {
    EXPECT_TRUE(SequenceGreater(1, 0));
    EXPECT_FALSE(SequenceGreater(0, 1));
    EXPECT_TRUE(SequenceGreater(0, 65535));
    EXPECT_TRUE(SequenceGreater(10, 65000));
    EXPECT_FALSE(SequenceGreater(65000, 10));
}

TEST(NetConnectionTest, ReliableDeliveryUnderLossReorderingAndWrap) {
    Connection a, b;
    std::mt19937 rng(7);
    std::uniform_real_distribution<float32> unit(0.0f, 1.0f);

    struct InFlight { float64 arrive; Array<uint8> data; bool to_b; };
    Array<InFlight> wire;
    Array<uint32> received_reliable;
    uint32 sent_reliable = 0;
    uint32 unreliable_received = 0;

    float64 now = 0.0;
    const float64 dt = 1.0 / 60.0;
    for (int step = 0; step < 72000; ++step) {   // > 65536 packets each way: sequence numbers wrap
        now += dt;
        if (step % 3 == 0 && step < 60000) {
            const Array<uint8> payload = Bytes(sent_reliable++);
            ASSERT_TRUE(a.QueueMessage(Delivery::Reliable, payload.data(), payload.size()));
        }
        if (step % 2 == 0) {
            const Array<uint8> payload = Bytes(0xFFFFFFFF);
            a.QueueMessage(Delivery::Unreliable, payload.data(), payload.size());
        }

        for (bool from_a : {true, false}) {
            Array<uint8> packet;
            if ((from_a ? a : b).WritePacket(now, packet)) {
                if (unit(rng) < 0.2f) continue;   // 20% loss
                const float64 latency = 0.03 + unit(rng) * 0.04;   // 30..70 ms, reorders
                wire.push_back({now + latency, std::move(packet), from_a});
                if (unit(rng) < 0.05f) wire.push_back(wire.back());   // duplicates
            }
        }
        for (size_t i = 0; i < wire.size();) {
            if (wire[i].arrive > now) { ++i; continue; }
            Array<Message> delivered;
            ASSERT_TRUE((wire[i].to_b ? b : a).ReadPacket(now, wire[i].data.data(), wire[i].data.size(), delivered));
            for (const Message& m : delivered) {
                if (m.delivery == Delivery::Reliable) received_reliable.push_back(Value(m.data));
                else ++unreliable_received;
            }
            wire.erase(wire.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }

    ASSERT_EQ(received_reliable.size(), sent_reliable);
    for (uint32 i = 0; i < received_reliable.size(); ++i) {
        ASSERT_EQ(received_reliable[i], i) << "out of order or duplicated";
    }
    EXPECT_EQ(a.GetStats().reliable_pending, 0u);
    EXPECT_GT(unreliable_received, 20000u);
    EXPECT_NEAR(a.GetStats().packet_loss, 0.2f, 0.1f);
    EXPECT_GT(a.GetStats().rtt_ms, 50.0f);
    EXPECT_LT(a.GetStats().rtt_ms, 200.0f);
}

TEST(NetConnectionTest, RejectsMalformedPackets) {
    Connection c;
    Array<Message> out;
    const uint8 short_packet[] = {1, 0, 0};
    EXPECT_FALSE(c.ReadPacket(0.0, short_packet, sizeof(short_packet), out));
    const uint8 lying[] = {1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 200, 0};   // message claims 200 bytes
    EXPECT_FALSE(c.ReadPacket(0.0, lying, sizeof(lying), out));
    EXPECT_TRUE(out.empty());

    Array<uint8> huge(Connection::kMaxMessageSize + 1);
    EXPECT_FALSE(c.QueueMessage(Delivery::Reliable, huge.data(), huge.size()));
}

namespace {

class LoopbackTest : public ::testing::Test {
protected:
    void Pump(NetServer& server, NetClient& client, float64 seconds) {
        const int steps = static_cast<int>(seconds * 60.0);
        for (int i = 0; i < steps; ++i) {
            now += 1.0 / 60.0;
            client.Update(now);
            server.Update(now);
        }
    }

    static Array<NetEvent> Drain(NetEndpoint& endpoint) {
        Array<NetEvent> events;
        NetEvent event;
        while (endpoint.PollEvent(event)) events.push_back(std::move(event));
        return events;
    }

    float64 now = 0.0;
};

} // namespace

TEST_F(LoopbackTest, ConnectExchangeAndDisconnect) {
    NetServer server;
    ASSERT_TRUE(server.Start(0, 4));
    NetClient client;
    ASSERT_TRUE(client.Connect(Address::Loopback(server.GetPort()), now));
    Pump(server, client, 0.5);

    ASSERT_TRUE(client.IsConnected());
    auto server_events = Drain(server);
    ASSERT_EQ(server_events.size(), 1u);
    EXPECT_EQ(server_events[0].type, NetEventType::Connected);
    const uint32 id = server_events[0].client_id;
    EXPECT_EQ(client.GetClientId(), id);
    EXPECT_EQ(Drain(client).front().type, NetEventType::Connected);

    const String hello = "hello server";
    ASSERT_TRUE(client.Send(Delivery::Reliable, hello.data(), hello.size()));
    const String state = "snapshot";
    server.Send(id, Delivery::Unreliable, state.data(), state.size());
    Pump(server, client, 0.2);

    server_events = Drain(server);
    ASSERT_EQ(server_events.size(), 1u);
    EXPECT_EQ(String(server_events[0].data.begin(), server_events[0].data.end()), hello);
    auto client_events = Drain(client);
    ASSERT_EQ(client_events.size(), 1u);
    EXPECT_EQ(String(client_events[0].data.begin(), client_events[0].data.end()), state);
    EXPECT_GE(server.GetStats(id)->packets_received, 1u);

    client.Disconnect(now);
    Pump(server, client, 0.2);
    server_events = Drain(server);
    ASSERT_EQ(server_events.size(), 1u);
    EXPECT_EQ(server_events[0].type, NetEventType::Disconnected);
    EXPECT_EQ(server.GetClientCount(), 0u);
}

TEST_F(LoopbackTest, ReliableOrderWithConditioner) {
    NetConfig config;
    config.conditioner = {40.0f, 15.0f, 0.25f, 0.05f, 3};
    NetServer server(config);
    NetClient client(config);
    ASSERT_TRUE(server.Start(0));
    ASSERT_TRUE(client.Connect(Address::Loopback(server.GetPort()), now));
    Pump(server, client, 1.0);
    ASSERT_TRUE(client.IsConnected());
    Drain(server);

    for (uint32 i = 0; i < 300; ++i) {
        const Array<uint8> payload = Bytes(i);
        ASSERT_TRUE(client.Send(Delivery::Reliable, payload.data(), payload.size()));
        if (i % 10 == 0) Pump(server, client, 1.0 / 60.0);
    }
    Pump(server, client, 3.0);

    Array<uint32> received;
    for (const NetEvent& e : Drain(server)) {
        if (e.type == NetEventType::Message) received.push_back(Value(e.data));
    }
    ASSERT_EQ(received.size(), 300u);
    for (uint32 i = 0; i < 300; ++i) ASSERT_EQ(received[i], i);
    EXPECT_GT(client.GetStats().rtt_ms, 60.0f);   // ~2 x 40 ms
}

TEST_F(LoopbackTest, ServerFullAndUnreachableServer) {
    NetServer server;
    ASSERT_TRUE(server.Start(0, 1));
    NetClient first, second;
    ASSERT_TRUE(first.Connect(Address::Loopback(server.GetPort()), now));
    ASSERT_TRUE(second.Connect(Address::Loopback(server.GetPort()), now));
    for (int i = 0; i < 30; ++i) {
        now += 1.0 / 60.0;
        first.Update(now);
        second.Update(now);
        server.Update(now);
    }
    EXPECT_TRUE(first.IsConnected());
    EXPECT_FALSE(second.IsConnected());
    auto events = Drain(second);
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().type, NetEventType::ConnectionFailed);

    NetConfig quick;
    quick.connect_timeout_seconds = 0.5f;
    NetClient lonely(quick);
    UdpSocket unused;
    ASSERT_TRUE(unused.Open(0));   // a port nobody answers on
    ASSERT_TRUE(lonely.Connect(Address::Loopback(unused.GetPort()), now));
    for (int i = 0; i < 60; ++i) {
        now += 1.0 / 60.0;
        lonely.Update(now);
    }
    events = Drain(lonely);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, NetEventType::ConnectionFailed);
}

TEST_F(LoopbackTest, SilentClientTimesOut) {
    NetConfig config;
    config.timeout_seconds = 1.0f;
    NetServer server(config);
    NetClient client(config);
    ASSERT_TRUE(server.Start(0));
    ASSERT_TRUE(client.Connect(Address::Loopback(server.GetPort()), now));
    Pump(server, client, 0.3);
    ASSERT_TRUE(client.IsConnected());
    Drain(server);

    for (int i = 0; i < 120; ++i) {   // client stops updating
        now += 1.0 / 60.0;
        server.Update(now);
    }
    const auto events = Drain(server);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, NetEventType::Disconnected);
    EXPECT_EQ(events[0].reason, "timeout");
}

TEST(AddressTest, ParseAndFormat) {
    Address address;
    ASSERT_TRUE(Address::Parse("192.168.1.20:27015", address));
    EXPECT_EQ(address, Address::FromIPv4(192, 168, 1, 20, 27015));
    EXPECT_EQ(address.ToString(), "192.168.1.20:27015");
    EXPECT_TRUE(Address::Parse("localhost:5000", address));
    EXPECT_EQ(address.port, 5000);
    EXPECT_FALSE(Address::Parse("127.0.0.1", address));
    EXPECT_FALSE(Address::Parse("127.0.0.1:99999", address));
    EXPECT_FALSE(Address::Parse(":80", address));
}

TEST(TickHistoryTest, StoreFindAndInterpolate) {
    TickHistory<float32, 16> history;
    EXPECT_FALSE(history.HasLatest());
    history.Store(10, 1.0f);
    history.Store(12, 3.0f);
    history.Store(13, 4.0f);

    ASSERT_NE(history.Find(12), nullptr);
    EXPECT_EQ(*history.Find(12), 3.0f);
    EXPECT_EQ(history.Find(11), nullptr);
    EXPECT_EQ(history.GetLatestTick(), 13u);

    const float32* from = nullptr;
    const float32* to = nullptr;
    float32 alpha = 0.0f;
    ASSERT_TRUE(history.FindBracket(11.0, from, to, alpha));   // gap between 10 and 12
    EXPECT_EQ(*from, 1.0f);
    EXPECT_EQ(*to, 3.0f);
    EXPECT_FLOAT_EQ(alpha, 0.5f);
    ASSERT_TRUE(history.FindBracket(12.25, from, to, alpha));
    EXPECT_FLOAT_EQ(*from + (*to - *from) * alpha, 3.25f);
    EXPECT_FALSE(history.FindBracket(14.0, from, to, alpha));

    history.Store(10 + 16, 9.0f);   // overwrites tick 10's slot
    EXPECT_EQ(history.Find(10), nullptr);
    history.DiscardBefore(13);
    EXPECT_EQ(history.Find(12), nullptr);
    EXPECT_NE(history.Find(13), nullptr);
}

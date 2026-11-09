// Forg3 relay: rooms reachable from any network through the relay server.
#include "harness.hpp"

#include <nexlevel/core/relay/protocol.hpp>
#include <nexlevel/core/relay/relay_server.hpp>
#include <nexlevel/core/session/client.hpp>
#include <nexlevel/core/session/host.hpp>
#include <nexlevel/core/session/room_code.hpp>

#include <asio.hpp>

#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <thread>
#include <system_error>

using namespace nexlevel;
using namespace nexlevel::session;
using sync::Op;
using sync::PropMap;

namespace {

bool waitFor(const std::function<void()>& pump, const std::function<bool()>& done, int ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        pump();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

std::string relayCode(const SessionHost& host) {
    RoomAddress a;
    a.relayRoom = host.relayRoomId();
    a.relayId = host.relayId();
    a.key = host.roomKey();
    return encodeRoomCode(a);
}

void testRelayCode() {
    RoomAddress a;
    a.relayRoom = 0x1EADBEEF;
    a.relayId = 7;
    a.key = 2047;
    auto code = encodeRoomCode(a);
    CHECK(code.size() == 11);  // 10 symbols + dash
    auto back = decodeRoomCode(code);
    CHECK(back && back->relayRoom == 0x1EADBEEF && back->key == 2047 && back->relayId == 7);
    int caught = 0, total = 0;
    for (std::size_t i = 0; i < code.size(); ++i) {
        if (code[i] == '-') continue;
        for (char c : std::string("0123456789ABCDEFGHJKMNPQRSTVWXYZ")) {
            if (c == code[i]) continue;
            auto bad = code;
            bad[i] = c;
            ++total;
            if (!decodeRoomCode(bad)) ++caught;
        }
    }
    std::printf("  relay code %s, %d/%d single-char typos caught\n", code.c_str(), caught, total);
    CHECK(caught * 100 >= total * 90);
}

void testRelaySession() {
    relay::RelayServer relayServer;
    std::string err;
    auto rport = relayServer.start(0, true, &err);
    CHECK(rport.has_value());
    if (!rport) return;

    SessionHost host(GameTarget::GD2208, "Host");
    CHECK(host.start(0, true));
    for (int i = 0; i < 20; ++i) host.submitLocal(Op::create(host.newId(), PropMap{{1, "1"}, {2, std::to_string(i * 30)}, {3, "105"}}));
    host.startRelay("127.0.0.1", *rport, 3);  // relay id 3 in the directory
    CHECK(waitFor([&] { host.pump(); }, [&] { return host.relayState() == RelayState::Ready; }, 3000));
    // The host measures its own round trip to the relay (shown in its Network tab).
    CHECK(waitFor([&] { host.pump(); }, [&] {
        auto st = host.relayPingStats();
        return st && st->ms.has_value();
    }, 4000));
    CHECK(host.relayRoomId() != 0);
    const auto code = relayCode(host);

    std::vector<std::unique_ptr<SessionClient>> clients;
    for (int i = 0; i < 3; ++i) {
        clients.push_back(std::make_unique<SessionClient>(GameTarget::GD2208, "Far" + std::to_string(i)));
        clients.back()->setRelayDirectory({relay::RelayEndpoint{3, "test", "127.0.0.1", *rport}});
        CHECK(clients.back()->connectCode(code));
    }
    auto pumpAll = [&] { host.pump(); for (auto& c : clients) c->pump(); };
    bool joined = waitFor(pumpAll, [&] {
        for (auto& c : clients) if (c->state() != ClientState::Joined) return false;
        return true;
    }, 5000);
    CHECK(joined);
    if (joined) {  // public counters: 1 room, host + 3 players
        auto counters = relay::queryRelayStats({relay::RelayEndpoint{3, "test", "127.0.0.1", *rport}}, std::chrono::milliseconds(1500));
        CHECK(counters.size() == 1 && counters[0].has_value() && counters[0]->rooms == 1 && counters[0]->players == 4);
    }
    if (!joined) {
        for (auto& c : clients) std::printf("  client state %d err '%s'\n", static_cast<int>(c->state()), c->lastError().c_str());
        return;
    }
    CHECK(host.peers().size() == 4);
    CHECK(relayServer.stats().pipes == 3);

    // Everyone edits; all converge through the relay.
    for (int round = 0; round < 30; ++round) {
        host.submitLocal(Op::create(host.newId(), PropMap{{1, "8"}, {2, std::to_string(round)}, {3, "15"}}));
        for (auto& c : clients) {
            if (auto id = c->newId()) c->submitLocal(Op::create(*id, PropMap{{1, "1"}, {2, std::to_string(900 + round)}, {3, "45"}}));
        }
    }
    bool converged = waitFor(pumpAll, [&] {
        for (auto& c : clients) {
            if (!c->replica()->pending().empty() || c->replica()->view().fingerprint() != host.state().fingerprint()) return false;
        }
        return true;
    }, 5000);
    CHECK(converged);
    CHECK(host.state().objects().size() == 20 + 30 + 90);

    // The relay's control link drops: the host reclaims the SAME room, players stay connected,
    // and new players can still join with the same code.
    const auto roomBefore = host.relayRoomId();
    host.simulateRelayDrop();
    CHECK(waitFor(pumpAll, [&] { return host.relayState() != RelayState::Ready; }, 3000));
    CHECK(waitFor(pumpAll, [&] { return host.relayState() == RelayState::Ready; }, 6000));
    CHECK(host.relayRoomId() == roomBefore);
    for (auto& c : clients) CHECK(c->state() == ClientState::Joined);
    SessionClient late(GameTarget::GD2208, "Late");
    late.setRelayDirectory({relay::RelayEndpoint{3, "test", "127.0.0.1", *rport}});
    CHECK(late.connectCode(code));
    CHECK(waitFor([&] { pumpAll(); late.pump(); }, [&] { return late.state() == ClientState::Joined; }, 5000));
    CHECK(late.replica() && late.replica()->view().objects().size() == host.state().objects().size());

    auto st = relayServer.stats();
    std::printf("  relay: %zu room(s), %zu pipe(s), %llu bytes relayed\n", st.rooms, st.pipes, static_cast<unsigned long long>(st.bytesRelayed));
    CHECK(st.bytesRelayed > 0);

    host.stop();
    bool allClosed = waitFor([&] { for (auto& c : clients) c->pump(); late.pump(); }, [&] {
        for (auto& c : clients) if (c->state() != ClientState::RoomClosed) return false;
        return late.state() == ClientState::RoomClosed;
    }, 3000);
    CHECK(allClosed);  // the Closing notice travels through the relay too
    relayServer.stop();
}

void testRelayErrors() {
    relay::RelayServer::Options opt;
    opt.pairTimeout = std::chrono::seconds(1);
    relay::RelayServer relayServer(opt);
    auto rport = relayServer.start(0, true);
    CHECK(rport.has_value());
    if (!rport) return;

    // Unknown room.
    SessionClient c(GameTarget::GD2208, "Lost");
    c.setRelayServer("127.0.0.1", *rport);
    RoomAddress a;
    a.relayRoom = 12345;
    a.relayId = kCustomRelayId;
    a.key = 7;
    CHECK(c.connectCode(encodeRoomCode(a)));
    CHECK(waitFor([&] { c.pump(); }, [&] { return c.state() == ClientState::Disconnected; }, 5000));
    CHECK(c.lastError().find("room not found") != std::string::npos);

    // No relay configured: relay codes are refused up front.
    SessionClient noRelay(GameTarget::GD2208, "X");
    CHECK(!noRelay.connectCode(encodeRoomCode(a)));
    // A relay id this build does not know (newer directory): clear message, no connection.
    RoomAddress future = a;
    future.relayId = 9;
    SessionClient old(GameTarget::GD2208, "Old");
    old.setRelayDirectory({relay::RelayEndpoint{1, "eu", "127.0.0.1", *rport}});
    CHECK(!old.connectCode(encodeRoomCode(future)));
    CHECK(old.lastError().find("unknown relay") != std::string::npos);

    // Latency probe: the live relay answers, a closed port does not.
    auto probe = relay::probeRelays({relay::RelayEndpoint{1, "live", "127.0.0.1", *rport}, relay::RelayEndpoint{2, "dead", "127.0.0.1", 1}},
                                    std::chrono::milliseconds(1500));
    CHECK(probe.size() == 2 && probe[0].has_value() && !probe[1].has_value());
    if (probe[0]) std::printf("  relay probe: %.2f ms\n", *probe[0]);

    // A "host" that opens a room but never answers: the player gets a timeout, not a hang.
    asio::io_context io;
    asio::ip::tcp::socket ctl(io);
    ctl.connect({asio::ip::make_address_v4("127.0.0.1"), *rport});
    asio::write(ctl, asio::buffer(relay::encode(relay::Message{relay::Kind::OpenRoom})));
    std::array<std::uint8_t, relay::kHeaderSize + 12> opened{};
    asio::read(ctl, asio::buffer(opened));
    auto kind = relay::decodeHeader(std::span(opened.data(), relay::kHeaderSize));
    CHECK(kind == relay::Kind::RoomOpened);
    auto msg = relay::decodeBody(relay::Kind::RoomOpened, std::span(opened.data() + relay::kHeaderSize, 12));
    CHECK(msg && msg->roomId != 0);
    if (msg) {
        SessionClient waiting(GameTarget::GD2208, "Waiting");
        waiting.setRelayServer("127.0.0.1", *rport);
        RoomAddress b;
        b.relayRoom = msg->roomId;
        b.relayId = kCustomRelayId;
        CHECK(waiting.connectCode(encodeRoomCode(b)));
        CHECK(waitFor([&] { waiting.pump(); }, [&] { return waiting.state() == ClientState::Disconnected; }, 5000));
        CHECK(waiting.lastError().find("did not answer") != std::string::npos);
    }

    // Garbage is dropped.
    asio::ip::tcp::socket junk(io);
    junk.connect({asio::ip::make_address_v4("127.0.0.1"), *rport});
    asio::write(junk, asio::buffer(std::string("GET / HTTP/1.1\r\n\r\n")));
    std::array<char, 16> buf{};
    std::error_code ec;
    junk.read_some(asio::buffer(buf), ec);
    CHECK(ec == asio::error::eof || ec == asio::error::connection_reset || ec == asio::error::connection_aborted);

    relayServer.stop();
}

} // namespace

void runRelayTests() {
    testRelayCode();
    testRelaySession();
    testRelayErrors();
}

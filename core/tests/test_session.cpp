// Real loopback session over TCP: embedded host + several clients editing concurrently.
#include "harness.hpp"

#include <nexlevel/core/session/client.hpp>
#include <nexlevel/core/session/host.hpp>
#include <nexlevel/core/net/local_address.hpp>
#include <nexlevel/core/session/room_code.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <random>
#include <thread>
#include <vector>
#include <algorithm>
#include <cctype>

using namespace nexlevel;
using namespace nexlevel::session;
using namespace nexlevel::sync;

namespace {

bool waitUntil(const std::function<void()>& pumpAll, const std::function<bool()>& done, int timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        pumpAll();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

NetId randomVisible(const Replica& r, std::mt19937& rng) {
    const auto& objs = r.objects();
    if (objs.empty()) return NetId{};
    auto it = objs.begin();
    std::advance(it, rng() % objs.size());
    return it->first;
}

Op randomOp(const Replica& visible, NetId fresh, std::mt19937& rng) {
    NetId target = randomVisible(visible, rng);
    int roll = static_cast<int>(rng() % 100);
    if (!target.valid() || roll < 25) {
        return Op::create(fresh, PropMap{{1, std::to_string(rng() % 8 + 1)}, {2, std::to_string(rng() % 900)}, {3, "105"}});
    }
    if (roll < 35) return Op::erase(target);
    PropDiff d;
    d.changed.push_back({static_cast<int>(rng() % 4 + 2), std::to_string(rng() % 1000)});
    return Op::setProps(target, d);
}

void testLoopbackSession() {
    SessionHost host(GameTarget::GD2208, "Host");
    std::string err;
    auto port = host.start(0, true, &err);
    CHECK(port.has_value());
    if (!port) { std::printf("  host start failed: %s\n", err.c_str()); return; }

    // Content created before anyone joins must reach late joiners (initial sync via Welcome).
    for (int i = 0; i < 5; ++i) {
        host.submitLocal(Op::create(host.newId(), PropMap{{1, "1"}, {2, std::to_string(i * 30)}, {3, "105"}}));
    }

    std::vector<std::unique_ptr<SessionClient>> clients;
    for (int i = 0; i < 3; ++i) {
        clients.push_back(std::make_unique<SessionClient>(GameTarget::GD2208, "Player" + std::to_string(i + 1)));
        if (i == 0) {
            clients.back()->connect("127.0.0.1", *port, host.roomKey());
        } else {  // the others join with the room code, like players will
            CHECK(clients.back()->connectCode(encodeRoomCode(RoomAddress{{127, 0, 0, 1}, *port, host.roomKey()})));
        }
    }
    auto pumpAll = [&] {
        host.pump();
        for (auto& c : clients) c->pump();
    };
    bool joined = waitUntil(pumpAll, [&] {
        for (auto& c : clients) if (c->state() != ClientState::Joined) return false;
        return true;
    }, 5000);
    CHECK(joined);
    if (!joined) {
        for (auto& c : clients) std::printf("  client state %d err '%s'\n", static_cast<int>(c->state()), c->lastError().c_str());
        return;
    }
    CHECK(host.peers().size() == 4);
    for (auto& c : clients) CHECK(c->replica()->view().objects().size() == 5);
    bool rosterSeen = waitUntil(pumpAll, [&] {
        for (auto& c : clients) if (c->peers().size() != 4) return false;
        return true;
    }, 5000);
    CHECK(rosterSeen);
    CHECK(clients[0]->peers().front().id == SessionHost::kHostPeer && clients[0]->peers().front().name == "Host");

    // Concurrent random editing by everyone.
    std::mt19937 rng(1234);
    for (int step = 0; step < 300; ++step) {
        if (rng() % 3 == 0) host.submitLocal(randomOp(host.state(), host.newId(), rng));
        for (auto& c : clients) {
            if (rng() % 2 == 0) c->submitLocal(randomOp(c->replica()->view(), *c->newId(), rng));
        }
        pumpAll();
        if (step % 10 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    bool converged = waitUntil(pumpAll, [&] {
        const auto truth = host.state().fingerprint();
        for (auto& c : clients) {
            const auto* r = c->replica();
            if (!r->pending().empty() || r->confirmed().fingerprint() != truth || r->view().fingerprint() != truth) return false;
        }
        return true;
    }, 10000);
    CHECK(converged);
    CHECK(host.state().lastSeq() > 300);
    std::printf("  loopback session: %zu objects, %llu ops sequenced, converged=%d\n",
        host.state().objects().size(), static_cast<unsigned long long>(host.state().lastSeq()), converged ? 1 : 0);

    // V1 rule: a player on another GD version is refused with a clear reason.
    SessionClient other(GameTarget::GD2209, "Other");
    other.connect("127.0.0.1", *port, host.roomKey());
    bool refused = waitUntil([&] { host.pump(); other.pump(); for (auto& c : clients) c->pump(); },
        [&] { return other.state() == ClientState::Refused; }, 5000);
    CHECK(refused);
    CHECK(other.refuseReason() == RefuseReason::WrongGameVersion);
    CHECK(other.hostTarget() == GameTarget::GD2208);
    CHECK(host.peers().size() == 4);

    // A wrong room code is refused even though the address is right.
    SessionClient intruder(GameTarget::GD2208, "Intruder");
    intruder.connect("127.0.0.1", *port, static_cast<std::uint16_t>((host.roomKey() + 1) & kRoomKeyMask));
    bool keyRefused = waitUntil([&] { host.pump(); intruder.pump(); for (auto& c : clients) c->pump(); },
        [&] { return intruder.state() == ClientState::Refused; }, 5000);
    CHECK(keyRefused);
    CHECK(intruder.refuseReason() == RefuseReason::WrongRoomCode);
    CHECK(host.peers().size() == 4);

    // A client leaving is noticed by the host and by everyone's roster.
    clients.pop_back();
    bool left = waitUntil(pumpAll, [&] {
        if (host.peers().size() != 3) return false;
        for (auto& c : clients) if (c->peers().size() != 3) return false;
        return true;
    }, 5000);
    CHECK(left);

    // Host kicks a player.
    auto victim = *clients[0]->peerId();
    CHECK(host.kick(victim));
    CHECK(!host.kick(victim));
    bool kicked = waitUntil(pumpAll, [&] {
        return clients[0]->state() == ClientState::Kicked && clients[1]->peers().size() == 2;
    }, 5000);
    CHECK(kicked);
    CHECK(host.peers().size() == 2);

    // The room keeps working without the kicked player.
    clients[1]->submitLocal(Op::create(*clients[1]->newId(), PropMap{{1, "1"}, {2, "0"}, {3, "0"}}));
    bool stillWorks = waitUntil(pumpAll, [&] {
        return clients[1]->replica()->pending().empty() && clients[1]->replica()->confirmed().fingerprint() == host.state().fingerprint();
    }, 5000);
    CHECK(stillWorks);

    // Host closes the room: remaining players are told it was closed (not a network error).
    host.stop();
    bool closed = waitUntil(pumpAll, [&] { return clients[1]->state() == ClientState::RoomClosed; }, 5000);
    CHECK(closed);
}

void testConnectFailure() {
    SessionClient c(GameTarget::GD2208, "Nobody");
    c.connect("127.0.0.1", 1, 0);  // nothing listens on port 1
    bool failed = waitUntil([&] { c.pump(); }, [&] { return c.state() == ClientState::Disconnected; }, 5000);
    CHECK(failed);
    CHECK(!c.lastError().empty());
}

} // namespace

void testLocalAddress() {
    auto ip = net::primaryLocalIPv4();
    // Offline CI machines may legitimately have no route; when present it must look like a dotted quad.
    if (ip) CHECK(std::count(ip->begin(), ip->end(), '.') == 3);
    std::printf("  primary LAN IPv4: %s\n", ip ? ip->c_str() : "(none)");
}

void testRoomCodes() {
    RoomAddress a{{192, 168, 1, 23}, 47777, 1337};
    auto code = encodeRoomCode(a);
    CHECK(code.size() == 15 && code[4] == '-' && code[9] == '-');
    CHECK(decodeRoomCode(code) == a);
    // Case, dashes and spaces don't matter; Crockford aliases are accepted.
    std::string lower;
    for (char ch : code) if (ch != '-') lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    CHECK(decodeRoomCode(lower) == a);
    CHECK(decodeRoomCode(" " + code.substr(0, 4) + " " + code.substr(5)) == a);
    // Extremes round-trip.
    RoomAddress b{{255, 255, 255, 255}, 65535, kRoomKeyMask};
    CHECK(decodeRoomCode(encodeRoomCode(b)) == b);
    RoomAddress c{{10, 0, 0, 1}, 1, 0};
    CHECK(decodeRoomCode(encodeRoomCode(c)) == c);
    // Single-character typos are caught by the checksum (most of them).
    const std::string alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    int caught = 0, total = 0;
    for (std::size_t pos = 0; pos < code.size(); ++pos) {
        if (code[pos] == '-') continue;
        for (char rep : alphabet) {
            if (rep == code[pos]) continue;
            auto typo = code;
            typo[pos] = rep;
            ++total;
            if (!decodeRoomCode(typo)) ++caught;
        }
    }
    CHECK(caught * 100 / total >= 90);
    std::printf("  room code %s, %d/%d single-char typos caught\n", code.c_str(), caught, total);
    CHECK(!decodeRoomCode(""));
    CHECK(!decodeRoomCode("K7Q2-M9XA"));
    CHECK(!decodeRoomCode("K7Q2-M9XA-3PF1Q-ZZ"));
    CHECK(!decodeRoomCode("K7Q2-M9XA-3PF1U"));  // 'U' is not in the alphabet
    CHECK(parseIPv4("192.168.1.23") == (std::array<std::uint8_t, 4>{192, 168, 1, 23}));
    CHECK(!parseIPv4("192.168.1") && !parseIPv4("256.1.1.1") && !parseIPv4("a.b.c.d"));
}

void testLargeLevelJoin() {
    SessionHost host(GameTarget::GD2208, "Host");
    auto port = host.start(0, true);
    CHECK(port.has_value());
    if (!port) return;
    const int n = 50000;
    for (int i = 0; i < n; ++i) {
        host.submitLocal(Op::create(host.newId(), PropMap{{1, std::to_string(i % 900 + 1)}, {2, std::to_string(i * 30)},
                                                          {3, std::to_string(105 + (i % 20) * 30)}, {57, "1.2.3"}}));
    }
    for (int i = 0; i < 500; ++i) host.submitLocal(Op::erase(NetId::make(SessionHost::kHostPeer, static_cast<std::uint64_t>(i * 7 + 1))));
    SessionClient c(GameTarget::GD2208, "Late");
    auto t0 = std::chrono::steady_clock::now();
    c.connect("127.0.0.1", *port, host.roomKey());
    bool joined = waitUntil([&] { host.pump(); c.pump(); }, [&] { return c.state() == ClientState::Joined; }, 30000);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(joined);
    if (!joined) { std::printf("  large join failed: state %d err '%s'\n", static_cast<int>(c.state()), c.lastError().c_str()); return; }
    CHECK(c.replica()->view().objects().size() == static_cast<std::size_t>(n - 500));
    CHECK(c.replica()->confirmed().fingerprint() == host.state().fingerprint());
    std::printf("  large level join: %d objects + 500 deleted in %lld ms\n", n - 500, static_cast<long long>(ms));
}

void testReconnect() {
    SessionHost host(GameTarget::GD2208, "Host");
    auto port = host.start(0, true);
    CHECK(port.has_value());
    if (!port) return;
    std::vector<std::unique_ptr<SessionClient>> clients;
    for (int i = 0; i < 2; ++i) {
        clients.push_back(std::make_unique<SessionClient>(GameTarget::GD2208, "P" + std::to_string(i)));
        clients.back()->connect("127.0.0.1", *port, host.roomKey());
    }
    auto pumpAll = [&] { host.pump(); for (auto& c : clients) c->pump(); };
    CHECK(waitUntil(pumpAll, [&] { return clients[0]->state() == ClientState::Joined && clients[1]->state() == ClientState::Joined; }, 5000));
    auto idBefore = *clients[0]->peerId();

    std::mt19937 rng(77);
    int drops = 0, reconnects = 0, editsWhileDown = 0;
    bool wasDown = false;
    int sinceUp = 0;
    for (int step = 0; step < 4000 && reconnects < 4; ++step) {
        // Drop the line again only once the client is back in and has been editing for a while.
        if (clients[0]->state() == ClientState::Joined && ++sinceUp > 60 && drops < 4) {
            clients[0]->simulateNetworkDrop();
            ++drops;
            sinceUp = 0;
        }
        for (auto& c : clients) {
            if (!c->replica()) continue;
            if (rng() % 2 == 0) {
                auto st = c->state();
                if (c->submitLocal(randomOp(c->replica()->view(), *c->newId(), rng)) && st == ClientState::Reconnecting) ++editsWhileDown;
            }
        }
        if (rng() % 3 == 0) host.submitLocal(randomOp(host.state(), host.newId(), rng));
        pumpAll();
        bool down = clients[0]->state() == ClientState::Reconnecting;
        if (wasDown && clients[0]->state() == ClientState::Joined) ++reconnects;
        wasDown = down;
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    bool converged = waitUntil(pumpAll, [&] {
        const auto truth = host.state().fingerprint();
        for (auto& c : clients) {
            if (c->state() != ClientState::Joined) return false;
            const auto* r = c->replica();
            if (!r->pending().empty() || r->confirmed().fingerprint() != truth || r->view().fingerprint() != truth) return false;
        }
        return true;
    }, 15000);
    CHECK(drops == 4);
    CHECK(reconnects == drops);
    CHECK(editsWhileDown > 0);
    CHECK(converged);
    CHECK(*clients[0]->peerId() == idBefore);  // same identity after every reconnect
    CHECK(!clients[0]->takeIdentityReset());
    CHECK(host.peers().size() == 3);
    std::printf("  reconnect: %d drops, %d reconnects, %d edits made while disconnected, converged=%d\n", drops, reconnects,
        editsWhileDown, converged ? 1 : 0);

    // A kicked player must not come back on its own.
    CHECK(host.kick(*clients[1]->peerId()));
    bool kicked = waitUntil(pumpAll, [&] { return clients[1]->state() == ClientState::Kicked; }, 5000);
    CHECK(kicked);
    for (int i = 0; i < 200; ++i) { pumpAll(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    CHECK(clients[1]->state() == ClientState::Kicked && host.peers().size() == 2);
}

void runSessionTests() {
    testRoomCodes();
    testLargeLevelJoin();
    testReconnect();
    testLocalAddress();
    testLoopbackSession();
    testConnectFailure();
}

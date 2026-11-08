// Ping: measured both ways, shared in the room, quality rating; GD account ids in the roster.
#include "harness.hpp"

#include <nexlevel/core/session/client.hpp>
#include <nexlevel/core/session/host.hpp>
#include <nexlevel/core/session/ping.hpp>

#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>

using namespace nexlevel;
using namespace nexlevel::session;

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

void testTracker() {
    RttTracker t;
    auto now = RttTracker::Clock::now();
    auto p1 = t.due(now);
    CHECK(p1.has_value());
    CHECK(!t.due(now + std::chrono::milliseconds(500)));  // once per second
    // Answered after 40 ms, of which the other side held it 10 ms -> 30 ms.
    t.onPong(Pong{p1->seq, p1->sentMicros, 10000}, now + std::chrono::milliseconds(40));
    auto s = t.stats();
    CHECK(s.ms && *s.ms > 29.f && *s.ms < 31.f);
    CHECK(qualityFor(s) == Quality::Demon);
    t.onPong(Pong{p1->seq, p1->sentMicros, 0}, now + std::chrono::seconds(1));  // duplicate ignored
    CHECK(t.stats().history.size() == 1);
    // Unanswered pings count as loss.
    for (int i = 1; i <= 8; ++i) t.due(now + std::chrono::seconds(i));
    CHECK(t.stats().lossPercent > 0.f);
    PingStats bad;
    bad.ms = 300.f;
    bad.avgMs = 300.f;
    CHECK(qualityFor(bad) == Quality::Bad);
    CHECK(qualityFor(PingStats{}) == Quality::Unknown);
}

void testSessionPing() {
    SessionHost host(GameTarget::GD2208, "Host");
    host.setAccountId(4242);
    auto port = host.start(0, true);
    CHECK(port.has_value());
    if (!port) return;
    SessionClient a(GameTarget::GD2208, "A");
    a.setAccountId(777);
    a.connect("127.0.0.1", *port, host.roomKey());
    auto pumpAll = [&] { host.pump(); a.pump(); };
    bool joined = waitFor(pumpAll, [&] { return a.state() == ClientState::Joined && a.peers().size() == 2; }, 3000);
    CHECK(joined);
    if (!joined) { std::printf("  ping test: state %d err %s\n", static_cast<int>(a.state()), a.lastError().c_str()); host.stop(); return; }
    CHECK(a.peers()[0].accountId == 4242 && a.peers()[1].accountId == 777);
    CHECK(host.peers()[1].accountId == 777);
    CHECK(a.connectedVia() && a.connectedVia()->port == *port && a.connectedVia()->relayRoom == 0);

    // Within a few seconds: client measured its ping, host measured the client, table shared.
    bool measured = waitFor(pumpAll, [&] {
        auto hs = host.pingStats(*a.peerId());
        return a.pingStats().ms && hs && hs->ms && a.pingTable().contains(*a.peerId());
    }, 6000);
    CHECK(measured);
    if (measured) {
        std::printf("  ping on loopback: client %.2f ms, host %.2f ms, quality %s\n", *a.pingStats().ms,
            *host.pingStats(*a.peerId())->ms, toString(qualityFor(a.pingStats())));
        CHECK(*a.pingStats().ms < 50.f);  // frame time must not inflate it (no 16 ms steps here)
    }
    host.stop();
}

} // namespace

void runPingTests() {
    testTracker();
    testSessionPing();
}

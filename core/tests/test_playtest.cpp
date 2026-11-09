// Playtest spectating: icons stream to the others, each test leaves its own trail.
#include "harness.hpp"

#include <nexlevel/core/session/client.hpp>
#include <nexlevel/core/session/host.hpp>
#include <nexlevel/core/session/playtest.hpp>

#include <chrono>
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

void testTrailRecording() {
    std::map<sync::PeerId, PlayTrail> trails;
    PlayerState s;
    s.peer = 2;
    s.session = 1;
    s.flags = PlayerState::Playing;
    for (int i = 0; i < 100; ++i) {
        s.x1 = static_cast<float>(i) * 0.5f;  // half a unit per step: thinned to one point per 2 units
        recordPlayerState(trails, s);
    }
    CHECK(trails[2].points1.size() > 20 && trails[2].points1.size() < 30);
    s.flags = 0;  // test stopped: the trail stays
    recordPlayerState(trails, s);
    CHECK(!trails[2].last.playing() && !trails[2].points1.empty());
    s.session = 2;  // a new test: a new trail
    s.flags = PlayerState::Playing;
    s.x1 = 1000.f;
    recordPlayerState(trails, s);
    CHECK(trails[2].points1.size() == 1);
    PlayerState bad = s;
    bad.x1 = 1e9f;
    CHECK(!validPlayerState(bad));
}

void testSpectating() {
    SessionHost host(GameTarget::GD2208, "Host");
    auto port = host.start(0, true);
    CHECK(port.has_value());
    if (!port) return;
    SessionClient a(GameTarget::GD2208, "Tester"), b(GameTarget::GD2208, "Watcher");
    a.connect("127.0.0.1", *port, host.roomKey());
    b.connect("127.0.0.1", *port, host.roomKey());
    auto pumpAll = [&] { host.pump(); a.pump(); b.pump(); };
    CHECK(waitFor(pumpAll, [&] { return a.state() == ClientState::Joined && b.state() == ClientState::Joined && b.peers().size() == 3; }, 3000));
    const auto tester = *a.peerId();

    // A plays for a while: the host and B see the icon move and the trail grow.
    PlayerState s;
    s.session = 7;
    s.flags = PlayerState::Playing;
    s.mode1 = 1;  // ship
    for (int i = 0; i < 40; ++i) {
        s.x1 = 100.f + static_cast<float>(i) * 10.f;
        s.y1 = 105.f + static_cast<float>(i % 5) * 3.f;
        a.setLocalPlayerState(s);
        for (int k = 0; k < 20; ++k) { pumpAll(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    }
    s.flags = 0;  // stop: always delivered even inside the rate limit
    a.setLocalPlayerState(s);
    bool seen = waitFor(pumpAll, [&] {
        auto it = b.playTrails().find(tester);
        return it != b.playTrails().end() && !it->second.last.playing() && it->second.points1.size() >= 20;
    }, 3000);
    CHECK(seen);
    auto hostIt = host.playTrails().find(tester);
    CHECK(hostIt != host.playTrails().end() && hostIt->second.points1.size() >= 20 && hostIt->second.last.mode1 == 1);
    CHECK(a.playTrails().find(tester) == a.playTrails().end());  // own state is not echoed

    // The host playtests too: both players see it.
    PlayerState h;
    h.session = 1;
    h.flags = PlayerState::Playing;
    h.x1 = 500.f;
    host.setLocalPlayerState(h);
    CHECK(waitFor(pumpAll, [&] {
        return a.playTrails().contains(SessionHost::kHostPeer) && b.playTrails().contains(SessionHost::kHostPeer);
    }, 3000));

    // Leaving removes the trail for everyone.
    a.disconnect();
    CHECK(waitFor([&] { host.pump(); b.pump(); }, [&] { return !b.playTrails().contains(tester) && !host.playTrails().contains(tester); }, 3000));
    host.stop();
}

} // namespace

void runPlaytestTests() {
    testTrailRecording();
    testSpectating();
}

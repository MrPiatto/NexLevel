// Editor presence: where each player is looking, shared live but never part of the level.
#include "harness.hpp"

#include <nexlevel/core/session/client.hpp>
#include <nexlevel/core/session/host.hpp>

#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
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

const Presence* find(const std::vector<Presence>& v, sync::PeerId id, std::vector<Presence>& storage) {
    storage = v;
    for (auto& p : storage) if (p.peer == id) return &p;
    return nullptr;
}

void testPresence() {
    SessionHost host(GameTarget::GD2208, "Host");
    auto port = host.start(0, true);
    CHECK(port.has_value());
    if (!port) return;
    SessionClient a(GameTarget::GD2208, "A");
    a.connect("127.0.0.1", *port, host.roomKey());
    auto pumpAll = [&] { host.pump(); a.pump(); };
    CHECK(waitFor(pumpAll, [&] { return a.state() == ClientState::Joined; }, 3000));

    std::vector<Presence> tmp;
    host.setLocalPresence(100.f, 200.f, 1.5f);
    a.setLocalPresence(-30.f, 45.f, 0.5f);
    CHECK(waitFor(pumpAll, [&] {
        return find(a.presences(), SessionHost::kHostPeer, tmp) && find(host.presences(), *a.peerId(), tmp);
    }, 3000));
    auto hp = find(a.presences(), SessionHost::kHostPeer, tmp);
    CHECK(hp && hp->x == 100.f && hp->y == 200.f && hp->zoom == 1.5f);
    std::vector<Presence> tmp2;
    auto ap = find(host.presences(), *a.peerId(), tmp2);
    CHECK(ap && ap->x == -30.f && ap->zoom == 0.5f);
    CHECK(find(a.presences(), *a.peerId(), tmp) == nullptr);  // own position is not echoed

    // A late joiner immediately sees where everyone already is.
    SessionClient b(GameTarget::GD2208, "B");
    b.connect("127.0.0.1", *port, host.roomKey());
    auto pumpAll3 = [&] { host.pump(); a.pump(); b.pump(); };
    CHECK(waitFor(pumpAll3, [&] {
        return b.state() == ClientState::Joined && find(b.presences(), SessionHost::kHostPeer, tmp) && find(b.presences(), *a.peerId(), tmp);
    }, 3000));

    // Moves arrive (latest wins); invalid values are never sent.
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    a.setLocalPresence(500.f, 600.f, 2.f);
    a.setLocalPresence(std::numeric_limits<float>::quiet_NaN(), 0.f, 1.f);
    CHECK(waitFor(pumpAll3, [&] {
        auto p = find(b.presences(), *a.peerId(), tmp);
        return p && p->x == 500.f;
    }, 3000));
    CHECK(a.state() == ClientState::Joined);

    // Leaving removes the marker for everyone.
    const auto aId = *a.peerId();
    a.disconnect();
    CHECK(waitFor([&] { host.pump(); b.pump(); }, [&] {
        return find(b.presences(), aId, tmp) == nullptr && find(host.presences(), aId, tmp) == nullptr;
    }, 3000));
    host.stop();
}

} // namespace

void runPresenceTests() { testPresence(); }

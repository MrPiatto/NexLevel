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

void testChatAndViewOnly() {
    SessionHost host(GameTarget::GD2208, "Host");
    auto port = host.start(0, true);
    CHECK(port.has_value());
    if (!port) return;
    SessionClient a(GameTarget::GD2208, "A");
    SessionClient b(GameTarget::GD2208, "B");
    a.connect("127.0.0.1", *port, host.roomKey());
    b.connect("127.0.0.1", *port, host.roomKey());
    std::vector<ChatMessage> hostLines, aLines, bLines;
    auto pumpAll = [&] {
        host.pump(); a.pump(); b.pump();
        for (auto& l : host.takeChat()) hostLines.push_back(l);
        for (auto& l : a.takeChat()) aLines.push_back(l);
        for (auto& l : b.takeChat()) bLines.push_back(l);
    };
    CHECK(waitFor(pumpAll, [&] { return a.state() == ClientState::Joined && b.state() == ClientState::Joined; }, 3000));

    // Chat: everyone gets the line once, with the sender's real id; control characters are stripped.
    a.sendChat("  hi\x01 all ");
    host.sendChat("welcome");
    CHECK(waitFor(pumpAll, [&] { return hostLines.size() == 2 && aLines.size() == 2 && bLines.size() == 2; }, 3000));
    bool fromA = false;
    for (auto& l : bLines) fromA = fromA || (l.peer == *a.peerId() && l.text == "hi all");
    CHECK(fromA);
    a.sendChat("   ");  // empty lines are never sent
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    pumpAll();
    CHECK(bLines.size() == 2);

    // View only: the player's edits are refused and undone; the roster says so.
    host.setViewOnly(*a.peerId(), true);
    CHECK(waitFor(pumpAll, [&] {
        for (auto& p : a.peers()) if (p.id == *a.peerId()) return p.viewOnly;
        return false;
    }, 3000));
    auto id = a.newId();
    CHECK(id.has_value());
    if (id) a.submitLocal(sync::Op::create(*id, sync::PropMap{{1, "1"}}));
    CHECK(waitFor(pumpAll, [&] { return a.replica() && a.replica()->view().find(*id) == nullptr && a.replica()->pending().empty(); }, 3000));
    CHECK(host.state().find(*id) == nullptr);
    host.setViewOnly(*a.peerId(), false);
    auto id2 = a.newId();
    if (id2) a.submitLocal(sync::Op::create(*id2, sync::PropMap{{1, "1"}}));
    CHECK(waitFor(pumpAll, [&] { return host.state().find(*id2) != nullptr; }, 3000));

    // Ban: B is removed and cannot come back; max players refuses extra players.
    const auto bId = *b.peerId();
    CHECK(host.ban(bId));
    CHECK(waitFor(pumpAll, [&] { return b.state() == ClientState::Kicked; }, 3000));
    SessionClient b2(GameTarget::GD2208, "B");
    b2.connect("127.0.0.1", host.port(), host.roomKey());
    CHECK(waitFor([&] { host.pump(); b2.pump(); }, [&] { return b2.state() == ClientState::Refused; }, 3000));
    CHECK(b2.refuseReason() == RefuseReason::Banned);
    host.setMaxPlayers(1);  // A is already in
    SessionClient c(GameTarget::GD2208, "C");
    c.connect("127.0.0.1", host.port(), host.roomKey());
    CHECK(waitFor([&] { host.pump(); c.pump(); }, [&] { return c.state() == ClientState::Refused; }, 3000));
    CHECK(c.refuseReason() == RefuseReason::SessionFull);
    host.stop();
}

} // namespace

void runPresenceTests() {
    testPresence();
    testChatAndViewOnly();
}

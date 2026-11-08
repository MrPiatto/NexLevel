#pragma once
// Embedded session server, run by the hosting player inside the game process.
// Threading: networking runs on an internal thread; ALL session/sync logic runs on the caller's thread
// inside pump() (call it once per frame from the game's main thread). Nothing here blocks.
#include <nexlevel/core/session/messages.hpp>
#include <nexlevel/core/sync/host_sequencer.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nexlevel::session {

struct PeerInfo {
    sync::PeerId id = 0;
    std::string name;
};

class SessionHost {
public:
    static constexpr sync::PeerId kHostPeer = 1;
    static constexpr std::size_t kMaxPeers = 16;

    SessionHost(GameTarget target, std::string hostName);
    ~SessionHost();
    SessionHost(const SessionHost&) = delete;
    SessionHost& operator=(const SessionHost&) = delete;

    // Listen on port (0 = any free port). loopbackOnly binds 127.0.0.1 (tests, local play).
    // Returns the bound port, or nullopt with *error set.
    std::optional<std::uint16_t> start(std::uint16_t port, bool loopbackOnly, std::string* error = nullptr);
    void stop();
    bool running() const noexcept;
    std::uint16_t port() const noexcept;

    // Process network events. Returns ids changed by other peers' ops (apply them to the host's editor).
    std::vector<sync::NetId> pump();

    // The host player's own edit (already visible in the host editor): sequenced now and broadcast.
    sync::Op submitLocal(sync::Op op);
    sync::NetId newId();

    const sync::Replica& state() const noexcept;
    std::vector<PeerInfo> peers() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace nexlevel::session

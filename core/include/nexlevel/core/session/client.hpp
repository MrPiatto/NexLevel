#pragma once
// Session client: connects to a host, joins, keeps a ClientReplica in sync.
// Same threading model as SessionHost: call pump() once per frame on the game's main thread.
#include <nexlevel/core/session/messages.hpp>
#include <nexlevel/core/sync/client_replica.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nexlevel::session {

enum class ClientState : std::uint8_t { Idle, Connecting, Joining, Joined, Refused, Kicked, RoomClosed, Disconnected };

class SessionClient {
public:
    SessionClient(GameTarget target, std::string displayName);
    ~SessionClient();
    SessionClient(const SessionClient&) = delete;
    SessionClient& operator=(const SessionClient&) = delete;

    void connect(const std::string& host, std::uint16_t port, std::uint16_t roomKey);
    // Join with a room code. Returns false (and does nothing) if the code is malformed.
    bool connectCode(std::string_view code);
    void disconnect();

    // Process network events. Returns ids whose visible state changed (apply them to the local editor).
    std::vector<sync::NetId> pump();

    // A local editor change. Only while Joined; returns the op as sent.
    std::optional<sync::Op> submitLocal(sync::Op op);
    std::optional<sync::NetId> newId();

    ClientState state() const noexcept;
    const std::string& lastError() const noexcept;
    std::optional<RefuseReason> refuseReason() const noexcept;
    std::optional<GameTarget> hostTarget() const noexcept;  // set when refused for a version mismatch
    const sync::ClientReplica* replica() const noexcept;
    const std::vector<PeerInfo>& peers() const noexcept;  // host first; empty until joined
    std::optional<sync::PeerId> peerId() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace nexlevel::session

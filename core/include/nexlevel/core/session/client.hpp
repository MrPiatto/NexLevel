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

// Reconnecting: the connection dropped after joining; the client retries on its own and keeps
// accepting local edits, which are sent once it is back in the room.
enum class ClientState : std::uint8_t { Idle, Connecting, Joining, Joined, Reconnecting, Refused, Kicked, RoomClosed, Disconnected };

struct ConnectTarget {
    std::string host;
    std::uint16_t port = 0;
    std::uint32_t relayRoom = 0;  // non-zero: host:port is a relay, join this room through it
};

class SessionClient {
public:
    SessionClient(GameTarget target, std::string displayName);
    ~SessionClient();
    SessionClient(const SessionClient&) = delete;
    SessionClient& operator=(const SessionClient&) = delete;

    void connect(const std::string& host, std::uint16_t port, std::uint16_t roomKey);
    // Several addresses for the same room (e.g. public + LAN): tried in parallel, first wins.
    void connectTargets(std::vector<ConnectTarget> targets, std::uint16_t roomKey);
    // Relay used for 10-character (relay) room codes. Must match the host's relay.
    void setRelayServer(const std::string& host, std::uint16_t port);
    // Join with a room code. Returns false (and does nothing) if the code is malformed, or is a
    // relay code and no relay server was set (lastError() says which).
    bool connectCode(std::string_view code);
    void disconnect();

    // Test hook: cut the connection as if the network dropped (triggers automatic reconnection).
    void simulateNetworkDrop();
    // True once after a reconnect that came back with a different identity (host restarted):
    // the editor should rebuild everything from the session.
    bool takeIdentityReset();

    // Process network events. Returns ids whose visible state changed (apply them to the local editor).
    std::vector<sync::NetId> pump();

    // A local editor change. While Joined or Reconnecting (sent after reconnecting); returns the op.
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

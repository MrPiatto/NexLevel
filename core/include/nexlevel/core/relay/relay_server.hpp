#pragma once
// The Forg3 relay server (see relay/protocol.hpp). Runs its own network thread.
// Used by the nexlevel_relay program and by the tests.
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace nexlevel::relay {

struct RelayStats {
    std::size_t rooms = 0;
    std::size_t pipes = 0;           // players currently connected through the relay
    std::uint64_t bytesRelayed = 0;  // total, both directions
    std::uint64_t joinsRefused = 0;
};

class RelayServer {
public:
    struct Options {
        std::size_t maxRooms = 5000;
        std::size_t maxStreamsPerRoom = 32;        // players per room (session allows 16)
        std::size_t maxConnectionsPerIp = 64;
        std::size_t maxFailedJoinsPerIpPerMinute = 30;  // slows down room-id guessing
        std::chrono::seconds handshakeTimeout{10};  // to send the first message
        std::chrono::seconds pairTimeout{8};        // host must open the data connection
        std::chrono::seconds controlIdleTimeout{60};  // hosts ping every 15 s
        std::chrono::seconds reclaimWindow{90};     // host may reclaim its room id after a drop
    };

    RelayServer();
    explicit RelayServer(Options opts);
    ~RelayServer();
    RelayServer(const RelayServer&) = delete;
    RelayServer& operator=(const RelayServer&) = delete;

    // Listens on port (0 = any free port). Returns the port, or nullopt with *error set.
    std::optional<std::uint16_t> start(std::uint16_t port, bool loopbackOnly, std::string* error = nullptr);
    void stop();
    RelayStats stats() const;

private:
    struct Impl;
    std::shared_ptr<Impl> m;
};

} // namespace nexlevel::relay

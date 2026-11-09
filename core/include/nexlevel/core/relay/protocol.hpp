#pragma once
// Forg3 relay ("ponte") protocol: lets a host behind CGNAT / mobile data / strict NAT run a room.
//
// The host keeps one outgoing CONTROL connection to the relay and gets a room id. When a player
// joins (Join{roomId}), the relay tells the host (Incoming{stream}); the host opens a new outgoing
// DATA connection (Accept{roomId, secret, stream}) and from then on the relay just copies bytes
// between the player's socket and that data socket. The session protocol runs end to end over the
// pipe, encrypted (crypto/secure_channel.hpp): the relay never sees level data in the clear.
//
// Deliberately independent of the session protocol version, so one relay serves every mod release.
// Wire: 6-byte header (u32 magic "NXRL", u8 version, u8 kind) then a fixed-size body per kind,
// all little-endian.
#include <array>
#include <chrono>
#include <string>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include <cstddef>

namespace nexlevel::relay {

inline constexpr std::uint32_t kMagic = 0x4C52584E;  // "NXRL"
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::size_t kHeaderSize = 6;
inline constexpr std::uint16_t kDefaultPort = 47800;

enum class Kind : std::uint8_t {
    // to the relay
    OpenRoom = 1,     // host control: u32 reclaimRoomId (0 = new), u64 reclaimSecret
    Accept = 2,       // host data:    u32 roomId, u64 secret, u32 stream
    Join = 3,         // player:       u32 roomId
    Ping = 4,         // host control keepalive, no body
    Probe = 5,        // latency check, no body: the relay answers Pong and closes
    // from the relay
    RoomOpened = 0x81,  // u32 roomId, u64 secret
    Incoming = 0x82,    // u32 stream: open a data connection for it
    Pong = 0x83,        // no body
    Paired = 0x84,      // to the player: the pipe to the host is open, start the session
    Error = 0x85,       // u8 ErrorCode, then the relay closes
};

enum class ErrorCode : std::uint8_t { NoRoom = 1, RoomFull = 2, RelayBusy = 3, BadRequest = 4, Timeout = 5 };

const char* toString(ErrorCode e) noexcept;

// Body size for a kind; nullopt for unknown kinds (close the connection).
std::optional<std::size_t> bodySize(Kind k) noexcept;

struct Message {
    Kind kind{};
    std::uint32_t roomId = 0;
    std::uint64_t secret = 0;
    std::uint32_t stream = 0;
    ErrorCode error = ErrorCode::BadRequest;
};

std::vector<std::uint8_t> encode(const Message& m);

struct RelayEndpoint {
    std::uint8_t id = 0;  // as in the room code (0..15)
    std::string name;     // e.g. "EU-1"
    std::string host;
    std::uint16_t port = kDefaultPort;
};
// Round trip to each relay (Probe -> Pong), nullopt if unreachable within the timeout.
// Blocking: call it from a worker thread. All relays are probed in parallel.
std::vector<std::optional<float>> probeRelays(const std::vector<RelayEndpoint>& relays, std::chrono::milliseconds timeout);
// Header check: nullopt if magic/version/kind is wrong.
std::optional<Kind> decodeHeader(std::span<const std::uint8_t> header);
std::optional<Message> decodeBody(Kind k, std::span<const std::uint8_t> body);

} // namespace nexlevel::relay

#pragma once
// Room codes: share a room without typing an IP and without any server.
// A code packs the host's IPv4 address, port and an 11-bit room key, plus a 5-bit checksum,
// into 64 bits written as 13 Crockford base32 characters: "K7Q2-M9XA-3PF1Q".
// Decoding ignores case, dashes and spaces, and accepts O->0, I/L->1 (Crockford aliases).
// The room key is checked by the host at join, so knowing the address alone is not enough.
//
// Relay rooms use a 10-character code ("K7Q2M-9XA3P"): the room id on the relay plus the key.
//
// Online rooms (router port opened automatically) use a 15-character code that also carries the
// last byte of the host's LAN address ("lanOctet"): players on the host's own network then connect
// over the LAN, which works even when the router cannot loop its public address back inside.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nexlevel::session {

inline constexpr std::uint16_t kRoomKeyMask = 0x7FF;  // 11 bits

struct RoomAddress {
    std::array<std::uint8_t, 4> ipv4{};
    std::uint16_t port = 0;
    std::uint16_t key = 0;  // 0..2047
    std::uint8_t lanOctet = 0;  // 0 = none (13-char code); else 15-char code
    // Non-zero: room on the Forg3 relay (10-char code: relay room id + key; ipv4/port unused).
    // Works from any network, including mobile data / CGNAT.
    std::uint32_t relayRoom = 0;

    std::string ipString() const;
    bool operator==(const RoomAddress&) const = default;
};

std::string encodeRoomCode(const RoomAddress& addr);
std::optional<RoomAddress> decodeRoomCode(std::string_view code);  // nullopt: malformed or typo
std::optional<std::array<std::uint8_t, 4>> parseIPv4(std::string_view s);

} // namespace nexlevel::session

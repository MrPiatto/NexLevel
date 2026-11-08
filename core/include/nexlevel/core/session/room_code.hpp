#pragma once
// Room codes: share a room without typing an IP and without any server.
// A code packs the host's IPv4 address, port and an 11-bit room key, plus a 5-bit checksum,
// into 64 bits written as 13 Crockford base32 characters: "K7Q2-M9XA-3PF1Q".
// Decoding ignores case, dashes and spaces, and accepts O->0, I/L->1 (Crockford aliases).
// The room key is checked by the host at join, so knowing the address alone is not enough.
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

    std::string ipString() const;
    bool operator==(const RoomAddress&) const = default;
};

std::string encodeRoomCode(const RoomAddress& addr);
std::optional<RoomAddress> decodeRoomCode(std::string_view code);  // nullopt: malformed or typo
std::optional<std::array<std::uint8_t, 4>> parseIPv4(std::string_view s);

} // namespace nexlevel::session

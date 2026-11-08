#include <nexlevel/core/session/room_code.hpp>

namespace nexlevel::session {

namespace {
constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";  // Crockford base32

int symbolValue(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c == 'O') c = '0';
    if (c == 'I' || c == 'L') c = '1';
    for (int i = 0; i < 32; ++i) {
        if (kAlphabet[i] == c) return i;
    }
    return -1;
}

// 5-bit checksum of the 59 data bits (FNV-1a folded), so most typos are caught before connecting.
std::uint64_t checksum5(std::uint64_t data) {
    std::uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 8; ++i) {
        h ^= (data >> (i * 8)) & 0xFF;
        h *= 1099511628211ull;
    }
    return (h ^ (h >> 5) ^ (h >> 10) ^ (h >> 20) ^ (h >> 40)) & 0x1F;
}
} // namespace

std::string RoomAddress::ipString() const {
    return std::to_string(ipv4[0]) + "." + std::to_string(ipv4[1]) + "." + std::to_string(ipv4[2]) + "." + std::to_string(ipv4[3]);
}

std::string encodeRoomCode(const RoomAddress& a) {
    std::uint64_t data = (static_cast<std::uint64_t>(a.ipv4[0]) << 51) | (static_cast<std::uint64_t>(a.ipv4[1]) << 43) |
                         (static_cast<std::uint64_t>(a.ipv4[2]) << 35) | (static_cast<std::uint64_t>(a.ipv4[3]) << 27) |
                         (static_cast<std::uint64_t>(a.port) << 11) | (a.key & kRoomKeyMask);
    std::uint64_t v = (data << 5) | checksum5(data);
    std::string sym(13, '0');
    for (int i = 12; i >= 0; --i) {  // 13 symbols = 65 bits; the top bit is always 0
        sym[static_cast<std::size_t>(i)] = kAlphabet[v & 0x1F];
        v >>= 5;
    }
    return sym.substr(0, 4) + "-" + sym.substr(4, 4) + "-" + sym.substr(8, 5);
}

std::optional<RoomAddress> decodeRoomCode(std::string_view code) {
    std::uint64_t v = 0;
    int count = 0;
    for (char c : code) {
        if (c == '-' || c == ' ') continue;
        int s = symbolValue(c);
        if (s < 0) return std::nullopt;
        if (count == 0 && s > 15) return std::nullopt;  // first symbol carries only 4 of 64 bits
        v = (v << 5) | static_cast<std::uint64_t>(s);
        if (++count > 13) return std::nullopt;
    }
    if (count != 13) return std::nullopt;
    std::uint64_t data = v >> 5;
    if ((v & 0x1F) != checksum5(data)) return std::nullopt;
    RoomAddress a;
    a.ipv4 = {static_cast<std::uint8_t>(data >> 51), static_cast<std::uint8_t>(data >> 43),
              static_cast<std::uint8_t>(data >> 35), static_cast<std::uint8_t>(data >> 27)};
    a.port = static_cast<std::uint16_t>(data >> 11);
    a.key = static_cast<std::uint16_t>(data & kRoomKeyMask);
    if (a.port == 0) return std::nullopt;
    return a;
}

std::optional<std::array<std::uint8_t, 4>> parseIPv4(std::string_view s) {
    std::array<std::uint8_t, 4> out{};
    int part = 0, value = -1;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '.') {
            if (value < 0 || value > 255 || part > 3) return std::nullopt;
            out[static_cast<std::size_t>(part++)] = static_cast<std::uint8_t>(value);
            value = -1;
        } else if (s[i] >= '0' && s[i] <= '9') {
            value = (value < 0 ? 0 : value * 10) + (s[i] - '0');
            if (value > 255) return std::nullopt;
        } else {
            return std::nullopt;
        }
    }
    if (part != 4) return std::nullopt;
    return out;
}

} // namespace nexlevel::session

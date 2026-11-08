#include <nexlevel/core/session/room_code.hpp>

#include <vector>

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
// Bit strings for the 15-character format (72 bits do not fit a uint64).
struct Bits {
    std::vector<bool> v;
    void put(std::uint64_t value, int n) { for (int i = n - 1; i >= 0; --i) v.push_back((value >> i) & 1); }
    std::uint64_t get(std::size_t& pos, int n) const {
        std::uint64_t r = 0;
        for (int i = 0; i < n; ++i) r = (r << 1) | (v[pos++] ? 1u : 0u);
        return r;
    }
};

std::uint64_t checksum5Bits(const std::vector<bool>& bits, std::uint64_t domain = 0x15) {
    std::uint64_t h = 1469598103934665603ull ^ domain;  // each format has its own domain
    std::uint8_t byte = 0;
    int n = 0;
    for (bool b : bits) {
        byte = static_cast<std::uint8_t>((byte << 1) | (b ? 1 : 0));
        if (++n == 8) { h ^= byte; h *= 1099511628211ull; byte = 0; n = 0; }
    }
    if (n) { h ^= byte; h *= 1099511628211ull; }
    return (h ^ (h >> 5) ^ (h >> 10) ^ (h >> 20) ^ (h >> 40)) & 0x1F;
}

constexpr int kLongDataBits = 32 + 16 + 11 + 8;   // 67
constexpr int kLongSymbols = 15;                  // 75 bits; top 3 always 0

std::string encodeLong(const RoomAddress& a) {
    Bits b;
    for (auto o : a.ipv4) b.put(o, 8);
    b.put(a.port, 16);
    b.put(a.key & kRoomKeyMask, 11);
    b.put(a.lanOctet, 8);
    const auto sum = checksum5Bits(b.v);
    Bits all;
    all.put(0, kLongSymbols * 5 - kLongDataBits - 5);
    all.v.insert(all.v.end(), b.v.begin(), b.v.end());
    all.put(sum, 5);
    std::string sym;
    std::size_t pos = 0;
    for (int i = 0; i < kLongSymbols; ++i) sym += kAlphabet[all.get(pos, 5)];
    return sym.substr(0, 5) + "-" + sym.substr(5, 5) + "-" + sym.substr(10, 5);
}

std::optional<RoomAddress> decodeLong(const std::vector<int>& symbols) {
    Bits all;
    for (int s : symbols) all.put(static_cast<std::uint64_t>(s), 5);
    std::size_t pos = 0;
    if (all.get(pos, kLongSymbols * 5 - kLongDataBits - 5) != 0) return std::nullopt;
    std::vector<bool> data(all.v.begin() + static_cast<std::ptrdiff_t>(pos), all.v.begin() + static_cast<std::ptrdiff_t>(pos + kLongDataBits));
    RoomAddress a;
    for (auto& o : a.ipv4) o = static_cast<std::uint8_t>(all.get(pos, 8));
    a.port = static_cast<std::uint16_t>(all.get(pos, 16));
    a.key = static_cast<std::uint16_t>(all.get(pos, 11));
    a.lanOctet = static_cast<std::uint8_t>(all.get(pos, 8));
    if (all.get(pos, 5) != checksum5Bits(data)) return std::nullopt;
    if (a.port == 0 || a.lanOctet == 0) return std::nullopt;
    return a;
}
constexpr int kRelayDataBits = 4 + 30 + 11;  // relay id, room id, key = 45
constexpr int kRelaySymbols = 10;            // 50 bits = 45 data + 5 checksum
constexpr std::uint64_t kRelayDomain = 0x2A;

std::string encodeRelay(const RoomAddress& a) {
    Bits b;
    b.put(a.relayId & 0xF, 4);
    b.put(a.relayRoom & kRelayRoomMask, 30);
    b.put(a.key & kRoomKeyMask, 11);
    const auto sum = checksum5Bits(b.v, kRelayDomain);
    Bits all;
    all.put(0, kRelaySymbols * 5 - kRelayDataBits - 5);
    all.v.insert(all.v.end(), b.v.begin(), b.v.end());
    all.put(sum, 5);
    std::string sym;
    std::size_t pos = 0;
    for (int i = 0; i < kRelaySymbols; ++i) sym += kAlphabet[all.get(pos, 5)];
    return sym.substr(0, 5) + "-" + sym.substr(5, 5);
}

std::optional<RoomAddress> decodeRelay(const std::vector<int>& symbols) {
    Bits all;
    for (int s : symbols) all.put(static_cast<std::uint64_t>(s), 5);
    std::size_t pos = 0;
    if (all.get(pos, kRelaySymbols * 5 - kRelayDataBits - 5) != 0) return std::nullopt;
    std::vector<bool> data(all.v.begin() + static_cast<std::ptrdiff_t>(pos), all.v.begin() + static_cast<std::ptrdiff_t>(pos + kRelayDataBits));
    RoomAddress a;
    a.relayId = static_cast<std::uint8_t>(all.get(pos, 4));
    a.relayRoom = static_cast<std::uint32_t>(all.get(pos, 30));
    a.key = static_cast<std::uint16_t>(all.get(pos, 11));
    if (all.get(pos, 5) != checksum5Bits(data, kRelayDomain)) return std::nullopt;
    if (a.relayRoom == 0) return std::nullopt;
    return a;
}
} // namespace

std::string RoomAddress::ipString() const {
    return std::to_string(ipv4[0]) + "." + std::to_string(ipv4[1]) + "." + std::to_string(ipv4[2]) + "." + std::to_string(ipv4[3]);
}

std::string encodeRoomCode(const RoomAddress& a) {
    if (a.relayRoom != 0) return encodeRelay(a);
    if (a.lanOctet != 0) return encodeLong(a);
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
    std::vector<int> symbols;
    for (char c : code) {
        if (c == '-' || c == ' ') continue;
        int s = symbolValue(c);
        if (s < 0) return std::nullopt;
        symbols.push_back(s);
        if (symbols.size() > static_cast<std::size_t>(kLongSymbols)) return std::nullopt;
    }
    if (symbols.size() == static_cast<std::size_t>(kLongSymbols)) return decodeLong(symbols);
    if (symbols.size() == static_cast<std::size_t>(kRelaySymbols)) return decodeRelay(symbols);
    if (symbols.size() != 13) return std::nullopt;
    if (symbols[0] > 15) return std::nullopt;  // first symbol carries only 4 of 64 bits
    std::uint64_t v = 0;
    for (int s : symbols) v = (v << 5) | static_cast<std::uint64_t>(s);
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

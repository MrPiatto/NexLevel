#include <nexlevel/core/compress.hpp>

#include <array>
#include <cstring>

namespace nexlevel {

namespace {

constexpr std::size_t kMinMatch = 4;
constexpr std::size_t kHashBits = 14;
constexpr std::size_t kMaxOffset = 65535;
constexpr std::size_t kLastLiterals = 5;   // the block always ends with literals (LZ4 rule)
constexpr std::size_t kMatchSafety = 12;   // no match may start this close to the end

inline std::uint32_t read32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
inline std::uint32_t hash4(std::uint32_t v) { return (v * 2654435761u) >> (32 - kHashBits); }

void writeLength(std::vector<std::uint8_t>& out, std::size_t len) {
    while (len >= 255) {
        out.push_back(255);
        len -= 255;
    }
    out.push_back(static_cast<std::uint8_t>(len));
}

void emitSequence(std::vector<std::uint8_t>& out, const std::uint8_t* lit, std::size_t litLen, std::size_t offset,
                  std::size_t matchLen /* 0 = last literals only */) {
    const std::size_t ml = matchLen ? matchLen - kMinMatch : 0;
    std::uint8_t token = static_cast<std::uint8_t>((litLen >= 15 ? 15 : litLen) << 4);
    if (matchLen) token |= static_cast<std::uint8_t>(ml >= 15 ? 15 : ml);
    out.push_back(token);
    if (litLen >= 15) writeLength(out, litLen - 15);
    out.insert(out.end(), lit, lit + litLen);
    if (!matchLen) return;
    out.push_back(static_cast<std::uint8_t>(offset & 0xFF));
    out.push_back(static_cast<std::uint8_t>(offset >> 8));
    if (ml >= 15) writeLength(out, ml - 15);
}

} // namespace

std::vector<std::uint8_t> lzCompress(std::span<const std::uint8_t> in) {
    std::vector<std::uint8_t> out;
    out.reserve(in.size() / 2 + 16);
    const std::uint8_t* base = in.data();
    const std::size_t n = in.size();
    if (n < kMatchSafety + 1) {
        emitSequence(out, base, n, 0, 0);
        return out;
    }
    std::vector<std::uint32_t> table(std::size_t{1} << kHashBits, 0);  // position + 1 (0 = empty)
    std::size_t anchor = 0;
    std::size_t i = 0;
    const std::size_t matchLimit = n - kMatchSafety;
    while (i < matchLimit) {
        const std::uint32_t seq = read32(base + i);
        const std::uint32_t h = hash4(seq);
        const std::size_t cand = table[h];
        table[h] = static_cast<std::uint32_t>(i + 1);
        if (cand == 0 || i - (cand - 1) > kMaxOffset || read32(base + cand - 1) != seq) {
            ++i;
            continue;
        }
        std::size_t ref = cand - 1;
        // Extend backwards over pending literals.
        while (i > anchor && ref > 0 && base[i - 1] == base[ref - 1]) {
            --i;
            --ref;
        }
        // Extend forwards (the last kLastLiterals bytes stay literals).
        std::size_t len = kMinMatch;
        const std::size_t maxLen = n - kLastLiterals - i;
        while (len < maxLen && base[i + len] == base[ref + len]) ++len;
        emitSequence(out, base + anchor, i - anchor, i - ref, len);
        i += len;
        anchor = i;
        if (i >= 2 && i - 2 < matchLimit) table[hash4(read32(base + i - 2))] = static_cast<std::uint32_t>(i - 2 + 1);
    }
    emitSequence(out, base + anchor, n - anchor, 0, 0);
    return out;
}

std::optional<std::vector<std::uint8_t>> lzDecompress(std::span<const std::uint8_t> in, std::size_t rawSize) {
    std::vector<std::uint8_t> out;
    out.reserve(rawSize);
    std::size_t ip = 0;
    const std::size_t n = in.size();
    auto readLen = [&](std::size_t start, std::size_t& len) -> bool {
        len = start;
        if (start != 15) return true;
        while (true) {
            if (ip >= n) return false;
            const std::uint8_t b = in[ip++];
            len += b;
            if (len > rawSize) return false;
            if (b != 255) return true;
        }
    };
    while (ip < n) {
        const std::uint8_t token = in[ip++];
        std::size_t litLen = 0;
        if (!readLen(token >> 4, litLen)) return std::nullopt;
        if (litLen > n - ip || out.size() + litLen > rawSize) return std::nullopt;
        out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(ip), in.begin() + static_cast<std::ptrdiff_t>(ip + litLen));
        ip += litLen;
        if (ip == n) break;  // last sequence: literals only
        if (n - ip < 2) return std::nullopt;
        const std::size_t offset = static_cast<std::size_t>(in[ip]) | (static_cast<std::size_t>(in[ip + 1]) << 8);
        ip += 2;
        if (offset == 0 || offset > out.size()) return std::nullopt;
        std::size_t ml = 0;
        if (!readLen(token & 0x0F, ml)) return std::nullopt;
        ml += kMinMatch;
        if (out.size() + ml > rawSize) return std::nullopt;
        const std::size_t from = out.size() - offset;
        for (std::size_t k = 0; k < ml; ++k) out.push_back(out[from + k]);  // may overlap: byte by byte
    }
    if (out.size() != rawSize) return std::nullopt;
    return out;
}

} // namespace nexlevel

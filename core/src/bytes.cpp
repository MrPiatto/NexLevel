#include <nexlevel/core/bytes.hpp>

#include <bit>
#include <cstring>

namespace nexlevel {

namespace {
template <class T>
void putLE(std::vector<std::uint8_t>& buf, T v) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        buf.push_back(static_cast<std::uint8_t>((static_cast<std::uint64_t>(v) >> (8 * i)) & 0xFF));
    }
}
template <class T>
T getLE(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    return static_cast<T>(v);
}
} // namespace

void ByteWriter::u8(std::uint8_t v) { m_buf.push_back(v); }
void ByteWriter::u16(std::uint16_t v) { putLE(m_buf, v); }
void ByteWriter::u32(std::uint32_t v) { putLE(m_buf, v); }
void ByteWriter::u64(std::uint64_t v) { putLE(m_buf, v); }
void ByteWriter::f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
void ByteWriter::bytes(std::span<const std::uint8_t> data) { m_buf.insert(m_buf.end(), data.begin(), data.end()); }
void ByteWriter::str(std::string_view s) {
    u32(static_cast<std::uint32_t>(s.size()));
    m_buf.insert(m_buf.end(), s.begin(), s.end());
}

bool ByteReader::need(std::size_t n) noexcept {
    if (m_failed || m_data.size() - m_pos < n) {
        m_failed = true;
        return false;
    }
    return true;
}

std::uint8_t ByteReader::u8() { if (!need(1)) return 0; return m_data[m_pos++]; }
std::uint16_t ByteReader::u16() { if (!need(2)) return 0; auto v = getLE<std::uint16_t>(&m_data[m_pos]); m_pos += 2; return v; }
std::uint32_t ByteReader::u32() { if (!need(4)) return 0; auto v = getLE<std::uint32_t>(&m_data[m_pos]); m_pos += 4; return v; }
std::uint64_t ByteReader::u64() { if (!need(8)) return 0; auto v = getLE<std::uint64_t>(&m_data[m_pos]); m_pos += 8; return v; }
float ByteReader::f32() { return std::bit_cast<float>(u32()); }

std::string ByteReader::str(std::size_t maxLen) {
    auto len = u32();
    if (!ok()) return {};
    if (len > maxLen) { m_failed = true; return {}; }
    auto b = bytes(len);
    if (!ok()) return {};
    return std::string(reinterpret_cast<const char*>(b.data()), b.size());
}

std::span<const std::uint8_t> ByteReader::bytes(std::size_t n) {
    if (!need(n)) return {};
    auto s = m_data.subspan(m_pos, n);
    m_pos += n;
    return s;
}

} // namespace nexlevel

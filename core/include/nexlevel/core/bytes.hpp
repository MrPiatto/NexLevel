#pragma once
// Bounds-checked little-endian binary reader/writer for the wire format.
// The reader uses a sticky failure flag: after any out-of-bounds or over-limit read,
// every later read returns a zero value and ok() stays false. Check ok() once at the end.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nexlevel {

class ByteWriter {
public:
    void u8(std::uint8_t v);
    void u16(std::uint16_t v);
    void u32(std::uint32_t v);
    void u64(std::uint64_t v);
    void f32(float v);
    void bytes(std::span<const std::uint8_t> data);
    // Length-prefixed (u32) UTF-8 string.
    void str(std::string_view s);

    const std::vector<std::uint8_t>& data() const noexcept { return m_buf; }
    std::vector<std::uint8_t> take() noexcept { return std::move(m_buf); }
    std::size_t size() const noexcept { return m_buf.size(); }

private:
    std::vector<std::uint8_t> m_buf;
};

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> data) noexcept : m_data(data) {}

    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();
    std::uint64_t u64();
    float f32();
    // Reads a u32 length then that many bytes; fails if length > maxLen.
    std::string str(std::size_t maxLen);
    std::span<const std::uint8_t> bytes(std::size_t n);

    bool ok() const noexcept { return !m_failed; }
    std::size_t remaining() const noexcept { return m_failed ? 0 : m_data.size() - m_pos; }
    bool atEnd() const noexcept { return ok() && m_pos == m_data.size(); }

private:
    bool need(std::size_t n) noexcept;
    std::span<const std::uint8_t> m_data;
    std::size_t m_pos = 0;
    bool m_failed = false;
};

} // namespace nexlevel

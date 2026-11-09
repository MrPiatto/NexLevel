#pragma once
// Stream framing for reliable byte streams (TCP, KCP).
// Header (10 bytes, little-endian): magic "NX" (u16 0x584E), protocol (u16), type (u16), payload length (u32).
// A malformed header puts the decoder into a permanent error state: the caller must drop the connection.
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include <cstddef>

namespace nexlevel {

inline constexpr std::uint16_t kFrameMagic = 0x584E;   // 'N','X' little-endian
inline constexpr std::size_t kFrameHeaderSize = 10;
inline constexpr std::uint32_t kDefaultMaxPayload = 64 * 1024;

struct Frame {
    std::uint16_t type = 0;
    std::vector<std::uint8_t> payload;
};

std::vector<std::uint8_t> encodeFrame(std::uint16_t type, std::span<const std::uint8_t> payload);

enum class FrameError : std::uint8_t { None, BadMagic, BadProtocol, TooLarge };

class FrameDecoder {
public:
    explicit FrameDecoder(std::uint32_t maxPayload = kDefaultMaxPayload) noexcept : m_maxPayload(maxPayload) {}

    // Append received bytes. Ignored once in error state.
    void push(std::span<const std::uint8_t> data);
    // Next complete frame, if any. Returns nullopt when more bytes are needed or on error.
    std::optional<Frame> next();

    FrameError error() const noexcept { return m_error; }
    std::size_t buffered() const noexcept { return m_buf.size() - m_pos; }

private:
    std::vector<std::uint8_t> m_buf;
    std::size_t m_pos = 0;
    std::uint32_t m_maxPayload;
    FrameError m_error = FrameError::None;
};

} // namespace nexlevel

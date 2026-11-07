#include <nexlevel/core/frame.hpp>

#include <nexlevel/core/bytes.hpp>
#include <nexlevel/core/version.hpp>

namespace nexlevel {

std::vector<std::uint8_t> encodeFrame(std::uint16_t type, std::span<const std::uint8_t> payload) {
    ByteWriter w;
    w.u16(kFrameMagic);
    w.u16(kProtocolVersion);
    w.u16(type);
    w.u32(static_cast<std::uint32_t>(payload.size()));
    w.bytes(payload);
    return w.take();
}

void FrameDecoder::push(std::span<const std::uint8_t> data) {
    if (m_error != FrameError::None) return;
    // Compact consumed bytes before growing.
    if (m_pos > 0 && m_pos * 2 >= m_buf.size()) {
        m_buf.erase(m_buf.begin(), m_buf.begin() + static_cast<std::ptrdiff_t>(m_pos));
        m_pos = 0;
    }
    m_buf.insert(m_buf.end(), data.begin(), data.end());
}

std::optional<Frame> FrameDecoder::next() {
    if (m_error != FrameError::None) return std::nullopt;
    if (buffered() < kFrameHeaderSize) return std::nullopt;

    ByteReader r({m_buf.data() + m_pos, kFrameHeaderSize});
    auto magic = r.u16();
    auto proto = r.u16();
    auto type = r.u16();
    auto len = r.u32();

    if (magic != kFrameMagic) { m_error = FrameError::BadMagic; return std::nullopt; }
    if (proto != kProtocolVersion) { m_error = FrameError::BadProtocol; return std::nullopt; }
    if (len > m_maxPayload) { m_error = FrameError::TooLarge; return std::nullopt; }
    if (buffered() < kFrameHeaderSize + len) return std::nullopt;

    Frame f;
    f.type = type;
    auto begin = m_buf.begin() + static_cast<std::ptrdiff_t>(m_pos + kFrameHeaderSize);
    f.payload.assign(begin, begin + len);
    m_pos += kFrameHeaderSize + len;
    return f;
}

} // namespace nexlevel

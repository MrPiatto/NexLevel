#include <nexlevel/core/relay/protocol.hpp>

#include <nexlevel/core/bytes.hpp>

namespace nexlevel::relay {

const char* toString(ErrorCode e) noexcept {
    switch (e) {
        case ErrorCode::NoRoom: return "room not found";
        case ErrorCode::RoomFull: return "room full";
        case ErrorCode::RelayBusy: return "relay busy";
        case ErrorCode::BadRequest: return "bad request";
        case ErrorCode::Timeout: return "host did not answer";
    }
    return "relay error";
}

std::optional<std::size_t> bodySize(Kind k) noexcept {
    switch (k) {
        case Kind::OpenRoom: return 12;
        case Kind::Accept: return 16;
        case Kind::Join: return 4;
        case Kind::Ping: return 0;
        case Kind::RoomOpened: return 12;
        case Kind::Incoming: return 4;
        case Kind::Pong: return 0;
        case Kind::Paired: return 0;
        case Kind::Error: return 1;
    }
    return std::nullopt;
}

std::vector<std::uint8_t> encode(const Message& m) {
    ByteWriter w;
    w.u32(kMagic);
    w.u8(kVersion);
    w.u8(static_cast<std::uint8_t>(m.kind));
    switch (m.kind) {
        case Kind::OpenRoom: w.u32(m.roomId); w.u64(m.secret); break;
        case Kind::Accept: w.u32(m.roomId); w.u64(m.secret); w.u32(m.stream); break;
        case Kind::Join: w.u32(m.roomId); break;
        case Kind::RoomOpened: w.u32(m.roomId); w.u64(m.secret); break;
        case Kind::Incoming: w.u32(m.stream); break;
        case Kind::Error: w.u8(static_cast<std::uint8_t>(m.error)); break;
        case Kind::Ping: case Kind::Pong: case Kind::Paired: break;
    }
    return w.take();
}

std::optional<Kind> decodeHeader(std::span<const std::uint8_t> header) {
    ByteReader r(header);
    const auto magic = r.u32();
    const auto version = r.u8();
    const auto kind = static_cast<Kind>(r.u8());
    if (!r.atEnd() || magic != kMagic || version != kVersion || !bodySize(kind)) return std::nullopt;
    return kind;
}

std::optional<Message> decodeBody(Kind k, std::span<const std::uint8_t> body) {
    auto size = bodySize(k);
    if (!size || body.size() != *size) return std::nullopt;
    ByteReader r(body);
    Message m;
    m.kind = k;
    switch (k) {
        case Kind::OpenRoom: m.roomId = r.u32(); m.secret = r.u64(); break;
        case Kind::Accept: m.roomId = r.u32(); m.secret = r.u64(); m.stream = r.u32(); break;
        case Kind::Join: m.roomId = r.u32(); break;
        case Kind::RoomOpened: m.roomId = r.u32(); m.secret = r.u64(); break;
        case Kind::Incoming: m.stream = r.u32(); break;
        case Kind::Error: {
            auto e = r.u8();
            if (e < 1 || e > 5) return std::nullopt;
            m.error = static_cast<ErrorCode>(e);
            break;
        }
        case Kind::Ping: case Kind::Pong: case Kind::Paired: break;
    }
    if (!r.atEnd()) return std::nullopt;
    return m;
}

} // namespace nexlevel::relay

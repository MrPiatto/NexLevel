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
        case Kind::Probe: return 0;
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
        case Kind::Ping: case Kind::Probe: case Kind::Pong: case Kind::Paired: break;
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
        case Kind::Ping: case Kind::Probe: case Kind::Pong: case Kind::Paired: break;
    }
    if (!r.atEnd()) return std::nullopt;
    return m;
}

} // namespace nexlevel::relay

#include <asio.hpp>

namespace nexlevel::relay {

std::vector<std::optional<float>> probeRelays(const std::vector<RelayEndpoint>& relays, std::chrono::milliseconds timeout) {
    using asio::ip::tcp;
    using Clock = std::chrono::steady_clock;
    std::vector<std::optional<float>> out(relays.size());
    asio::io_context io;
    struct Probe {
        tcp::resolver resolver;
        tcp::socket socket;
        std::array<std::uint8_t, kHeaderSize> reply{};
        std::vector<std::uint8_t> request;
        Clock::time_point sent{};
        explicit Probe(asio::io_context& io) : resolver(io), socket(io) {}
    };
    std::vector<std::unique_ptr<Probe>> probes;
    for (std::size_t i = 0; i < relays.size(); ++i) {
        auto& p = *probes.emplace_back(std::make_unique<Probe>(io));
        p.request = encode(Message{Kind::Probe});
        p.resolver.async_resolve(relays[i].host, std::to_string(relays[i].port), [&p, &out, i](std::error_code ec, tcp::resolver::results_type results) {
            if (ec) return;
            asio::async_connect(p.socket, results, [&p, &out, i](std::error_code cec, const tcp::endpoint&) {
                if (cec) return;
                p.sent = Clock::now();
                asio::async_write(p.socket, asio::buffer(p.request), [&p, &out, i](std::error_code wec, std::size_t) {
                    if (wec) return;
                    asio::async_read(p.socket, asio::buffer(p.reply), [&p, &out, i](std::error_code rec, std::size_t) {
                        if (rec || decodeHeader(p.reply) != Kind::Pong) return;
                        out[i] = std::chrono::duration<float, std::milli>(Clock::now() - p.sent).count();
                    });
                });
            });
        });
    }
    io.run_for(timeout);
    for (auto& p : probes) {
        std::error_code ec;
        p->socket.close(ec);
        p->resolver.cancel();
    }
    return out;
}

} // namespace nexlevel::relay

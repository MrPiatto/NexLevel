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
        case Kind::StatsRequest: return 0;
        case Kind::Stats: return 8;
        case Kind::Announce: return kListingSize;
        case Kind::ListRooms: return 0;
        case Kind::RoomList: return 2;
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
        case Kind::Stats: w.u32(m.statRooms); w.u32(m.statPlayers); break;
        case Kind::Announce: for (auto b : m.listing) w.u8(b); break;
        case Kind::RoomList: w.u16(m.count); break;
        case Kind::Ping: case Kind::Probe: case Kind::Pong: case Kind::Paired: case Kind::StatsRequest: case Kind::ListRooms: break;
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
        case Kind::Stats: m.statRooms = r.u32(); m.statPlayers = r.u32(); break;
        case Kind::Announce: for (auto& b : m.listing) b = r.u8(); break;
        case Kind::RoomList: m.count = r.u16(); break;
        case Kind::Ping: case Kind::Probe: case Kind::Pong: case Kind::Paired: case Kind::StatsRequest: case Kind::ListRooms: break;
    }
    if (!r.atEnd()) return std::nullopt;
    return m;
}

} // namespace nexlevel::relay

#include <asio.hpp>
#include <memory>
#include <system_error>

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

std::vector<std::optional<RelayCounters>> queryRelayStats(const std::vector<RelayEndpoint>& relays, std::chrono::milliseconds timeout) {
    using asio::ip::tcp;
    std::vector<std::optional<RelayCounters>> out(relays.size());
    asio::io_context io;
    struct Query {
        tcp::resolver resolver;
        tcp::socket socket;
        std::array<std::uint8_t, kHeaderSize + 8> reply{};
        std::vector<std::uint8_t> request;
        explicit Query(asio::io_context& io) : resolver(io), socket(io) {}
    };
    std::vector<std::unique_ptr<Query>> queries;
    for (std::size_t i = 0; i < relays.size(); ++i) {
        auto& q = *queries.emplace_back(std::make_unique<Query>(io));
        q.request = encode(Message{Kind::StatsRequest});
        q.resolver.async_resolve(relays[i].host, std::to_string(relays[i].port), [&q, &out, i](std::error_code ec, tcp::resolver::results_type results) {
            if (ec) return;
            asio::async_connect(q.socket, results, [&q, &out, i](std::error_code cec, const tcp::endpoint&) {
                if (cec) return;
                asio::async_write(q.socket, asio::buffer(q.request), [&q, &out, i](std::error_code wec, std::size_t) {
                    if (wec) return;
                    asio::async_read(q.socket, asio::buffer(q.reply), [&q, &out, i](std::error_code rec, std::size_t) {
                        if (rec) return;
                        std::span<const std::uint8_t> all(q.reply);
                        if (decodeHeader(all.first(kHeaderSize)) != Kind::Stats) return;
                        auto m = decodeBody(Kind::Stats, all.subspan(kHeaderSize));
                        if (m) out[i] = RelayCounters{m->statRooms, m->statPlayers};
                    });
                });
            });
        });
    }
    io.run_for(timeout);
    for (auto& q : queries) {
        std::error_code ec;
        q->socket.close(ec);
        q->resolver.cancel();
    }
    return out;
}

namespace {
void putText(std::array<std::uint8_t, kListingSize>& out, std::size_t at, std::size_t len, const std::string& text) {
    for (std::size_t i = 0; i < len && i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        out[at + i] = (c >= 32 && c < 127) ? c : '?';  // plain ASCII only (shown with GD's fonts)
    }
}
std::string getText(std::span<const std::uint8_t> in, std::size_t at, std::size_t len) {
    std::string s;
    for (std::size_t i = 0; i < len && in[at + i] != 0; ++i) {
        const auto c = in[at + i];
        s += (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
    }
    return s;
}
} // namespace

std::array<std::uint8_t, kListingSize> encodeListing(bool listed, std::uint8_t players, std::uint8_t maxPlayers,
                                                      std::uint16_t roomKey, const std::string& host, const std::string& level) {
    std::array<std::uint8_t, kListingSize> out{};
    out[0] = listed ? 1 : 0;
    out[1] = players;
    out[2] = maxPlayers;
    out[3] = static_cast<std::uint8_t>(roomKey & 0xFF);
    out[4] = static_cast<std::uint8_t>(roomKey >> 8);
    putText(out, 5, 21, host);
    putText(out, 26, 22, level);
    return out;
}

std::optional<PublicRoom> decodeListingRecord(std::span<const std::uint8_t> rec) {
    if (rec.size() != kListingRecordSize) return std::nullopt;
    PublicRoom r;
    r.roomId = static_cast<std::uint32_t>(rec[0]) | (static_cast<std::uint32_t>(rec[1]) << 8) |
               (static_cast<std::uint32_t>(rec[2]) << 16) | (static_cast<std::uint32_t>(rec[3]) << 24);
    auto l = rec.subspan(4);
    if (l[0] != 1 || r.roomId == 0) return std::nullopt;
    r.players = l[1];
    r.maxPlayers = l[2];
    r.roomKey = static_cast<std::uint16_t>(l[3] | (l[4] << 8));
    r.host = getText(l, 5, 21);
    r.level = getText(l, 26, 22);
    return r;
}

std::vector<PublicRoom> listPublicRooms(const RelayEndpoint& relay, std::chrono::milliseconds timeout) {
    using asio::ip::tcp;
    std::vector<PublicRoom> out;
    asio::io_context io;
    tcp::resolver resolver(io);
    tcp::socket socket(io);
    auto request = encode(Message{Kind::ListRooms});
    std::array<std::uint8_t, kHeaderSize + 2> head{};
    std::vector<std::uint8_t> records;
    resolver.async_resolve(relay.host, std::to_string(relay.port), [&](std::error_code ec, tcp::resolver::results_type results) {
        if (ec) return;
        asio::async_connect(socket, results, [&](std::error_code cec, const tcp::endpoint&) {
            if (cec) return;
            asio::async_write(socket, asio::buffer(request), [&](std::error_code wec, std::size_t) {
                if (wec) return;
                asio::async_read(socket, asio::buffer(head), [&](std::error_code rec, std::size_t) {
                    if (rec) return;
                    std::span<const std::uint8_t> h(head);
                    if (decodeHeader(h.first(kHeaderSize)) != Kind::RoomList) return;
                    auto m = decodeBody(Kind::RoomList, h.subspan(kHeaderSize));
                    if (!m || m->count == 0 || m->count > 200) return;
                    records.resize(static_cast<std::size_t>(m->count) * kListingRecordSize);
                    asio::async_read(socket, asio::buffer(records), [&](std::error_code rec2, std::size_t) {
                        if (rec2) return;
                        for (std::size_t i = 0; i + kListingRecordSize <= records.size(); i += kListingRecordSize) {
                            if (auto r = decodeListingRecord(std::span<const std::uint8_t>(records).subspan(i, kListingRecordSize))) {
                                out.push_back(std::move(*r));
                            }
                        }
                    });
                });
            });
        });
    });
    io.run_for(timeout);
    std::error_code ec;
    socket.close(ec);
    return out;
}

} // namespace nexlevel::relay

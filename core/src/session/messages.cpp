#include <nexlevel/core/session/messages.hpp>

namespace nexlevel::session {

std::vector<std::uint8_t> encode(const Hello& m) {
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(m.target));
    w.str(m.displayName.substr(0, kMaxDisplayName));
    return w.take();
}
std::vector<std::uint8_t> encode(const Welcome& m) {
    ByteWriter w;
    w.u16(m.peerId);
    w.u32(static_cast<std::uint32_t>(m.backlog.size()));
    for (const auto& op : m.backlog) sync::encodeOp(w, op);
    return w.take();
}
std::vector<std::uint8_t> encode(const Refuse& m) {
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(m.reason));
    w.u8(static_cast<std::uint8_t>(m.hostTarget));
    return w.take();
}
std::vector<std::uint8_t> encode(const Rejected& m) {
    ByteWriter w;
    w.u32(m.clientSeq);
    w.u8(m.reason);
    return w.take();
}
std::vector<std::uint8_t> encode(const Roster& m) {
    ByteWriter w;
    w.u16(static_cast<std::uint16_t>(m.peers.size()));
    for (const auto& p : m.peers) {
        w.u16(p.id);
        w.str(p.name.substr(0, kMaxDisplayName));
    }
    return w.take();
}
std::vector<std::uint8_t> encodeOpMsg(const sync::Op& op) {
    ByteWriter w;
    sync::encodeOp(w, op);
    return w.take();
}

std::optional<Hello> decodeHello(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    Hello m;
    m.target = static_cast<GameTarget>(r.u8());
    m.displayName = r.str(kMaxDisplayName);
    if (!r.atEnd()) return std::nullopt;
    return m;
}
std::optional<Welcome> decodeWelcome(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    Welcome m;
    m.peerId = r.u16();
    auto n = r.u32();
    if (!r.ok() || n > kMaxBacklogOps) return std::nullopt;
    m.backlog.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        auto op = sync::decodeOp(r);
        if (!op) return std::nullopt;
        m.backlog.push_back(std::move(*op));
    }
    if (!r.atEnd() || m.peerId == 0) return std::nullopt;
    return m;
}
std::optional<Refuse> decodeRefuse(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    Refuse m;
    m.reason = static_cast<RefuseReason>(r.u8());
    m.hostTarget = static_cast<GameTarget>(r.u8());
    if (!r.atEnd()) return std::nullopt;
    return m;
}
std::optional<Rejected> decodeRejected(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    Rejected m;
    m.clientSeq = r.u32();
    m.reason = r.u8();
    if (!r.atEnd()) return std::nullopt;
    return m;
}
std::optional<Roster> decodeRoster(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    Roster m;
    auto n = r.u16();
    if (!r.ok() || n > kMaxRosterPeers) return std::nullopt;
    for (std::uint16_t i = 0; i < n; ++i) {
        PeerInfo pi;
        pi.id = r.u16();
        pi.name = r.str(kMaxDisplayName);
        if (!r.ok()) return std::nullopt;
        m.peers.push_back(std::move(pi));
    }
    if (!r.atEnd()) return std::nullopt;
    return m;
}
std::optional<sync::Op> decodeOpMsg(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    auto op = sync::decodeOp(r);
    if (!op || !r.atEnd()) return std::nullopt;
    return op;
}

} // namespace nexlevel::session

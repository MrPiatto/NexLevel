#include <nexlevel/core/session/messages.hpp>

#include <nexlevel/core/compress.hpp>

#include <algorithm>

namespace nexlevel::session {

namespace {
void writeProps(ByteWriter& w, const sync::PropMap& props) {
    w.u32(static_cast<std::uint32_t>(props.size()));
    for (const auto& [k, v] : props) {
        w.u32(static_cast<std::uint32_t>(k));
        w.str(v);
    }
}
bool readProps(ByteReader& r, sync::PropMap& out) {
    const sync::OpLimits limits;
    auto n = r.u32();
    if (!r.ok() || n > limits.maxProps) return false;
    for (std::uint32_t i = 0; i < n; ++i) {
        auto k = static_cast<int>(r.u32());
        auto v = r.str(limits.maxValueLen);
        if (!r.ok() || !out.emplace(k, std::move(v)).second) return false;
    }
    return true;
}
} // namespace

std::vector<std::uint8_t> encode(const Hello& m) {
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(m.target));
    w.u16(m.roomKey);
    w.u64(m.resumeToken);
    w.str(m.displayName.substr(0, kMaxDisplayName));
    w.bytes(m.publicKey);
    w.u32(m.accountId);
    return w.take();
}
bool validPresence(const Presence& p) noexcept {
    auto ok = [](float v, float lim) { return v == v && v > -lim && v < lim; };  // v == v: not NaN
    return ok(p.x, 1e7f) && ok(p.y, 1e7f) && p.zoom > 0.01f && p.zoom < 100.f;
}
std::vector<std::uint8_t> encode(const Presence& m) {
    ByteWriter w;
    w.u16(m.peer);
    w.f32(m.x);
    w.f32(m.y);
    w.f32(m.zoom);
    return w.take();
}
std::vector<std::uint8_t> encode(const Secure& m) {
    ByteWriter w;
    w.bytes(m.publicKey);
    return w.take();
}
std::vector<std::uint8_t> encode(const Welcome& m) {
    ByteWriter w;
    w.u16(m.peerId);
    w.u64(m.resumeToken);
    w.u32(m.lastClientSeq);
    w.u8(m.resumed ? 1 : 0);
    return w.take();
}
std::vector<std::uint8_t> encode(const SnapshotEnd& m) {
    ByteWriter w;
    w.u64(m.lastSeq);
    w.u32(m.objectCount);
    w.u32(m.tombstoneCount);
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
        w.u32(p.accountId);
    }
    return w.take();
}
std::vector<std::uint8_t> encodeOpMsg(const sync::Op& op) {
    ByteWriter w;
    sync::encodeOp(w, op);
    return w.take();
}

namespace {
std::vector<std::vector<std::uint8_t>> encodeChunks(const sync::Replica& state, std::size_t target);
}

std::vector<std::vector<std::uint8_t>> encodeSnapshotChunks(const sync::Replica& state) {
    return encodeChunks(state, kChunkTargetBytes);
}

std::vector<std::vector<std::uint8_t>> encodeSnapshotChunksZ(const sync::Replica& state) {
    auto raw = encodeChunks(state, kChunkTargetBytesZ);
    for (auto& chunk : raw) {
        auto packed = lzCompress(chunk);
        ByteWriter w;
        w.u32(static_cast<std::uint32_t>(chunk.size()));
        w.bytes(packed);
        chunk = w.take();
    }
    return raw;
}

std::optional<SnapshotChunk> decodeSnapshotChunkZ(std::span<const std::uint8_t> p) {
    if (p.size() < 4) return std::nullopt;
    const std::uint32_t rawSize = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
                                  (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    if (rawSize > kMaxChunkRawBytes) return std::nullopt;
    auto raw = lzDecompress(p.subspan(4), rawSize);
    if (!raw) return std::nullopt;
    return decodeSnapshotChunk(*raw);
}

namespace {
std::vector<std::vector<std::uint8_t>> encodeChunks(const sync::Replica& state, std::size_t target) {
    std::vector<std::vector<std::uint8_t>> chunks;
    // Chunk layout: u32 objectCount, objects..., u32 tombstoneCount, tombstones...
    ByteWriter body;
    std::uint32_t count = 0;
    auto flushObjects = [&](bool force) {
        if (count == 0 && !force) return;
        ByteWriter w;
        w.u32(count);
        w.bytes(body.data());
        w.u32(0);  // no tombstones in object chunks
        chunks.push_back(w.take());
        body = ByteWriter{};
        count = 0;
    };
    for (const auto& [id, obj] : state.objects()) {
        body.u64(id.value);
        writeProps(body, obj.props);
        ++count;
        if (body.size() >= target || count >= kMaxChunkEntries) flushObjects(false);
    }
    flushObjects(false);

    // Tombstones, 8 bytes each.
    std::vector<sync::NetId> dead(state.tombstones().begin(), state.tombstones().end());
    for (std::size_t i = 0; i < dead.size();) {
        std::size_t n = std::min(dead.size() - i, std::min<std::size_t>(target / 8, kMaxChunkEntries));
        ByteWriter w;
        w.u32(0);
        w.u32(static_cast<std::uint32_t>(n));
        for (std::size_t j = 0; j < n; ++j) w.u64(dead[i + j].value);
        chunks.push_back(w.take());
        i += n;
    }
    if (chunks.empty()) flushObjects(true);  // empty level: still one (empty) chunk
    return chunks;
}
} // namespace

std::optional<Hello> decodeHello(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    Hello m;
    m.target = static_cast<GameTarget>(r.u8());
    m.roomKey = r.u16();
    m.resumeToken = r.u64();
    m.displayName = r.str(kMaxDisplayName);
    auto pk = r.bytes(32);
    m.accountId = r.u32();
    if (!r.atEnd()) return std::nullopt;
    std::copy(pk.begin(), pk.end(), m.publicKey.begin());
    return m;
}
bool validPlayerState(const PlayerState& s) noexcept {
    auto ok = [](float v, float lim) { return v == v && v > -lim && v < lim; };
    return ok(s.x1, 1e7f) && ok(s.y1, 1e7f) && ok(s.rot1, 1e6f) && ok(s.scale1, 100.f) && ok(s.x2, 1e7f) && ok(s.y2, 1e7f) &&
           ok(s.rot2, 1e6f) && ok(s.scale2, 100.f) && s.mode1 < 32 && s.mode2 < 32 && s.icon1 < 2000 && s.icon2 < 2000;
}
std::vector<std::uint8_t> encode(const PlayerState& m) {
    ByteWriter w;
    w.u16(m.peer);
    w.u16(m.session);
    w.u8(m.flags);
    w.u8(m.mode1);
    w.u8(m.mode2);
    for (float v : {m.x1, m.y1, m.rot1, m.scale1, m.x2, m.y2, m.rot2, m.scale2}) w.f32(v);
    w.u16(m.icon1);
    w.u16(m.icon2);
    w.u8(m.r);
    w.u8(m.g);
    w.u8(m.b);
    return w.take();
}
std::optional<PlayerState> decodePlayerState(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    PlayerState m;
    m.peer = r.u16();
    m.session = r.u16();
    m.flags = r.u8();
    m.mode1 = r.u8();
    m.mode2 = r.u8();
    for (float* v : {&m.x1, &m.y1, &m.rot1, &m.scale1, &m.x2, &m.y2, &m.rot2, &m.scale2}) *v = r.f32();
    m.icon1 = r.u16();
    m.icon2 = r.u16();
    m.r = r.u8();
    m.g = r.u8();
    m.b = r.u8();
    if (!r.atEnd() || !validPlayerState(m)) return std::nullopt;
    return m;
}
std::vector<std::uint8_t> encode(const Ping& m) {
    ByteWriter w;
    w.u32(m.seq);
    w.u64(m.sentMicros);
    return w.take();
}
std::vector<std::uint8_t> encode(const Pong& m) {
    ByteWriter w;
    w.u32(m.seq);
    w.u64(m.sentMicros);
    w.u32(m.holdMicros);
    return w.take();
}
std::vector<std::uint8_t> encode(const PingTable& m) {
    ByteWriter w;
    w.u16(static_cast<std::uint16_t>(m.pings.size()));
    for (auto& [peer, ms] : m.pings) {
        w.u16(peer);
        w.u16(ms);
    }
    return w.take();
}
std::optional<Ping> decodePing(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    Ping m{r.u32(), r.u64()};
    if (!r.atEnd()) return std::nullopt;
    return m;
}
std::optional<Pong> decodePong(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    Pong m;
    m.seq = r.u32();
    m.sentMicros = r.u64();
    m.holdMicros = r.u32();
    if (!r.atEnd()) return std::nullopt;
    return m;
}
std::optional<PingTable> decodePingTable(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    PingTable m;
    auto n = r.u16();
    if (!r.ok() || n > kMaxRosterPeers) return std::nullopt;
    for (std::uint16_t i = 0; i < n; ++i) {
        auto peer = r.u16();
        auto ms = r.u16();
        m.pings.emplace_back(peer, ms);
    }
    if (!r.atEnd()) return std::nullopt;
    return m;
}
std::optional<Presence> decodePresence(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    Presence m;
    m.peer = r.u16();
    m.x = r.f32();
    m.y = r.f32();
    m.zoom = r.f32();
    if (!r.atEnd() || !validPresence(m)) return std::nullopt;
    return m;
}
std::optional<Secure> decodeSecure(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    Secure m;
    auto pk = r.bytes(32);
    if (!r.atEnd()) return std::nullopt;
    std::copy(pk.begin(), pk.end(), m.publicKey.begin());
    return m;
}
std::optional<Welcome> decodeWelcome(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    Welcome m;
    m.peerId = r.u16();
    m.resumeToken = r.u64();
    m.lastClientSeq = r.u32();
    m.resumed = r.u8() != 0;
    if (!r.atEnd() || m.peerId == 0) return std::nullopt;
    return m;
}
std::optional<SnapshotChunk> decodeSnapshotChunk(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    SnapshotChunk m;
    auto nObj = r.u32();
    if (!r.ok() || nObj > kMaxChunkEntries) return std::nullopt;
    m.objects.reserve(nObj);
    for (std::uint32_t i = 0; i < nObj; ++i) {
        sync::NetId id{r.u64()};
        sync::PropMap props;
        if (!r.ok() || !id.valid() || !readProps(r, props)) return std::nullopt;
        m.objects.emplace_back(id, std::move(props));
    }
    auto nDead = r.u32();
    if (!r.ok() || nDead > kMaxChunkEntries) return std::nullopt;
    m.tombstones.reserve(nDead);
    for (std::uint32_t i = 0; i < nDead; ++i) m.tombstones.push_back(sync::NetId{r.u64()});
    if (!r.atEnd()) return std::nullopt;
    return m;
}
std::optional<SnapshotEnd> decodeSnapshotEnd(std::span<const std::uint8_t> p) {
    ByteReader r(p);
    SnapshotEnd m;
    m.lastSeq = r.u64();
    m.objectCount = r.u32();
    m.tombstoneCount = r.u32();
    if (!r.atEnd()) return std::nullopt;
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
        pi.accountId = r.u32();
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

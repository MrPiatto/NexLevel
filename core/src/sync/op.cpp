#include <nexlevel/core/sync/op.hpp>

namespace nexlevel::sync {

void encodeOp(ByteWriter& w, const Op& op) {
    w.u8(static_cast<std::uint8_t>(op.kind));
    w.u16(op.author);
    w.u32(op.clientSeq);
    w.u64(op.seq);
    w.u64(op.target.value);
    w.u32(static_cast<std::uint32_t>(op.props.size()));
    for (const auto& [k, v] : op.props) {
        w.u32(static_cast<std::uint32_t>(k));
        w.str(v);
    }
    w.u32(static_cast<std::uint32_t>(op.removedKeys.size()));
    for (int k : op.removedKeys) w.u32(static_cast<std::uint32_t>(k));
}

std::optional<Op> decodeOp(ByteReader& r, const OpLimits& limits) {
    Op op;
    auto kind = r.u8();
    if (kind < 1 || kind > 3) return std::nullopt;
    op.kind = static_cast<OpKind>(kind);
    op.author = r.u16();
    op.clientSeq = r.u32();
    op.seq = r.u64();
    op.target.value = r.u64();

    auto nProps = r.u32();
    if (!r.ok() || nProps > limits.maxProps) return std::nullopt;
    for (std::uint32_t i = 0; i < nProps; ++i) {
        auto k = static_cast<int>(r.u32());
        auto v = r.str(limits.maxValueLen);
        if (!r.ok() || !op.props.emplace(k, std::move(v)).second) return std::nullopt;  // duplicate key
    }
    auto nRemoved = r.u32();
    if (!r.ok() || nRemoved > limits.maxRemovedKeys) return std::nullopt;
    op.removedKeys.reserve(nRemoved);
    for (std::uint32_t i = 0; i < nRemoved; ++i) op.removedKeys.push_back(static_cast<int>(r.u32()));
    if (!r.ok()) return std::nullopt;

    // Shape checks per kind.
    if (!op.target.valid()) return std::nullopt;
    switch (op.kind) {
        case OpKind::Create:
            if (!op.removedKeys.empty()) return std::nullopt;
            break;
        case OpKind::Delete:
            if (!op.props.empty() || !op.removedKeys.empty()) return std::nullopt;
            break;
        case OpKind::SetProps:
            if (op.props.empty() && op.removedKeys.empty()) return std::nullopt;
            break;
    }
    return op;
}

} // namespace nexlevel::sync

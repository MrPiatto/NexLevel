#include <nexlevel/core/sync/host_sequencer.hpp>

namespace nexlevel::sync {

HostSequencer::Result HostSequencer::submit(Op op) {
    // Transport is reliable and ordered per peer, so anything at or below the highest seen clientSeq
    // is a resend (e.g. after reconnect) and must not be applied twice.
    auto& highest = m_highestClientSeq[op.author];
    if (op.clientSeq <= highest) return {std::nullopt, Reject::Duplicate};
    // A peer may only mint ids in its own namespace.
    if (op.kind == OpKind::Create && op.target.peer() != op.author) return {std::nullopt, Reject::ForgedId};

    highest = op.clientSeq;
    op.seq = m_nextSeq++;
    m_state.apply(op);
    m_log.push_back(op);
    return {std::move(op), Reject::None};
}

std::vector<Op> HostSequencer::opsSince(std::uint64_t after) const {
    std::vector<Op> out;
    for (const auto& op : m_log) {
        if (op.seq > after) out.push_back(op);
    }
    return out;
}

} // namespace nexlevel::sync

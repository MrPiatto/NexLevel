#pragma once
// Host side of a session: the single authority that orders operations.
// The session layer must set Op::author from the authenticated connection, never from the payload.
#include <nexlevel/core/sync/replica.hpp>

#include <optional>
#include <unordered_map>
#include <vector>

namespace nexlevel::sync {

class HostSequencer {
public:
    enum class Reject : std::uint8_t { None, Duplicate, ForgedId };

    struct Result {
        std::optional<Op> sequenced;  // broadcast this to every peer, including the author (it is the ack)
        Reject reason = Reject::None;
    };

    // Ops that do not change state (e.g. SetProps on a deleted object) are still sequenced:
    // the broadcast acknowledges the author's pending op, and every replica ignores it the same way.
    Result submit(Op op);

    const Replica& state() const noexcept { return m_state; }
    std::uint64_t lastSeq() const noexcept { return m_nextSeq - 1; }
    std::uint32_t highestClientSeq(PeerId peer) const {
        auto it = m_highestClientSeq.find(peer);
        return it == m_highestClientSeq.end() ? 0 : it->second;
    }

    // For reconnect replay: every sequenced op with seq > after, oldest first.
    std::vector<Op> opsSince(std::uint64_t after) const;

private:
    Replica m_state;
    std::uint64_t m_nextSeq = 1;
    std::unordered_map<PeerId, std::uint32_t> m_highestClientSeq;
    std::vector<Op> m_log;  // every sequenced op (late joiners get a snapshot instead)
};

} // namespace nexlevel::sync

#pragma once
// Client side of a session.
//   confirmed - host-ordered truth, identical on every peer
//   view      - what the local editor shows: confirmed plus this peer's not-yet-acknowledged ops
// A remote change to an object with pending local edits is applied underneath them, so the local user's
// own edit does not flicker back; once the host acknowledges it, view and confirmed agree again.
#include <nexlevel/core/sync/replica.hpp>

#include <deque>
#include <vector>

namespace nexlevel::sync {

class ClientReplica {
public:
    explicit ClientReplica(PeerId self) : m_self(self) {}

    // A local editor change: stamps author/clientSeq, applies it to the view, returns the op to send.
    Op local(Op op);

    // A sequenced op broadcast by the host. Returns the ids whose *visible* state changed,
    // i.e. what the editor adapter must apply to GD objects.
    std::vector<NetId> onSequenced(const Op& op);

    // The host refused one of our ops (it will never be sequenced): drop it and restore the view.
    std::vector<NetId> onRejected(std::uint32_t clientSeq);

    const Replica& confirmed() const noexcept { return m_confirmed; }
    const Replica& view() const noexcept { return m_view; }
    const std::deque<Op>& pending() const noexcept { return m_pending; }
    PeerId self() const noexcept { return m_self; }

private:
    void rebuildViewObject(NetId id);

    PeerId m_self;
    std::uint32_t m_nextClientSeq = 1;
    Replica m_confirmed;
    Replica m_view;
    std::deque<Op> m_pending;  // in send order; acks arrive in the same order
};

} // namespace nexlevel::sync

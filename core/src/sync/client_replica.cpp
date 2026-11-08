#include <nexlevel/core/sync/client_replica.hpp>

namespace nexlevel::sync {

Op ClientReplica::local(Op op) {
    op.author = m_self;
    op.clientSeq = m_nextClientSeq++;
    op.seq = 0;
    m_view.apply(op);
    m_pending.push_back(op);
    return op;
}

std::vector<NetId> ClientReplica::onSequenced(const Op& op) {
    if (op.author == m_self && !m_pending.empty() && m_pending.front().clientSeq == op.clientSeq) {
        m_pending.pop_front();  // our own op came back: acknowledged
    }
    m_confirmed.apply(op);

    const ObjectState* beforePtr = m_view.find(op.target);
    const bool existedBefore = beforePtr != nullptr;
    ObjectState before = existedBefore ? *beforePtr : ObjectState{};

    rebuildViewObject(op.target);

    const ObjectState* after = m_view.find(op.target);
    const bool changed = (existedBefore != (after != nullptr)) || (after && !(before == *after));
    if (changed) return {op.target};
    return {};
}

std::vector<NetId> ClientReplica::onRejected(std::uint32_t clientSeq) {
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (it->clientSeq == clientSeq) {
            NetId id = it->target;
            m_pending.erase(it);
            rebuildViewObject(id);
            return {id};
        }
    }
    return {};
}

void ClientReplica::rebuildViewObject(NetId id) {
    m_view.copyObjectFrom(m_confirmed, id);
    for (const auto& p : m_pending) {
        if (p.target == id) m_view.apply(p);
    }
}

} // namespace nexlevel::sync

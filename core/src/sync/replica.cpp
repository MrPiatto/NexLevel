#include <nexlevel/core/sync/replica.hpp>

namespace nexlevel::sync {

bool Replica::apply(const Op& op) {
    if (op.seq > m_lastSeq) m_lastSeq = op.seq;
    switch (op.kind) {
        case OpKind::Create: {
            if (m_tombstones.contains(op.target) || m_objects.contains(op.target)) return false;
            m_objects.emplace(op.target, ObjectState{op.props});
            return true;
        }
        case OpKind::Delete: {
            m_tombstones.insert(op.target);
            return m_objects.erase(op.target) > 0;
        }
        case OpKind::SetProps: {
            auto it = m_objects.find(op.target);
            if (it == m_objects.end()) return false;
            auto before = it->second.props;
            for (int k : op.removedKeys) it->second.props.erase(k);
            for (const auto& [k, v] : op.props) it->second.props[k] = v;
            return it->second.props != before;
        }
    }
    return false;
}

const ObjectState* Replica::find(NetId id) const {
    auto it = m_objects.find(id);
    return it == m_objects.end() ? nullptr : &it->second;
}

void Replica::copyObjectFrom(const Replica& src, NetId id) {
    m_objects.erase(id);
    m_tombstones.erase(id);
    if (src.m_tombstones.contains(id)) m_tombstones.insert(id);
    if (auto* o = src.find(id)) m_objects.emplace(id, *o);
}

std::string Replica::fingerprint() const {
    std::string out;
    for (const auto& [id, o] : m_objects) {
        out += std::to_string(id.value);
        out += '{';
        out += toSaveString(o.props);
        out += "};";
    }
    out += "|dead:";
    for (auto id : m_tombstones) {
        out += std::to_string(id.value);
        out += ',';
    }
    return out;
}

} // namespace nexlevel::sync

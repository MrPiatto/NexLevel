#pragma once
// Deterministic replicated level state. Every peer applies the host's sequenced ops in the same order,
// so every confirmed Replica ends up identical. Rules:
//   Create   - ignored if the id exists or was deleted (no resurrection, no duplicates)
//   Delete   - removes the object and leaves a tombstone; later ops on that id are ignored
//   SetProps - ignored if the object does not exist; otherwise removed keys, then changed keys
#include <nexlevel/core/sync/op.hpp>

#include <map>
#include <set>
#include <string>

namespace nexlevel::sync {

struct ObjectState {
    PropMap props;
    bool operator==(const ObjectState&) const = default;
};

class Replica {
public:
    // Returns true if the state changed.
    bool apply(const Op& op);

    const ObjectState* find(NetId id) const;
    bool isDeleted(NetId id) const { return m_tombstones.contains(id); }
    const std::map<NetId, ObjectState>& objects() const noexcept { return m_objects; }
    std::uint64_t lastSeq() const noexcept { return m_lastSeq; }

    // Copies one object's existence/tombstone/props from another replica (used to rebuild views).
    void copyObjectFrom(const Replica& src, NetId id);

    // Deterministic text form, for convergence checks and debugging.
    std::string fingerprint() const;

private:
    std::map<NetId, ObjectState> m_objects;
    std::set<NetId> m_tombstones;
    std::uint64_t m_lastSeq = 0;
};

} // namespace nexlevel::sync

#pragma once
// NetId: session-wide object identity. GD's GameObject::m_uniqueID is runtime-only and changes across
// undo/redo (seen in Phase 1 tests), so every synced object gets a NetId instead.
// Layout: high 16 bits = author peer id, low 48 bits = per-peer counter. Peers mint ids without asking
// the host, and ids from different peers can never collide.
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace nexlevel::sync {

using PeerId = std::uint16_t;

struct NetId {
    std::uint64_t value = 0;

    static constexpr std::uint64_t kCounterMask = 0x0000FFFFFFFFFFFFull;

    static constexpr NetId make(PeerId peer, std::uint64_t counter) noexcept {
        return NetId{(static_cast<std::uint64_t>(peer) << 48) | (counter & kCounterMask)};
    }
    constexpr PeerId peer() const noexcept { return static_cast<PeerId>(value >> 48); }
    constexpr std::uint64_t counter() const noexcept { return value & kCounterMask; }
    constexpr bool valid() const noexcept { return counter() != 0; }

    auto operator<=>(const NetId&) const = default;
};

struct NetIdHash {
    std::size_t operator()(NetId id) const noexcept { return std::hash<std::uint64_t>{}(id.value); }
};

class NetIdAllocator {
public:
    explicit NetIdAllocator(PeerId peer, std::uint64_t firstCounter = 1) noexcept : m_peer(peer), m_next(firstCounter) {}
    NetId next() noexcept { return NetId::make(m_peer, m_next++); }
    PeerId peer() const noexcept { return m_peer; }
    // After a reconnect/snapshot: never reuse a counter this peer already used.
    void ensureAbove(std::uint64_t counter) noexcept { if (m_next <= counter) m_next = counter + 1; }

private:
    PeerId m_peer;
    std::uint64_t m_next;
};

} // namespace nexlevel::sync

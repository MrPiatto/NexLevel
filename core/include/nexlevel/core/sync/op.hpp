#pragma once
// Editing operations: the unit clients send to the host and the host broadcasts in global order.
#include <nexlevel/core/bytes.hpp>
#include <nexlevel/core/sync/netid.hpp>
#include <nexlevel/core/sync/object_props.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace nexlevel::sync {

enum class OpKind : std::uint8_t {
    Create = 1,    // target + full props
    Delete = 2,    // target
    SetProps = 3,  // target + changed props + removed keys
};

struct Op {
    OpKind kind = OpKind::SetProps;
    PeerId author = 0;
    std::uint32_t clientSeq = 0;  // per-author, strictly increasing; used for dedupe and acks
    std::uint64_t seq = 0;        // global order assigned by the host; 0 = not sequenced yet
    NetId target;
    PropMap props;                // Create: all props; SetProps: changed/added props
    std::vector<int> removedKeys; // SetProps only

    static Op create(NetId id, PropMap props) { Op o; o.kind = OpKind::Create; o.target = id; o.props = std::move(props); return o; }
    static Op erase(NetId id) { Op o; o.kind = OpKind::Delete; o.target = id; return o; }
    static Op setProps(NetId id, const PropDiff& d) {
        Op o; o.kind = OpKind::SetProps; o.target = id;
        for (const auto& [k, v] : d.changed) o.props[k] = v;
        o.removedKeys = d.removed;
        return o;
    }
};

// Hard caps applied while decoding untrusted input.
struct OpLimits {
    std::size_t maxProps = 512;
    std::size_t maxValueLen = 64 * 1024;  // text objects carry base64 strings
    std::size_t maxRemovedKeys = 512;
};

void encodeOp(ByteWriter& w, const Op& op);
// nullopt on truncated input, unknown kind, limit violations, or an op that is invalid for its kind.
std::optional<Op> decodeOp(ByteReader& r, const OpLimits& limits = {});

} // namespace nexlevel::sync

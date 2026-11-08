#pragma once
// Session messages carried in NexLevel frames (frame type = MsgType).
#include <nexlevel/core/sync/op.hpp>
#include <nexlevel/core/version.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace nexlevel::session {

enum class MsgType : std::uint16_t {
    Hello = 1,      // client -> host: who I am, which game line
    Welcome = 2,    // host -> client: your peer id + everything sequenced so far
    Refuse = 3,     // host -> client: join refused (then the host closes the connection)
    Roster = 4,     // host -> all: who is in the room (sent on every join/leave/kick)
    Kicked = 5,     // host -> client: removed by the host (then closed)
    Closing = 6,    // host -> all: the host closed the room
    Submit = 10,    // client -> host: a local op
    Sequenced = 11, // host -> all: an op in global order (also the author's ack)
    Rejected = 12,  // host -> author: op refused, will never be sequenced
};

enum class RefuseReason : std::uint8_t { WrongGameVersion = 1, SessionFull = 2, BadHello = 3 };

struct PeerInfo {
    sync::PeerId id = 0;
    std::string name;
    bool operator==(const PeerInfo&) const = default;
};
struct Roster {
    std::vector<PeerInfo> peers;  // host first
};
inline constexpr std::size_t kMaxRosterPeers = 64;

struct Hello {
    GameTarget target = GameTarget::Unknown;
    std::string displayName;
};
struct Welcome {
    sync::PeerId peerId = 0;
    std::vector<sync::Op> backlog;  // Phase 2: full op log. Phase 5 replaces this with a snapshot + tail.
};
struct Refuse {
    RefuseReason reason = RefuseReason::BadHello;
    GameTarget hostTarget = GameTarget::Unknown;
};
struct Rejected {
    std::uint32_t clientSeq = 0;
    std::uint8_t reason = 0;
};

inline constexpr std::size_t kMaxDisplayName = 32;
inline constexpr std::size_t kMaxBacklogOps = 1'000'000;

std::vector<std::uint8_t> encode(const Hello& m);
std::vector<std::uint8_t> encode(const Welcome& m);
std::vector<std::uint8_t> encode(const Refuse& m);
std::vector<std::uint8_t> encode(const Rejected& m);
std::vector<std::uint8_t> encode(const Roster& m);
std::vector<std::uint8_t> encodeOpMsg(const sync::Op& op);  // Submit and Sequenced payload

std::optional<Hello> decodeHello(std::span<const std::uint8_t> p);
std::optional<Welcome> decodeWelcome(std::span<const std::uint8_t> p);
std::optional<Refuse> decodeRefuse(std::span<const std::uint8_t> p);
std::optional<Rejected> decodeRejected(std::span<const std::uint8_t> p);
std::optional<Roster> decodeRoster(std::span<const std::uint8_t> p);
std::optional<sync::Op> decodeOpMsg(std::span<const std::uint8_t> p);

} // namespace nexlevel::session

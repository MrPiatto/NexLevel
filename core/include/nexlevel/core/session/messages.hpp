#pragma once
// Session messages carried in NexLevel frames (frame type = MsgType).
#include <nexlevel/core/sync/replica.hpp>
#include <nexlevel/core/version.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <array>
#include <vector>

namespace nexlevel::session {

enum class MsgType : std::uint16_t {
    Hello = 1,         // client -> host: who I am, game line, room key, resume token
    Welcome = 2,       // host -> client: peer id + resume token; snapshot follows
    Refuse = 3,        // host -> client: join refused (then the host closes the connection)
    Roster = 4,        // host -> all: who is in the room (sent on every join/leave/kick)
    Kicked = 5,        // host -> client: removed by the host (then closed)
    Closing = 6,       // host -> all: the host closed the room
    SnapshotChunk = 7, // host -> joining client: part of the level state
    SnapshotEnd = 8,   // host -> joining client: snapshot complete, live ops follow
    Secure = 9,        // host -> client (plain): host key; from here on every frame is Sealed
    Submit = 10,       // client -> host: a local op
    Sequenced = 11,    // host -> all: an op in global order (also the author's ack)
    Rejected = 12,     // host -> author: op refused, will never be sequenced
    PresenceUpdate = 13,  // client -> host: where I am looking in the editor (not level content)
    Presence = 14,        // host -> others: a player's editor position (rate limited, not stored)
    Ping = 15,            // either way, every second: measure round-trip time
    Pong = 16,            // answer to Ping (echoes it, says how long it waited before answering)
    PingTable = 17,       // host -> all: everyone's ping to the host
    PlayerState = 18,     // either way: a player's icon while they playtest (spectating + trail)
    SnapshotChunkZ = 21,
    Chat = 22,            // client -> host: a chat line; host -> others: who said it + the line  // host -> joining client: SnapshotChunk, compressed (u32 raw size + LZ block)
    Sealed = 20,       // encrypted envelope around any of the above (see crypto/secure_channel.hpp)
};

enum class RefuseReason : std::uint8_t { WrongGameVersion = 1, SessionFull = 2, BadHello = 3, WrongRoomCode = 4, Banned = 5 };

struct PeerInfo {
    sync::PeerId id = 0;
    std::string name;
    std::uint32_t accountId = 0;  // GD account (for the profile button); self-reported, 0 = none
    bool viewOnly = false;        // the host turned off editing for this player
    bool operator==(const PeerInfo&) const = default;
};
struct Roster {
    std::vector<PeerInfo> peers;  // host first
};
inline constexpr std::size_t kMaxRosterPeers = 64;

struct Hello {
    GameTarget target = GameTarget::Unknown;
    std::uint16_t roomKey = 0;
    std::uint64_t resumeToken = 0;  // 0 = new player; otherwise reclaim the same identity after a drop
    std::string displayName;
    std::array<std::uint8_t, 32> publicKey{};  // client's X25519 key for this connection
    std::uint32_t accountId = 0;
};
struct Secure {
    std::array<std::uint8_t, 32> publicKey{};  // host's X25519 key for this connection
};
struct Welcome {
    sync::PeerId peerId = 0;
    std::uint64_t resumeToken = 0;
    std::uint32_t lastClientSeq = 0;  // highest op of this peer the host has applied (for resume)
    bool resumed = false;
};
struct SnapshotChunk {
    std::vector<std::pair<sync::NetId, sync::PropMap>> objects;
    std::vector<sync::NetId> tombstones;
};
struct SnapshotEnd {
    std::uint64_t lastSeq = 0;
    std::uint32_t objectCount = 0;
    std::uint32_t tombstoneCount = 0;
};
struct Refuse {
    RefuseReason reason = RefuseReason::BadHello;
    GameTarget hostTarget = GameTarget::Unknown;
};
inline constexpr std::size_t kMaxChatLen = 200;
struct ChatMessage {
    sync::PeerId peer = 0;  // filled in by the host (never trusted from a client)
    std::string text;
};
struct Rejected {
    std::uint32_t clientSeq = 0;
    std::uint8_t reason = 0;
};

// Editor presence: camera centre in level coordinates and zoom. Never part of the level.
struct Presence {
    sync::PeerId peer = 0;  // ignored in PresenceUpdate (the host fills it from the connection)
    float x = 0, y = 0, zoom = 1;
    bool operator==(const Presence&) const = default;
};
bool validPresence(const Presence& p) noexcept;

// A playtesting player's icon(s), streamed ~30 times per second so the others see them play.
struct PlayerState {
    enum Flags : std::uint8_t { Playing = 1, Dead = 2, Dual = 4, UpsideDown1 = 8, UpsideDown2 = 16 };
    sync::PeerId peer = 0;      // filled by the host
    std::uint16_t session = 0;  // changes on every new playtest (a new trail starts)
    std::uint8_t flags = 0;
    std::uint8_t mode1 = 0, mode2 = 0;  // GD IconType (cube, ship, ball, ufo, wave, robot, spider, swing...)
    float x1 = 0, y1 = 0, rot1 = 0, scale1 = 1;
    float x2 = 0, y2 = 0, rot2 = 0, scale2 = 1;
    std::uint16_t icon1 = 1, icon2 = 1;  // the player's own icon for the current modes
    std::uint8_t r = 255, g = 255, b = 255;  // the player's main icon colour
    bool playing() const noexcept { return flags & Playing; }
};
bool validPlayerState(const PlayerState& s) noexcept;

struct Ping {
    std::uint32_t seq = 0;
    std::uint64_t sentMicros = 0;  // sender's clock, echoed back
};
struct Pong {
    std::uint32_t seq = 0;
    std::uint64_t sentMicros = 0;
    std::uint32_t holdMicros = 0;  // time between receiving the Ping and sending this (subtracted)
};
struct PingTable {
    std::vector<std::pair<sync::PeerId, std::uint16_t>> pings;  // peer -> ms to the host
};

inline constexpr std::size_t kMaxDisplayName = 32;
inline constexpr std::size_t kChunkTargetBytes = 192 * 1024;  // well under the 1 MiB frame cap
inline constexpr std::size_t kMaxChunkEntries = 200'000;
inline constexpr std::size_t kChunkTargetBytesZ = 512 * 1024;  // raw size of a compressed chunk
inline constexpr std::size_t kMaxChunkRawBytes = 1024 * 1024;

std::vector<std::uint8_t> encode(const Hello& m);
std::vector<std::uint8_t> encode(const Secure& m);
std::vector<std::uint8_t> encode(const Presence& m);
std::vector<std::uint8_t> encode(const Ping& m);
std::vector<std::uint8_t> encode(const Pong& m);
std::vector<std::uint8_t> encode(const PingTable& m);
std::vector<std::uint8_t> encode(const PlayerState& m);
std::vector<std::uint8_t> encode(const Welcome& m);
std::vector<std::uint8_t> encode(const SnapshotEnd& m);
std::vector<std::uint8_t> encode(const Refuse& m);
std::vector<std::uint8_t> encode(const Rejected& m);
std::vector<std::uint8_t> encode(const Roster& m);
std::vector<std::uint8_t> encodeOpMsg(const sync::Op& op);  // Submit and Sequenced payload
// Splits a replica into SnapshotChunk payloads of ~kChunkTargetBytes each (at least one chunk).
std::vector<std::vector<std::uint8_t>> encodeSnapshotChunks(const sync::Replica& state);
// Same chunks, bigger and compressed (SnapshotChunkZ payloads). Big levels shrink several times.
std::vector<std::vector<std::uint8_t>> encodeSnapshotChunksZ(const sync::Replica& state);

std::optional<Hello> decodeHello(std::span<const std::uint8_t> p);
std::optional<Secure> decodeSecure(std::span<const std::uint8_t> p);
std::optional<Presence> decodePresence(std::span<const std::uint8_t> p);
std::optional<Ping> decodePing(std::span<const std::uint8_t> p);
std::optional<Pong> decodePong(std::span<const std::uint8_t> p);
std::optional<PingTable> decodePingTable(std::span<const std::uint8_t> p);
std::optional<PlayerState> decodePlayerState(std::span<const std::uint8_t> p);
std::optional<Welcome> decodeWelcome(std::span<const std::uint8_t> p);
std::optional<SnapshotChunk> decodeSnapshotChunk(std::span<const std::uint8_t> p);
std::optional<SnapshotChunk> decodeSnapshotChunkZ(std::span<const std::uint8_t> p);
std::optional<SnapshotEnd> decodeSnapshotEnd(std::span<const std::uint8_t> p);
std::optional<Refuse> decodeRefuse(std::span<const std::uint8_t> p);
std::optional<Rejected> decodeRejected(std::span<const std::uint8_t> p);
std::vector<std::uint8_t> encode(const ChatMessage& m);
std::optional<ChatMessage> decodeChat(std::span<const std::uint8_t> p);
// Chat text as shown: printable characters only, trimmed, at most kMaxChatLen.
std::string cleanChatText(std::string_view text);
std::optional<Roster> decodeRoster(std::span<const std::uint8_t> p);
std::optional<sync::Op> decodeOpMsg(std::span<const std::uint8_t> p);

} // namespace nexlevel::session

#include <nexlevel/core/session/client.hpp>

#include "../net/connection.hpp"

#include <nexlevel/core/crypto/secure_channel.hpp>
#include <nexlevel/core/relay/protocol.hpp>
#include <nexlevel/core/session/playtest.hpp>
#include <nexlevel/core/session/room_code.hpp>
#include <nexlevel/core/net/local_address.hpp>

#include <algorithm>
#include <chrono>
#include <future>
#include <set>

namespace nexlevel::session {

using asio::ip::tcp;
using Clock = std::chrono::steady_clock;

namespace {
constexpr auto kRetryInterval = std::chrono::seconds(1);
constexpr int kMaxReconnectAttempts = 30;
constexpr auto kConnectTimeout = std::chrono::seconds(10);
}

struct SessionClient::Impl {
    struct Event {
        enum Kind { Connected, FrameIn, Closed, ConnectFailed } kind;
        std::uint32_t generation = 0;
        Frame frame;
        std::string reason;
        std::optional<ConnectTarget> via;     // Connected: which address won
        Clock::time_point at = Clock::now();  // when it reached the network thread (ping timing)
    };

    GameTarget target;
    std::string name;
    net::NetThread net;
    bool netStarted = false;

    // Network thread only.
    std::shared_ptr<net::Connection> conn;
    net::EventQueue<Event> events;

    // Main thread only.
    ClientState state = ClientState::Idle;
    std::string error;
    std::optional<RefuseReason> refuse;
    std::optional<GameTarget> hostTarget;
    std::optional<sync::ClientReplica> replica;
    std::optional<sync::NetIdAllocator> ids;
    std::vector<PeerInfo> peers;
    // Addresses to try for this room, raced in parallel; the first to connect wins.
    std::vector<ConnectTarget> targets;
    std::vector<relay::RelayEndpoint> relays;  // directory + custom (id kCustomRelayId)
    std::uint16_t roomKey = 0;
    std::uint64_t resumeToken = 0;
    std::uint32_t generation = 0;  // ignores events from connections that were replaced
    bool attemptInFlight = false;
    bool helloSent = false;
    int reconnectAttempts = 0;
    Clock::time_point nextRetry{};
    bool identityReset = false;
    std::map<sync::PeerId, Presence> presences;
    RttTracker rtt;
    std::map<sync::PeerId, std::uint16_t> pingTable;
    std::map<sync::PeerId, PlayTrail> trails;
    Clock::time_point lastPlayerStateSent{};
    std::uint8_t lastPlayerFlags = 0;
    std::uint32_t accountId = 0;
    std::optional<ConnectTarget> via;
    std::optional<Presence> lastSentPresence;
    Clock::time_point lastPresenceSent{};
    std::optional<crypto::KeyPair> keys;           // this connection's key pair
    std::optional<crypto::SecureChannel> channel;  // set once the host's key arrived

    // Snapshot being received.
    std::optional<Welcome> welcome;
    std::map<sync::NetId, sync::ObjectState> incomingObjects;
    std::set<sync::NetId> incomingTombstones;

    Impl(GameTarget t, std::string n) : target(t), name(std::move(n)) {}

    void send(MsgType type, std::vector<std::uint8_t> payload) {
        auto bytes = channel ? encodeFrame(static_cast<std::uint16_t>(MsgType::Sealed), channel->seal(static_cast<std::uint16_t>(type), payload))
                             : encodeFrame(static_cast<std::uint16_t>(type), payload);
        net.post([this, bytes = std::move(bytes)]() mutable {
            if (conn) conn->send(std::move(bytes));
        });
    }

    void startAttempt() {
        if (!netStarted) {
            net.start();
            netStarted = true;
        }
        attemptInFlight = true;
        helloSent = false;
        channel.reset();
        keys.reset();
        const auto gen = ++generation;
        net.post([this, gen, targets = targets] {
            if (conn) {
                conn->close("replaced");
                conn.reset();
            }
            // Happy-eyeballs style race over all candidate addresses, with an overall timeout
            // (a connect to an unreachable public address can otherwise hang for ~20 s).
            struct Race {
                bool done = false;
                std::size_t pending = 0;
                std::string lastError;
                std::vector<std::shared_ptr<tcp::socket>> sockets;
                std::shared_ptr<asio::steady_timer> timer;
            };
            auto race = std::make_shared<Race>();
            race->pending = targets.size();
            race->timer = std::make_shared<asio::steady_timer>(net.io(), kConnectTimeout);
            auto finishFail = [this, gen, race] {
                if (race->done) return;
                race->done = true;
                race->timer->cancel();
                for (auto& s : race->sockets) { std::error_code ec; s->close(ec); }
                events.push({Event::ConnectFailed, gen, {}, race->lastError.empty() ? "connect failed" : race->lastError});
            };
            race->timer->async_wait([race, finishFail](std::error_code ec) {
                if (ec) return;  // cancelled
                race->lastError = "connect: timed out";
                finishFail();
            });
            if (targets.empty()) { race->lastError = "no address"; finishFail(); return; }
            for (const auto& t : targets) {
                auto resolver = std::make_shared<tcp::resolver>(net.io());
                resolver->async_resolve(t.host, std::to_string(t.port), [this, gen, race, resolver, finishFail, relayRoom = t.relayRoom, target = t](std::error_code ec, tcp::resolver::results_type results) {
                    if (race->done) return;
                    if (ec) {
                        race->lastError = "resolve: " + ec.message();
                        if (--race->pending == 0) finishFail();
                        return;
                    }
                    auto socket = std::make_shared<tcp::socket>(net.io());
                    race->sockets.push_back(socket);
                    // The winner becomes the session connection.
                    auto win = [this, gen, race, socket, target] {
                        if (race->done) { std::error_code cec; socket->close(cec); return; }
                        race->done = true;
                        race->timer->cancel();
                        for (auto& other : race->sockets) {
                            if (other != socket) { std::error_code cec; other->close(cec); }
                        }
                        conn = std::make_shared<net::Connection>(
                            std::move(*socket), gen,
                            [this](std::uint32_t g, Frame f) { events.push({Event::FrameIn, g, std::move(f), {}}); },
                            [this](std::uint32_t g, std::string r) { events.push({Event::Closed, g, {}, std::move(r)}); });
                        conn->start();
                        events.push({Event::Connected, gen, {}, {}, target});
                    };
                    auto lose = [race, finishFail](std::string why) {
                        if (race->done) return;
                        race->lastError = std::move(why);
                        if (--race->pending == 0) finishFail();
                    };
                    asio::async_connect(*socket, results, [socket, race, win, lose, relayRoom](std::error_code ec2, const tcp::endpoint&) {
                        if (race->done) return;
                        if (ec2) { lose("connect: " + ec2.message()); return; }
                        if (relayRoom == 0) { win(); return; }
                        // Through the relay: ask for the room, wait until the host is on the line.
                        auto out = std::make_shared<std::vector<std::uint8_t>>(relay::encode(relay::Message{relay::Kind::Join, relayRoom}));
                        asio::async_write(*socket, asio::buffer(*out), [socket, race, win, lose, out](std::error_code wec, std::size_t) {
                            if (race->done) return;
                            if (wec) { lose("relay: " + wec.message()); return; }
                            auto hdr = std::make_shared<std::array<std::uint8_t, relay::kHeaderSize>>();
                            asio::async_read(*socket, asio::buffer(*hdr), [socket, race, win, lose, hdr](std::error_code rec, std::size_t) {
                                if (race->done) return;
                                if (rec) { lose("relay: " + rec.message()); return; }
                                auto kind = relay::decodeHeader(*hdr);
                                if (kind == relay::Kind::Paired) { win(); return; }
                                if (kind != relay::Kind::Error) { lose("relay: bad reply"); return; }
                                auto code = std::make_shared<std::array<std::uint8_t, 1>>();
                                asio::async_read(*socket, asio::buffer(*code), [race, lose, code](std::error_code cec, std::size_t) {
                                    if (race->done) return;
                                    auto msg = cec ? std::nullopt : relay::decodeBody(relay::Kind::Error, *code);
                                    lose(msg ? std::string("relay: ") + relay::toString(msg->error) : std::string("relay: closed"));
                                });
                            });
                        });
                    });
                });
            }
        });
    }

    bool hasJoinedBefore() const { return replica.has_value(); }

    // Connection lost or attempt failed: reconnect if we had joined, otherwise give up.
    void connectionLost(const std::string& reason) {
        attemptInFlight = false;
        if (state == ClientState::Refused || state == ClientState::Kicked || state == ClientState::RoomClosed ||
            state == ClientState::Disconnected) {
            return;
        }
        error = reason;
        // Connected and said hello, then the host hung up without a word: almost always an
        // older/newer NexLevel (the frame protocol differs, so it cannot even send a refusal).
        if (helloSent && !hasJoinedBefore() && !channel) error = "host closed the connection while joining (different NexLevel version?)";
        if (hasJoinedBefore() && reconnectAttempts < kMaxReconnectAttempts) {
            state = ClientState::Reconnecting;
            ++reconnectAttempts;
            nextRetry = Clock::now() + kRetryInterval;
            welcome.reset();
            return;
        }
        state = ClientState::Disconnected;
    }

    void finishSnapshot(const SnapshotEnd& end, std::vector<sync::NetId>& changed) {
        sync::Replica snapshot;
        snapshot.restore(std::move(incomingObjects), std::move(incomingTombstones), end.lastSeq);
        incomingObjects.clear();
        incomingTombstones.clear();

        const auto& w = *welcome;
        if (replica && w.resumed && w.peerId == replica->self()) {
            // Same identity: keep pending local edits the host has not applied yet, then resend them.
            auto c = replica->loadSnapshot(snapshot, w.lastClientSeq);
            changed.insert(changed.end(), c.begin(), c.end());
            for (const auto& op : replica->pending()) send(MsgType::Submit, encodeOpMsg(op));
        } else {
            // New identity (first join, or the host no longer knew us): start clean.
            std::vector<sync::NetId> oldIds;
            if (replica) {
                for (const auto& [id, _] : replica->view().objects()) oldIds.push_back(id);
                identityReset = true;
            }
            replica.emplace(w.peerId);
            ids.emplace(w.peerId);
            auto c = replica->loadSnapshot(snapshot, 0);
            changed.insert(changed.end(), c.begin(), c.end());
            changed.insert(changed.end(), oldIds.begin(), oldIds.end());
        }
        resumeToken = w.resumeToken;
        lastSentPresence.reset();  // announce our position again in the (new) room connection
        rtt.reset();
        welcome.reset();
        reconnectAttempts = 0;
        error.clear();
        state = ClientState::Joined;
    }
};

SessionClient::SessionClient(GameTarget target, std::string displayName)
    : m(std::make_unique<Impl>(target, std::move(displayName))) {}

SessionClient::~SessionClient() {
    if (!m->netStarted) return;
    std::promise<void> done;
    auto fut = done.get_future();
    m->net.post([this, &done] {
        if (m->conn) m->conn->close("client destroyed");
        m->conn.reset();
        done.set_value();
    });
    fut.wait_for(std::chrono::seconds(2));
    m->net.stop();
}

bool SessionClient::connectCode(std::string_view code) {
    auto addr = decodeRoomCode(code);
    if (!addr) return false;
    if (addr->relayRoom != 0) {
        auto it = std::find_if(m->relays.begin(), m->relays.end(), [&](auto& r) { return r.id == addr->relayId; });
        if (it == m->relays.end()) {
            m->error = addr->relayId == kCustomRelayId ? "no relay server configured" : "unknown relay " + std::to_string(addr->relayId) + " (update NexLevel)";
            return false;
        }
        connectTargets({ConnectTarget{it->host, it->port, addr->relayRoom}}, addr->key);
        return true;
    }
    std::vector<ConnectTarget> targets{{addr->ipString(), addr->port, 0}};
    if (addr->lanOctet != 0) {
        // Online code: also try the host's LAN address in case we are on the same network
        // (assumes the usual /24 home network).
        if (auto mine = net::primaryLocalIPv4()) {
            if (auto ip = parseIPv4(*mine)) {
                std::array<std::uint8_t, 4> lan{(*ip)[0], (*ip)[1], (*ip)[2], addr->lanOctet};
                RoomAddress l;
                l.ipv4 = lan;
                if (lan != addr->ipv4) targets.push_back({l.ipString(), addr->port, 0});
            }
        }
    }
    connectTargets(std::move(targets), addr->key);
    return true;
}

void SessionClient::connect(const std::string& host, std::uint16_t port, std::uint16_t roomKey) {
    connectTargets({ConnectTarget{host, port, 0}}, roomKey);
}

void SessionClient::setRelayDirectory(std::vector<relay::RelayEndpoint> relays) {
    std::erase_if(m->relays, [](auto& r) { return r.id != kCustomRelayId; });
    for (auto& r : relays) {
        if (r.id != kCustomRelayId) m->relays.push_back(std::move(r));
    }
}

void SessionClient::setRelayServer(const std::string& host, std::uint16_t port) {
    std::erase_if(m->relays, [](auto& r) { return r.id == kCustomRelayId; });
    if (!host.empty()) m->relays.push_back(relay::RelayEndpoint{kCustomRelayId, "custom", host, port});
}

void SessionClient::connectTargets(std::vector<ConnectTarget> targets, std::uint16_t roomKey) {
    m->targets = std::move(targets);
    m->roomKey = roomKey;
    m->state = ClientState::Connecting;
    m->error.clear();
    m->startAttempt();
}

void SessionClient::disconnect() {
    m->state = ClientState::Disconnected;
    if (m->netStarted) {
        m->net.post([this] { if (m->conn) m->conn->close("disconnected by user"); });
    }
}

void SessionClient::simulateNetworkDrop() {
    if (m->netStarted) {
        m->net.post([this] { if (m->conn) m->conn->close("simulated network drop"); });
    }
}

bool SessionClient::takeIdentityReset() {
    bool r = m->identityReset;
    m->identityReset = false;
    return r;
}

std::vector<sync::NetId> SessionClient::pump() {
    std::vector<sync::NetId> changed;

    if (m->state == ClientState::Reconnecting && !m->attemptInFlight && Clock::now() >= m->nextRetry) {
        m->startAttempt();
    }

    for (auto& e : m->events.drain()) {
        if (e.generation != m->generation) continue;  // from a replaced connection
        switch (e.kind) {
            case Impl::Event::Connected:
                if (m->state == ClientState::Connecting) m->state = ClientState::Joining;
                m->helloSent = true;
                m->via = e.via;
                m->keys = crypto::generateKeyPair();
                if (!m->keys) { m->error = "no secure random source"; disconnect(); break; }
                m->send(MsgType::Hello, encode(Hello{m->target, m->roomKey, m->resumeToken, m->name, m->keys->publicKey, m->accountId}));
                break;
            case Impl::Event::ConnectFailed:
            case Impl::Event::Closed:
                m->connectionLost(e.reason);
                break;
            case Impl::Event::FrameIn: {
                // Only the key exchange and a refusal travel in the clear; everything else must be sealed.
                if (m->channel) {
                    if (static_cast<MsgType>(e.frame.type) != MsgType::Sealed) { m->error = "unencrypted message from host"; disconnect(); break; }
                    auto inner = m->channel->open(e.frame.payload);
                    if (!inner) { m->error = "message from host failed authentication"; disconnect(); break; }
                    e.frame = std::move(*inner);
                    if (static_cast<MsgType>(e.frame.type) == MsgType::Secure) { m->error = "unexpected key exchange"; disconnect(); break; }
                } else if (static_cast<MsgType>(e.frame.type) == MsgType::Secure) {
                    auto sec = decodeSecure(e.frame.payload);
                    if (sec && m->keys) m->channel = crypto::SecureChannel::establish(crypto::Side::Client, *m->keys, sec->publicKey);
                    if (!m->channel) { m->error = "key exchange failed"; disconnect(); }
                    break;
                } else if (static_cast<MsgType>(e.frame.type) != MsgType::Refuse) {
                    m->error = "host did not encrypt the session";
                    disconnect();
                    break;
                }
                const auto type = static_cast<MsgType>(e.frame.type);
                const bool joining = m->state == ClientState::Joining || m->state == ClientState::Reconnecting;
                if (joining && type == MsgType::Welcome) {
                    m->welcome = decodeWelcome(e.frame.payload);
                    if (!m->welcome) { m->error = "malformed welcome"; disconnect(); break; }
                    m->incomingObjects.clear();
                    m->incomingTombstones.clear();
                } else if (joining && m->welcome && type == MsgType::SnapshotChunk) {
                    auto chunk = decodeSnapshotChunk(e.frame.payload);
                    if (!chunk) { m->error = "malformed snapshot"; disconnect(); break; }
                    for (auto& [id, props] : chunk->objects) m->incomingObjects[id] = sync::ObjectState{std::move(props)};
                    for (auto id : chunk->tombstones) m->incomingTombstones.insert(id);
                } else if (joining && m->welcome && type == MsgType::SnapshotEnd) {
                    auto end = decodeSnapshotEnd(e.frame.payload);
                    if (!end || end->objectCount != m->incomingObjects.size() || end->tombstoneCount != m->incomingTombstones.size()) {
                        m->error = "incomplete snapshot";
                        disconnect();
                        break;
                    }
                    m->finishSnapshot(*end, changed);
                } else if (type == MsgType::Refuse) {
                    auto r = decodeRefuse(e.frame.payload);
                    m->state = ClientState::Refused;
                    m->refuse = r ? std::optional(r->reason) : std::nullopt;
                    m->hostTarget = r ? std::optional(r->hostTarget) : std::nullopt;
                } else if (type == MsgType::Roster) {
                    if (auto r = decodeRoster(e.frame.payload)) {
                        m->peers = std::move(r->peers);
                        // Forget where players who left were (and their playtest trails).
                        for (auto it = m->presences.begin(); it != m->presences.end();) {
                            bool present = false;
                            for (auto& peer : m->peers) present = present || peer.id == it->first;
                            it = present ? std::next(it) : m->presences.erase(it);
                        }
                        for (auto it = m->trails.begin(); it != m->trails.end();) {
                            bool present = false;
                            for (auto& peer : m->peers) present = present || peer.id == it->first;
                            it = present ? std::next(it) : m->trails.erase(it);
                        }
                    }
                } else if (type == MsgType::PlayerState) {
                    if (auto st = decodePlayerState(e.frame.payload)) {
                        if (!m->replica || st->peer != m->replica->self()) recordPlayerState(m->trails, *st);
                    }
                } else if (type == MsgType::Ping) {
                    if (auto ping = decodePing(e.frame.payload)) m->send(MsgType::Pong, encode(answer(*ping, e.at)));
                } else if (type == MsgType::Pong) {
                    if (auto pong = decodePong(e.frame.payload)) m->rtt.onPong(*pong, e.at);
                } else if (type == MsgType::PingTable) {
                    if (auto t = decodePingTable(e.frame.payload)) {
                        m->pingTable.clear();
                        for (auto& [peer, ms] : t->pings) m->pingTable[peer] = ms;
                    }
                } else if (type == MsgType::Presence) {
                    if (auto pr = decodePresence(e.frame.payload)) {
                        if (!m->replica || pr->peer != m->replica->self()) m->presences[pr->peer] = *pr;
                    }
                } else if (type == MsgType::Kicked) {
                    m->state = ClientState::Kicked;
                    m->peers.clear();
                } else if (type == MsgType::Closing) {
                    m->state = ClientState::RoomClosed;
                    m->peers.clear();
                } else if (m->state == ClientState::Joined && type == MsgType::Sequenced) {
                    auto op = decodeOpMsg(e.frame.payload);
                    if (!op) { m->error = "malformed op from host"; disconnect(); break; }
                    auto c = m->replica->onSequenced(*op);
                    changed.insert(changed.end(), c.begin(), c.end());
                } else if (m->state == ClientState::Joined && type == MsgType::Rejected) {
                    if (auto r = decodeRejected(e.frame.payload)) {
                        auto c = m->replica->onRejected(r->clientSeq);
                        changed.insert(changed.end(), c.begin(), c.end());
                    }
                }
                break;
            }
        }
    }
    if (m->state == ClientState::Joined) {
        if (auto ping = m->rtt.due(Clock::now())) m->send(MsgType::Ping, encode(*ping));
    }
    return changed;
}

std::optional<sync::Op> SessionClient::submitLocal(sync::Op op) {
    if (!m->replica || (m->state != ClientState::Joined && m->state != ClientState::Reconnecting)) return std::nullopt;
    auto sent = m->replica->local(std::move(op));
    if (m->state == ClientState::Joined) m->send(MsgType::Submit, encodeOpMsg(sent));
    return sent;  // while reconnecting it stays pending and is resent after the snapshot
}

void SessionClient::setAccountId(std::uint32_t accountId) { m->accountId = accountId; }
PingStats SessionClient::pingStats() const { return m->rtt.stats(); }
std::map<sync::PeerId, std::uint16_t> SessionClient::pingTable() const { return m->pingTable; }
std::optional<ConnectTarget> SessionClient::connectedVia() const { return m->via; }

void SessionClient::setLocalPlayerState(PlayerState st) {
    if (m->state != ClientState::Joined || !validPlayerState(st)) return;
    const auto now = Clock::now();
    if (st.flags == m->lastPlayerFlags && now - m->lastPlayerStateSent < std::chrono::milliseconds(33)) return;
    m->lastPlayerStateSent = now;
    m->lastPlayerFlags = st.flags;
    st.peer = 0;
    m->send(MsgType::PlayerState, encode(st));
}

const std::map<sync::PeerId, PlayTrail>& SessionClient::playTrails() const noexcept { return m->trails; }

void SessionClient::setLocalPresence(float x, float y, float zoom) {
    if (m->state != ClientState::Joined) return;
    Presence pr{0, x, y, zoom};
    if (!validPresence(pr) || (m->lastSentPresence && *m->lastSentPresence == pr)) return;
    const auto now = Clock::now();
    if (now - m->lastPresenceSent < std::chrono::milliseconds(100)) return;
    m->lastPresenceSent = now;
    m->lastSentPresence = pr;
    m->send(MsgType::PresenceUpdate, encode(pr));
}

std::vector<Presence> SessionClient::presences() const {
    std::vector<Presence> out;
    for (auto& [_, p] : m->presences) out.push_back(p);
    return out;
}

std::optional<sync::NetId> SessionClient::newId() {
    if (!m->ids) return std::nullopt;
    return m->ids->next();
}

ClientState SessionClient::state() const noexcept { return m->state; }
const std::string& SessionClient::lastError() const noexcept { return m->error; }
std::optional<RefuseReason> SessionClient::refuseReason() const noexcept { return m->refuse; }
std::optional<GameTarget> SessionClient::hostTarget() const noexcept { return m->hostTarget; }
const std::vector<PeerInfo>& SessionClient::peers() const noexcept { return m->peers; }
std::optional<sync::PeerId> SessionClient::peerId() const noexcept {
    if (!m->replica) return std::nullopt;
    return m->replica->self();
}
const sync::ClientReplica* SessionClient::replica() const noexcept { return m->replica ? &*m->replica : nullptr; }

} // namespace nexlevel::session

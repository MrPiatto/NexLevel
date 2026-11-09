#include <nexlevel/core/session/host.hpp>

#include "../net/connection.hpp"

#include <nexlevel/core/crypto/secure_channel.hpp>
#include <nexlevel/core/relay/protocol.hpp>
#include <nexlevel/core/session/playtest.hpp>
#include <nexlevel/core/session/room_code.hpp>

#include <algorithm>
#include <atomic>

#include <chrono>
#include <future>
#include <map>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <system_error>

namespace nexlevel::session {

using asio::ip::tcp;
using Clock = std::chrono::steady_clock;

namespace {
constexpr auto kResumeWindow = std::chrono::seconds(120);
constexpr double kOpBurst = 60000.0;       // ops a player may send at once (huge paste)
constexpr double kOpRefillPerSec = 10000.0;  // how long a dropped player can reclaim their identity
}

struct SessionHost::Impl {
    struct Event {
        enum Kind { Connected, FrameIn, Closed } kind;
        std::uint32_t conn = 0;
        Frame frame;
        std::string reason;
        Clock::time_point at = Clock::now();  // when it reached the network thread (ping timing)
    };
    struct Peer {
        sync::PeerId id = 0;
        std::string name;
        std::uint32_t accountId = 0;
        RttTracker rtt;
        std::uint64_t token = 0;
        bool joined = false;
        std::optional<crypto::SecureChannel> channel;  // set once the key exchange is done
        std::optional<Presence> presence;
        Clock::time_point lastPresence{};
        Clock::time_point lastPlayerState{};
        std::uint8_t lastPlayerFlags = 0;
        // Edit flood protection (token bucket): big pastes fit, a script spamming the room does not.
        double opTokens = kOpBurst;
        Clock::time_point lastPingAnswered{};
        Clock::time_point opRefill = Clock::now();
    };
    struct Identity {  // survives the connection, for reconnects
        sync::PeerId id = 0;
        std::string name;
        bool connected = false;
        Clock::time_point leftAt{};
    };

    GameTarget target;
    std::string hostName;
    net::NetThread net;
    std::optional<tcp::acceptor> acceptor;
    std::uint16_t port = 0;
    std::uint16_t roomKey = 0;
    bool running = false;

    // Network thread only.
    std::unordered_map<std::uint32_t, std::shared_ptr<net::Connection>> conns;
    std::uint32_t nextConnId = 1;
    net::EventQueue<Event> events;

    // Main thread only.
    sync::HostSequencer sequencer;
    sync::NetIdAllocator ids{kHostPeer};
    std::uint32_t hostClientSeq = 1;
    std::map<std::uint32_t, Peer> peers;
    std::unordered_map<std::uint64_t, Identity> identities;  // by resume token
    sync::PeerId nextPeer = kHostPeer + 1;

    Impl(GameTarget t, std::string n) : target(t), hostName(std::move(n)) {}

    void acceptNext() {
        acceptor->async_accept([this](std::error_code ec, tcp::socket socket) {
            if (ec) {
                if (ec != asio::error::operation_aborted && acceptor && acceptor->is_open()) acceptNext();
                return;
            }
            adopt(std::move(socket));
            acceptNext();
        });
    }

    // A player connection, accepted locally or opened through the relay. Network thread.
    void adopt(tcp::socket socket) {
        auto id = nextConnId++;
        auto c = std::make_shared<net::Connection>(
            std::move(socket), id,
            [this](std::uint32_t i, Frame f) { events.push({Event::FrameIn, i, std::move(f), {}}); },
            [this](std::uint32_t i, std::string r) {
                conns.erase(i);
                events.push({Event::Closed, i, {}, std::move(r)});
            });
        conns.emplace(id, c);
        events.push({Event::Connected, id, {}, {}});
        c->start();
    }

    // ---- relay link (network thread, except the atomics/err read by the main thread) ----
    std::string relayHost;
    std::uint16_t relayPort = 0;
    bool relayEnabled = false;
    std::uint32_t relayGen = 0;
    std::uint32_t reclaimId = 0;
    std::uint64_t reclaimSecret = 0;
    std::shared_ptr<tcp::socket> relaySock;
    std::optional<asio::steady_timer> relayRetry;
    std::optional<asio::steady_timer> relayPing;
    // Host's own round trip to the relay (keepalive pings on the control connection, every second).
    mutable std::mutex relayRttMutex;
    RttTracker relayRtt;
    std::deque<std::uint32_t> relayPingSeqs;
    std::atomic<RelayState> relayStateA{RelayState::Off};
    std::atomic<std::uint32_t> relayRoomA{0};
    std::uint8_t relayIdValue = 0;  // main thread
    mutable std::mutex relayErrMutex;
    std::string relayErr;

    void setRelayError(std::string e) {
        std::lock_guard lk(relayErrMutex);
        relayErr = std::move(e);
    }

    void relayConnect() {
        if (!relayEnabled) return;
        const auto gen = ++relayGen;
        auto resolver = std::make_shared<tcp::resolver>(net.io());
        resolver->async_resolve(relayHost, std::to_string(relayPort), [this, gen, resolver](std::error_code ec, tcp::resolver::results_type results) {
            if (gen != relayGen) return;
            if (ec) { relayLost("relay: " + ec.message()); return; }
            auto sock = std::make_shared<tcp::socket>(net.io());
            asio::async_connect(*sock, results, [this, gen, sock](std::error_code cec, const tcp::endpoint&) {
                if (gen != relayGen) return;
                if (cec) { relayLost("relay: " + cec.message()); return; }
                relaySock = sock;
                auto out = std::make_shared<std::vector<std::uint8_t>>(relay::encode(relay::Message{relay::Kind::OpenRoom, reclaimId, reclaimSecret}));
                asio::async_write(*sock, asio::buffer(*out), [this, gen, sock, out](std::error_code wec, std::size_t) {
                    if (gen != relayGen) return;
                    if (wec) { relayLost("relay: " + wec.message()); return; }
                    relayRead(gen, sock);
                });
            });
        });
    }

    void relayRead(std::uint32_t gen, std::shared_ptr<tcp::socket> sock) {
        auto hdr = std::make_shared<std::array<std::uint8_t, relay::kHeaderSize>>();
        asio::async_read(*sock, asio::buffer(*hdr), [this, gen, sock, hdr](std::error_code ec, std::size_t) {
            if (gen != relayGen) return;
            if (ec) { relayLost("relay connection lost"); return; }
            auto kind = relay::decodeHeader(*hdr);
            if (!kind) { relayLost("bad relay reply"); return; }
            auto body = std::make_shared<std::vector<std::uint8_t>>(*relay::bodySize(*kind));
            auto handle = [this, gen, sock, kind, body] {
                auto msg = relay::decodeBody(*kind, *body);
                if (!msg) { relayLost("bad relay reply"); return; }
                switch (msg->kind) {
                    case relay::Kind::RoomOpened:
                        reclaimId = msg->roomId;
                        reclaimSecret = msg->secret;
                        relayRoomA = msg->roomId;
                        relayStateA = RelayState::Ready;
                        setRelayError({});
                        schedulePing(gen, sock);
                        break;
                    case relay::Kind::Incoming: openRelayData(msg->stream); break;
                    case relay::Kind::Pong: {
                        std::lock_guard lk(relayRttMutex);
                        if (!relayPingSeqs.empty()) {
                            const auto seq = relayPingSeqs.front();
                            relayPingSeqs.pop_front();
                            relayRtt.onPong(Pong{seq, 0, 0}, std::chrono::steady_clock::now());
                        }
                        break;
                    }
                    case relay::Kind::Error: relayLost(std::string("relay: ") + relay::toString(msg->error)); return;
                    default: relayLost("unexpected relay message"); return;
                }
                relayRead(gen, sock);
            };
            if (body->empty()) { handle(); return; }
            asio::async_read(*sock, asio::buffer(*body), [this, gen, handle](std::error_code bec, std::size_t) {
                if (gen != relayGen) return;
                if (bec) { relayLost("relay connection lost"); return; }
                handle();
            });
        });
    }

    void schedulePing(std::uint32_t gen, std::shared_ptr<tcp::socket> sock) {
        relayPing.emplace(net.io(), std::chrono::milliseconds(1000));
        relayPing->async_wait([this, gen, sock](std::error_code ec) {
            if (ec || gen != relayGen) return;
            {
                std::lock_guard lk(relayRttMutex);
                // Every relay Ping gets one Pong, in order: remember which measurement it belongs to
                // (0 = not measured, when the timer fired a little early).
                auto ping = relayRtt.due(std::chrono::steady_clock::now());
                relayPingSeqs.push_back(ping ? ping->seq : 0);
                while (relayPingSeqs.size() > 16) relayPingSeqs.pop_front();
            }
            auto out = std::make_shared<std::vector<std::uint8_t>>(relay::encode(relay::Message{relay::Kind::Ping}));
            asio::async_write(*sock, asio::buffer(*out), [this, gen, sock, out](std::error_code wec, std::size_t) {
                if (gen != relayGen) return;
                if (wec) { relayLost("relay connection lost"); return; }
                schedulePing(gen, sock);
            });
        });
    }

    void openRelayData(std::uint32_t stream) {
        auto resolver = std::make_shared<tcp::resolver>(net.io());
        const auto roomId = reclaimId;
        const auto secret = reclaimSecret;
        resolver->async_resolve(relayHost, std::to_string(relayPort), [this, resolver, roomId, secret, stream](std::error_code ec, tcp::resolver::results_type results) {
            if (ec || !relayEnabled) return;
            auto sock = std::make_shared<tcp::socket>(net.io());
            asio::async_connect(*sock, results, [this, sock, roomId, secret, stream](std::error_code cec, const tcp::endpoint&) {
                if (cec || !relayEnabled) return;
                auto out = std::make_shared<std::vector<std::uint8_t>>(relay::encode(relay::Message{relay::Kind::Accept, roomId, secret, stream}));
                asio::async_write(*sock, asio::buffer(*out), [this, sock, out](std::error_code wec, std::size_t) {
                    if (wec || !relayEnabled) return;
                    adopt(std::move(*sock));
                });
            });
        });
    }

    void relayLost(std::string reason) {
        ++relayGen;  // ignore callbacks from the old connection
        if (relaySock) { std::error_code ec; relaySock->close(ec); relaySock.reset(); }
        if (relayPing) relayPing->cancel();
        {
            std::lock_guard lk(relayRttMutex);
            relayPingSeqs.clear();  // pongs of the old connection will never come
        }
        setRelayError(std::move(reason));
        if (!relayEnabled) return;
        relayStateA = relayRoomA.load() != 0 ? RelayState::Reconnecting : RelayState::Connecting;
        relayRetry.emplace(net.io(), std::chrono::seconds(3));
        relayRetry->async_wait([this](std::error_code ec) {
            if (!ec) relayConnect();
        });
    }

    void relayShutdown() {
        relayEnabled = false;
        ++relayGen;
        if (relaySock) { std::error_code ec; relaySock->close(ec); relaySock.reset(); }
        if (relayPing) relayPing->cancel();
        {
            std::lock_guard lk(relayRttMutex);
            relayPingSeqs.clear();  // pongs of the old connection will never come
        }
        if (relayRetry) relayRetry->cancel();
        relayStateA = RelayState::Off;
    }

    void sendTo(std::uint32_t conn, MsgType type, std::vector<std::uint8_t> payload, bool closeAfter = false) {
        std::vector<std::uint8_t> bytes;
        auto pit = peers.find(conn);
        if (pit != peers.end() && pit->second.channel) {
            bytes = encodeFrame(static_cast<std::uint16_t>(MsgType::Sealed),
                                pit->second.channel->seal(static_cast<std::uint16_t>(type), payload));
        } else {
            bytes = encodeFrame(static_cast<std::uint16_t>(type), payload);
        }
        net.post([this, conn, closeAfter, bytes = std::move(bytes)]() mutable {
            auto it = conns.find(conn);
            if (it == conns.end()) return;
            if (closeAfter) it->second->sendAndClose(std::move(bytes), "closed by host");
            else it->second->send(std::move(bytes));
        });
    }
    void closeConn(std::uint32_t conn, std::string reason) {
        net.post([this, conn, reason = std::move(reason)]() mutable {
            auto it = conns.find(conn);
            if (it != conns.end()) it->second->close(std::move(reason));
        });
    }
    void broadcast(const sync::Op& op) {
        auto payload = encodeOpMsg(op);
        for (auto& [conn, p] : peers) {
            if (p.joined) sendTo(conn, MsgType::Sequenced, payload);
        }
    }
    std::vector<PeerInfo> roster() const {
        std::vector<PeerInfo> out{{kHostPeer, hostName, hostAccountId}};
        for (auto& [_, p] : peers) {
            if (p.joined) out.push_back({p.id, p.name, p.accountId});
        }
        return out;
    }
    std::uint32_t hostAccountId = 0;
    Clock::time_point lastPingTable{};

    void pingPeers() {
        const auto now = Clock::now();
        for (auto& [conn, p] : peers) {
            if (!p.joined) continue;
            if (auto ping = p.rtt.due(now)) sendTo(conn, MsgType::Ping, encode(*ping));
        }
        if (now - lastPingTable < std::chrono::seconds(2)) return;
        lastPingTable = now;
        PingTable table;
        for (auto& [conn, p] : peers) {
            if (!p.joined) continue;
            if (auto ms = p.rtt.stats().ms) table.pings.emplace_back(p.id, static_cast<std::uint16_t>(std::min(*ms + 0.5f, 65535.f)));
        }
        if (table.pings.empty()) return;
        auto payload = encode(table);
        for (auto& [conn, p] : peers) {
            if (p.joined) sendTo(conn, MsgType::PingTable, payload);
        }
    }

    std::map<sync::PeerId, PlayTrail> trails;  // other players' playtests (host is not stored)
    Clock::time_point lastHostPlayerState{};
    std::uint8_t lastHostPlayerFlags = 0;

    void broadcastPlayerState(const PlayerState& st, std::uint32_t exceptConn) {
        auto payload = encode(st);
        for (auto& [conn, p] : peers) {
            if (p.joined && conn != exceptConn) sendTo(conn, MsgType::PlayerState, payload);
        }
    }

    std::optional<Presence> hostPresence;
    Clock::time_point lastHostPresence{};

    void broadcastPresence(const Presence& pr, std::uint32_t exceptConn) {
        auto payload = encode(pr);
        for (auto& [conn, p] : peers) {
            if (p.joined && conn != exceptConn) sendTo(conn, MsgType::Presence, payload);
        }
    }

    void broadcastRoster() {
        auto payload = encode(Roster{roster()});
        for (auto& [conn, p] : peers) {
            if (p.joined) sendTo(conn, MsgType::Roster, payload);
        }
    }
    std::size_t joinedCount() const {
        std::size_t n = 0;
        for (auto& [_, p] : peers) n += p.joined ? 1 : 0;
        return n;
    }

    // Welcome + the whole level in chunks + end marker. Runs on the main thread, so the snapshot is
    // consistent: ops sequenced later are queued behind it on the same ordered connection.
    void sendJoinPackage(std::uint32_t conn, const Peer& p, bool resumed) {
        sendTo(conn, MsgType::Welcome, encode(Welcome{p.id, p.token, sequencer.highestClientSeq(p.id), resumed}));
        const auto& state = sequencer.state();
        for (auto& chunk : encodeSnapshotChunksZ(state)) sendTo(conn, MsgType::SnapshotChunkZ, std::move(chunk));
        sendTo(conn, MsgType::SnapshotEnd, encode(SnapshotEnd{state.lastSeq(), static_cast<std::uint32_t>(state.objects().size()),
                                                              static_cast<std::uint32_t>(state.tombstones().size())}));
    }

    void handleHello(std::uint32_t conn, Peer& p, const Frame& f) {
        auto hello = static_cast<MsgType>(f.type) == MsgType::Hello ? decodeHello(f.payload) : std::nullopt;
        if (!hello) {
            sendTo(conn, MsgType::Refuse, encode(Refuse{RefuseReason::BadHello, target}), true);
            return;
        }
        if (hello->roomKey != roomKey) {
            sendTo(conn, MsgType::Refuse, encode(Refuse{RefuseReason::WrongRoomCode, target}), true);
            return;
        }
        if (!canShareSession(hello->target, target)) {  // V1: same GD version only
            sendTo(conn, MsgType::Refuse, encode(Refuse{RefuseReason::WrongGameVersion, target}), true);
            return;
        }
        bool resumed = false;
        if (hello->resumeToken != 0) {
            auto it = identities.find(hello->resumeToken);
            if (it != identities.end() && !it->second.connected) {
                p.id = it->second.id;
                p.name = it->second.name;
                p.token = hello->resumeToken;
                it->second.connected = true;
                resumed = true;
            }
        }
        if (!resumed) {
            if (joinedCount() >= kMaxPeers) {
                sendTo(conn, MsgType::Refuse, encode(Refuse{RefuseReason::SessionFull, target}), true);
                return;
            }
            p.id = nextPeer++;
            p.name = hello->displayName;
            do { p.token = crypto::secureRandomU64(); } while (p.token == 0 || identities.contains(p.token));
            identities[p.token] = Identity{p.id, p.name, true, {}};
        }
        p.accountId = hello->accountId;
        // Key exchange: our key goes out in the clear, everything after it is encrypted.
        auto keys = crypto::generateKeyPair();
        auto channel = keys ? crypto::SecureChannel::establish(crypto::Side::Host, *keys, hello->publicKey) : std::nullopt;
        if (!channel) {
            if (resumed) identities[p.token].connected = false;
            else identities.erase(p.token);
            sendTo(conn, MsgType::Refuse, encode(Refuse{RefuseReason::BadHello, target}), true);
            return;
        }
        sendTo(conn, MsgType::Secure, encode(Secure{keys->publicKey}));
        p.channel = std::move(channel);
        p.joined = true;
        sendJoinPackage(conn, p, resumed);
        broadcastRoster();
        // Where everyone already is.
        if (hostPresence) sendTo(conn, MsgType::Presence, encode(*hostPresence));
        for (auto& [c, other] : peers) {
            if (c != conn && other.joined && other.presence) sendTo(conn, MsgType::Presence, encode(*other.presence));
        }
    }

    void handleFrame(std::uint32_t conn, Peer& p, const Frame& f, Clock::time_point arrival, std::vector<sync::NetId>& changed) {
        if (!p.joined) {
            handleHello(conn, p, f);
            return;
        }
        if (static_cast<MsgType>(f.type) != MsgType::Sealed || !p.channel) {
            closeConn(conn, "unencrypted message");
            return;
        }
        auto inner = p.channel->open(f.payload);
        if (!inner) {
            closeConn(conn, "message failed authentication");
            return;
        }
        handleSecureFrame(conn, p, *inner, arrival, changed);
    }

    void handleSecureFrame(std::uint32_t conn, Peer& p, const Frame& f, Clock::time_point arrival, std::vector<sync::NetId>& changed) {
        if (static_cast<MsgType>(f.type) == MsgType::PlayerState) {
            auto st = decodePlayerState(f.payload);
            if (!st) { closeConn(conn, "malformed player state"); return; }
            const auto now = Clock::now();
            // Rate limit, but never drop a start/stop of a test.
            if (st->flags == p.lastPlayerFlags && now - p.lastPlayerState < std::chrono::milliseconds(25)) return;
            p.lastPlayerState = now;
            p.lastPlayerFlags = st->flags;
            st->peer = p.id;
            recordPlayerState(trails, *st);
            broadcastPlayerState(*st, conn);
            return;
        }
        if (static_cast<MsgType>(f.type) == MsgType::Ping) {
            // Clients ping once a second; answering at most 5/s keeps a spammer from using us as an amplifier.
            if (arrival - p.lastPingAnswered < std::chrono::milliseconds(200)) return;
            p.lastPingAnswered = arrival;
            if (auto ping = decodePing(f.payload)) sendTo(conn, MsgType::Pong, encode(answer(*ping, arrival)));
            return;
        }
        if (static_cast<MsgType>(f.type) == MsgType::Pong) {
            if (auto pong = decodePong(f.payload)) p.rtt.onPong(*pong, arrival);
            return;
        }
        if (static_cast<MsgType>(f.type) == MsgType::PresenceUpdate) {
            auto pr = decodePresence(f.payload);
            if (!pr) { closeConn(conn, "malformed presence"); return; }
            const auto now = Clock::now();
            if (now - p.lastPresence < std::chrono::milliseconds(50)) return;  // rate limit, drop extras
            p.lastPresence = now;
            pr->peer = p.id;  // identity comes from the connection
            p.presence = pr;
            broadcastPresence(*pr, conn);
            return;
        }
        if (static_cast<MsgType>(f.type) == MsgType::Submit) {
            const auto now = Clock::now();
            p.opTokens = std::min(kOpBurst, p.opTokens + std::chrono::duration<double>(now - p.opRefill).count() * kOpRefillPerSec);
            p.opRefill = now;
            if (p.opTokens < 1.0) {
                closeConn(conn, "too many edits (flood protection)");
                return;
            }
            p.opTokens -= 1.0;
        }
        if (static_cast<MsgType>(f.type) != MsgType::Submit) {
            closeConn(conn, "unexpected message");
            return;
        }
        auto op = decodeOpMsg(f.payload);
        if (!op) {
            closeConn(conn, "malformed op");
            return;
        }
        const auto clientSeq = op->clientSeq;
        op->author = p.id;  // identity comes from the connection, never from the payload
        auto res = sequencer.submit(std::move(*op));
        if (res.sequenced) {
            changed.push_back(res.sequenced->target);
            broadcast(*res.sequenced);
        } else if (res.reason == sync::HostSequencer::Reject::ForgedId) {
            sendTo(conn, MsgType::Rejected, encode(Rejected{clientSeq, 1}));
        }
        // Duplicates (resends after a reconnect) are dropped: the original was already sequenced.
    }

    void expireIdentities() {
        auto now = Clock::now();
        for (auto it = identities.begin(); it != identities.end();) {
            if (!it->second.connected && now - it->second.leftAt > kResumeWindow) it = identities.erase(it);
            else ++it;
        }
    }
};

SessionHost::SessionHost(GameTarget target, std::string hostName) : m(std::make_unique<Impl>(target, std::move(hostName))) {}
SessionHost::~SessionHost() { stop(); }

std::optional<std::uint16_t> SessionHost::start(std::uint16_t port, bool loopbackOnly, std::string* error) {
    if (m->running) return m->port;
    std::error_code ec;
    tcp::endpoint ep(loopbackOnly ? asio::ip::address(asio::ip::address_v4::loopback())
                                  : asio::ip::address(asio::ip::address_v4::any()), port);
    m->acceptor.emplace(m->net.io());
    m->acceptor->open(ep.protocol(), ec);
    if (!ec) m->acceptor->set_option(tcp::acceptor::reuse_address(true), ec);
    if (!ec) m->acceptor->bind(ep, ec);
    if (!ec) m->acceptor->listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        if (error) *error = ec.message();
        m->acceptor.reset();
        return std::nullopt;
    }
    m->port = m->acceptor->local_endpoint(ec).port();
    m->roomKey = static_cast<std::uint16_t>(crypto::secureRandomU64() & kRoomKeyMask);
    m->acceptNext();
    m->net.start();
    m->running = true;
    return m->port;
}

void SessionHost::stop() {
    if (!m || !m->running) return;
    m->running = false;
    // Tell everyone the room is closing, give the notices a moment to flush.
    for (auto& [conn, p] : m->peers) {
        if (p.joined) m->sendTo(conn, MsgType::Closing, {}, true);
    }
    if (!m->peers.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::promise<void> done;
    auto fut = done.get_future();
    m->net.post([this, &done] {
        std::error_code ec;
        if (m->acceptor) m->acceptor->close(ec);
        m->relayShutdown();
        auto all = m->conns;
        for (auto& [_, c] : all) c->close("host stopped");
        m->conns.clear();
        done.set_value();
    });
    fut.wait_for(std::chrono::seconds(2));
    m->net.stop();
}

void SessionHost::startRelay(const std::string& relayHost, std::uint16_t relayPort, std::uint8_t relayId) {
    if (!m->running) return;
    m->relayIdValue = relayId;
    m->relayStateA = RelayState::Connecting;
    m->net.post([this, relayHost, relayPort] {
        m->relayHost = relayHost;
        m->relayPort = relayPort;
        m->relayEnabled = true;
        m->relayConnect();
    });
}
std::uint8_t SessionHost::relayId() const noexcept { return m->relayIdValue; }
RelayState SessionHost::relayState() const noexcept { return m->relayStateA.load(); }
std::uint32_t SessionHost::relayRoomId() const noexcept { return m->relayRoomA.load(); }
std::string SessionHost::relayError() const {
    std::lock_guard lk(m->relayErrMutex);
    return m->relayErr;
}
void SessionHost::simulateRelayDrop() {
    m->net.post([this] {
        if (m->relaySock) { std::error_code ec; m->relaySock->shutdown(tcp::socket::shutdown_both, ec); }
    });
}

bool SessionHost::running() const noexcept { return m->running; }
std::uint16_t SessionHost::port() const noexcept { return m->port; }
std::uint16_t SessionHost::roomKey() const noexcept { return m->roomKey; }

std::vector<sync::NetId> SessionHost::pump() {
    std::vector<sync::NetId> changed;
    for (auto& e : m->events.drain()) {
        switch (e.kind) {
            case Impl::Event::Connected: m->peers[e.conn] = {}; break;
            case Impl::Event::Closed: {
                auto it = m->peers.find(e.conn);
                if (it == m->peers.end()) break;
                bool wasJoined = it->second.joined;
                if (wasJoined) {
                    auto id = m->identities.find(it->second.token);
                    if (id != m->identities.end()) {
                        id->second.connected = false;
                        id->second.leftAt = Clock::now();
                    }
                }
                m->trails.erase(it->second.id);
                m->peers.erase(it);
                if (wasJoined) m->broadcastRoster();
                break;
            }
            case Impl::Event::FrameIn: {
                auto it = m->peers.find(e.conn);
                if (it != m->peers.end()) m->handleFrame(e.conn, it->second, e.frame, e.at, changed);
                break;
            }
        }
    }
    m->expireIdentities();
    m->pingPeers();
    return changed;
}

sync::Op SessionHost::submitLocal(sync::Op op) {
    op.author = kHostPeer;
    op.clientSeq = m->hostClientSeq++;
    auto res = m->sequencer.submit(std::move(op));
    // The host's own ops cannot be duplicates or forged (ids come from newId()).
    m->broadcast(*res.sequenced);
    return *res.sequenced;
}

sync::NetId SessionHost::newId() { return m->ids.next(); }
const sync::Replica& SessionHost::state() const noexcept { return m->sequencer.state(); }
std::vector<PeerInfo> SessionHost::peers() const { return m->roster(); }

void SessionHost::setAccountId(std::uint32_t accountId) { m->hostAccountId = accountId; }

std::optional<PingStats> SessionHost::relayPingStats() const {
    if (m->relayStateA.load() != RelayState::Ready) return std::nullopt;
    std::lock_guard lk(m->relayRttMutex);
    return m->relayRtt.stats();
}

std::optional<PingStats> SessionHost::pingStats(sync::PeerId peer) const {
    for (auto& [_, p] : m->peers) {
        if (p.joined && p.id == peer) return p.rtt.stats();
    }
    return std::nullopt;
}

void SessionHost::setLocalPlayerState(PlayerState st) {
    st.peer = kHostPeer;
    if (!validPlayerState(st)) return;
    const auto now = Clock::now();
    if (st.flags == m->lastHostPlayerFlags && now - m->lastHostPlayerState < std::chrono::milliseconds(33)) return;
    m->lastHostPlayerState = now;
    m->lastHostPlayerFlags = st.flags;
    m->broadcastPlayerState(st, 0);
}

const std::map<sync::PeerId, PlayTrail>& SessionHost::playTrails() const noexcept { return m->trails; }

void SessionHost::setLocalPresence(float x, float y, float zoom) {
    Presence pr{kHostPeer, x, y, zoom};
    if (!validPresence(pr) || (m->hostPresence && *m->hostPresence == pr)) return;
    const auto now = Clock::now();
    if (now - m->lastHostPresence < std::chrono::milliseconds(100)) return;
    m->lastHostPresence = now;
    m->hostPresence = pr;
    m->broadcastPresence(pr, 0);
}

std::vector<Presence> SessionHost::presences() const {
    std::vector<Presence> out;
    for (auto& [_, p] : m->peers) {
        if (p.joined && p.presence) out.push_back(*p.presence);
    }
    return out;
}

bool SessionHost::kick(sync::PeerId peer) {
    for (auto it = m->peers.begin(); it != m->peers.end(); ++it) {
        if (it->second.joined && it->second.id == peer) {
            m->identities.erase(it->second.token);  // a kicked player cannot "reconnect"
            m->sendTo(it->first, MsgType::Kicked, {}, true);
            m->trails.erase(it->second.id);
            m->peers.erase(it);
            m->broadcastRoster();
            return true;
        }
    }
    return false;
}

} // namespace nexlevel::session

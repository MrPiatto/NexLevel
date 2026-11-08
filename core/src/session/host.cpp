#include <nexlevel/core/session/host.hpp>

#include "../net/connection.hpp"

#include <nexlevel/core/session/room_code.hpp>

#include <random>

#include <chrono>
#include <future>
#include <thread>
#include <map>
#include <unordered_map>

namespace nexlevel::session {

using asio::ip::tcp;

struct SessionHost::Impl {
    struct Event {
        enum Kind { Connected, FrameIn, Closed } kind;
        std::uint32_t conn = 0;
        Frame frame;
        std::string reason;
    };
    struct Peer {
        sync::PeerId id = 0;
        std::string name;
        bool joined = false;
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
    sync::PeerId nextPeer = kHostPeer + 1;

    Impl(GameTarget t, std::string n) : target(t), hostName(std::move(n)) {}

    void acceptNext() {
        acceptor->async_accept([this](std::error_code ec, tcp::socket socket) {
            if (ec) {
                if (ec != asio::error::operation_aborted && acceptor && acceptor->is_open()) acceptNext();
                return;
            }
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
            acceptNext();
        });
    }

    void sendTo(std::uint32_t conn, MsgType type, std::vector<std::uint8_t> payload, bool closeAfter = false) {
        auto bytes = encodeFrame(static_cast<std::uint16_t>(type), payload);
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
        std::vector<PeerInfo> out{{kHostPeer, hostName}};
        for (auto& [_, p] : peers) {
            if (p.joined) out.push_back({p.id, p.name});
        }
        return out;
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

    void handleFrame(std::uint32_t conn, Peer& p, const Frame& f, std::vector<sync::NetId>& changed) {
        const auto type = static_cast<MsgType>(f.type);
        if (!p.joined) {
            auto hello = type == MsgType::Hello ? decodeHello(f.payload) : std::nullopt;
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
            if (joinedCount() >= kMaxPeers) {
                sendTo(conn, MsgType::Refuse, encode(Refuse{RefuseReason::SessionFull, target}), true);
                return;
            }
            p.id = nextPeer++;
            p.name = hello->displayName;
            p.joined = true;
            sendTo(conn, MsgType::Welcome, encode(Welcome{p.id, sequencer.opsSince(0)}));
            broadcastRoster();
            return;
        }
        if (type != MsgType::Submit) {
            closeConn(conn, "unexpected message");
            return;
        }
        auto op = decodeOpMsg(f.payload);
        if (!op) {
            closeConn(conn, "malformed op");
            return;
        }
        op->author = p.id;  // identity comes from the connection, never from the payload
        auto res = sequencer.submit(std::move(*op));
        if (res.sequenced) {
            changed.push_back(res.sequenced->target);
            broadcast(*res.sequenced);
        } else if (res.reason == sync::HostSequencer::Reject::ForgedId) {
            // clientSeq survives in the rejected op only through the original; re-decode to report it.
            auto original = decodeOpMsg(f.payload);
            sendTo(conn, MsgType::Rejected, encode(Rejected{original ? original->clientSeq : 0, 1}));
        }
        // Duplicates are dropped silently: the original was already sequenced and acknowledged.
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
    m->roomKey = static_cast<std::uint16_t>(std::random_device{}() & kRoomKeyMask);
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
        auto all = m->conns;
        for (auto& [_, c] : all) c->close("host stopped");
        m->conns.clear();
        done.set_value();
    });
    fut.wait_for(std::chrono::seconds(2));
    m->net.stop();
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
                bool wasJoined = it != m->peers.end() && it->second.joined;
                m->peers.erase(e.conn);
                if (wasJoined) m->broadcastRoster();
                break;
            }
            case Impl::Event::FrameIn: {
                auto it = m->peers.find(e.conn);
                if (it != m->peers.end()) m->handleFrame(e.conn, it->second, e.frame, changed);
                break;
            }
        }
    }
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

bool SessionHost::kick(sync::PeerId peer) {
    for (auto it = m->peers.begin(); it != m->peers.end(); ++it) {
        if (it->second.joined && it->second.id == peer) {
            m->sendTo(it->first, MsgType::Kicked, {}, true);
            m->peers.erase(it);
            m->broadcastRoster();
            return true;
        }
    }
    return false;
}

} // namespace nexlevel::session

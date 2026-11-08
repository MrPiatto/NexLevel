#include <nexlevel/core/relay/relay_server.hpp>

#include <nexlevel/core/crypto/secure_channel.hpp>
#include <nexlevel/core/relay/protocol.hpp>

#include <asio.hpp>

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace nexlevel::relay {

using asio::ip::tcp;
using Clock = std::chrono::steady_clock;
using SocketPtr = std::shared_ptr<tcp::socket>;

namespace {
void closeSocket(tcp::socket& s) {
    std::error_code ec;
    s.shutdown(tcp::socket::shutdown_both, ec);
    s.close(ec);
}
} // namespace

struct RelayServer::Impl : std::enable_shared_from_this<RelayServer::Impl> {
    // ---- declared before io: must outlive objects destroyed when io is destroyed ----
    Options opt;
    std::atomic<std::uint64_t> bytes{0};
    std::atomic<std::uint64_t> refused{0};
    std::atomic<std::size_t> roomCount{0};
    std::atomic<std::size_t> pipeCount{0};

    struct IpEntry {
        std::size_t conns = 0;
        std::deque<Clock::time_point> failures;
    };
    std::unordered_map<std::string, IpEntry> ips;  // io thread only

    // Counts one live connection for an address; released when the last owner lets go.
    struct IpSlot {
        std::weak_ptr<Impl> impl;
        std::string ip;
        ~IpSlot() {
            auto i = impl.lock();
            if (!i) return;
            auto it = i->ips.find(ip);
            if (it != i->ips.end() && it->second.conns > 0 && --it->second.conns == 0 && it->second.failures.empty()) i->ips.erase(it);
        }
    };
    using SlotPtr = std::shared_ptr<IpSlot>;

    struct Control;
    struct Pending {
        SocketPtr sock;
        SlotPtr slot;
        std::shared_ptr<asio::steady_timer> timer;
    };
    struct Room {
        std::uint32_t id = 0;
        std::uint64_t secret = 0;
        std::shared_ptr<Control> control;
        Clock::time_point lostAt{};
        std::uint32_t nextStream = 0;
        std::map<std::uint32_t, Pending> pending;
        std::shared_ptr<std::atomic<std::size_t>> live = std::make_shared<std::atomic<std::size_t>>(0);
    };
    std::unordered_map<std::uint32_t, Room> rooms;  // io thread only

    // ---- network ----
    asio::io_context io;
    std::optional<tcp::acceptor> acceptor;
    std::optional<asio::steady_timer> cleanupTimer;
    std::thread thread;

    explicit Impl(Options o) : opt(o) {}

    // Host control connection for one room.
    struct Control : std::enable_shared_from_this<Control> {
        Impl* impl;
        SocketPtr sock;
        SlotPtr slot;
        std::uint32_t roomId;
        std::deque<std::vector<std::uint8_t>> queue;
        asio::steady_timer idle;
        std::array<std::uint8_t, kHeaderSize> header{};
        bool closed = false;

        Control(Impl* i, SocketPtr s, SlotPtr sl, std::uint32_t room)
            : impl(i), sock(std::move(s)), slot(std::move(sl)), roomId(room), idle(i->io) {}

        void start() {
            armIdle();
            readNext();
        }
        void armIdle() {
            idle.expires_after(impl->opt.controlIdleTimeout);
            idle.async_wait([self = shared_from_this()](std::error_code ec) {
                if (!ec) self->close();
            });
        }
        void send(const Message& m) {
            if (closed) return;
            queue.push_back(encode(m));
            if (queue.size() == 1) writeNext();
        }
        void writeNext() {
            asio::async_write(*sock, asio::buffer(queue.front()), [self = shared_from_this()](std::error_code ec, std::size_t) {
                if (ec) { self->close(); return; }
                self->queue.pop_front();
                if (!self->queue.empty() && !self->closed) self->writeNext();
            });
        }
        void readNext() {
            asio::async_read(*sock, asio::buffer(header), [self = shared_from_this()](std::error_code ec, std::size_t) {
                if (ec) { self->close(); return; }
                auto kind = decodeHeader(self->header);
                if (!kind || *kind != Kind::Ping) { self->close(); return; }
                self->armIdle();
                self->send(Message{Kind::Pong});
                self->readNext();
            });
        }
        void close() {
            if (closed) return;
            closed = true;
            idle.cancel();
            closeSocket(*sock);
            auto it = impl->rooms.find(roomId);
            if (it != impl->rooms.end() && it->second.control.get() == this) {
                it->second.control.reset();
                it->second.lostAt = Clock::now();
                // Players waiting for this host will not be answered.
                for (auto& [stream, p] : it->second.pending) {
                    p.timer->cancel();
                    impl->sendError(p.sock, ErrorCode::NoRoom);
                }
                it->second.pending.clear();
            }
        }
    };

    // Byte pipe between a player and the host's data connection.
    struct Pipe : std::enable_shared_from_this<Pipe> {
        Impl* impl;
        SocketPtr a, b;
        SlotPtr sa, sb;
        std::shared_ptr<std::atomic<std::size_t>> roomLive;
        std::array<std::uint8_t, 16384> ab{}, ba{};

        Pipe(Impl* i, SocketPtr x, SocketPtr y, SlotPtr sx, SlotPtr sy, std::shared_ptr<std::atomic<std::size_t>> live)
            : impl(i), a(std::move(x)), b(std::move(y)), sa(std::move(sx)), sb(std::move(sy)), roomLive(std::move(live)) {
            ++impl->pipeCount;
            ++*roomLive;
        }
        ~Pipe() {
            --impl->pipeCount;
            --*roomLive;
        }
        void start() {
            std::error_code ec;
            a->set_option(tcp::no_delay(true), ec);
            b->set_option(tcp::no_delay(true), ec);
            pump(a, b, ab);
            pump(b, a, ba);
        }
        void pump(const SocketPtr& from, const SocketPtr& to, std::array<std::uint8_t, 16384>& buf) {
            from->async_read_some(asio::buffer(buf), [self = shared_from_this(), from, to, &buf](std::error_code ec, std::size_t n) {
                if (ec) { self->shutdown(); return; }
                asio::async_write(*to, asio::buffer(buf.data(), n), [self, from, to, &buf, n](std::error_code ec2, std::size_t) {
                    if (ec2) { self->shutdown(); return; }
                    self->impl->bytes += n;
                    self->pump(from, to, buf);
                });
            });
        }
        void shutdown() {
            closeSocket(*a);
            closeSocket(*b);
        }
    };

    void sendError(const SocketPtr& sock, ErrorCode code) {
        auto bytesOut = std::make_shared<std::vector<std::uint8_t>>(encode(Message{Kind::Error, 0, 0, 0, code}));
        asio::async_write(*sock, asio::buffer(*bytesOut), [sock, bytesOut](std::error_code, std::size_t) { closeSocket(*sock); });
    }

    bool tooManyFailures(const std::string& ip) {
        auto it = ips.find(ip);
        if (it == ips.end()) return false;
        auto& f = it->second.failures;
        const auto cutoff = Clock::now() - std::chrono::minutes(1);
        while (!f.empty() && f.front() < cutoff) f.pop_front();
        return f.size() >= opt.maxFailedJoinsPerIpPerMinute;
    }
    void recordFailure(const std::string& ip) {
        ++refused;
        auto it = ips.find(ip);
        if (it != ips.end()) it->second.failures.push_back(Clock::now());
    }

    void acceptNext() {
        acceptor->async_accept([this](std::error_code ec, tcp::socket s) {
            if (!acceptor || !acceptor->is_open()) return;
            if (!ec) handleNew(std::make_shared<tcp::socket>(std::move(s)));
            acceptNext();
        });
    }

    void handleNew(SocketPtr sock) {
        std::error_code ec;
        auto remote = sock->remote_endpoint(ec);
        if (ec) { closeSocket(*sock); return; }
        const auto ip = remote.address().to_string();
        auto& entry = ips[ip];
        if (entry.conns >= opt.maxConnectionsPerIp) { closeSocket(*sock); return; }
        ++entry.conns;
        auto slot = std::make_shared<IpSlot>(IpSlot{weak_from_this(), ip});

        struct Handshake {
            std::array<std::uint8_t, kHeaderSize> header{};
            std::array<std::uint8_t, 16> body{};
            asio::steady_timer timer;
            explicit Handshake(asio::io_context& io) : timer(io) {}
        };
        auto hs = std::make_shared<Handshake>(io);
        hs->timer.expires_after(opt.handshakeTimeout);
        hs->timer.async_wait([sock](std::error_code tec) { if (!tec) closeSocket(*sock); });
        asio::async_read(*sock, asio::buffer(hs->header), [this, sock, slot, hs](std::error_code rec, std::size_t) {
            if (rec) { hs->timer.cancel(); return; }
            auto kind = decodeHeader(hs->header);
            if (!kind || (*kind != Kind::OpenRoom && *kind != Kind::Accept && *kind != Kind::Join)) {
                hs->timer.cancel();
                closeSocket(*sock);
                return;
            }
            const auto size = *bodySize(*kind);
            asio::async_read(*sock, asio::buffer(hs->body.data(), size), [this, sock, slot, hs, kind, size](std::error_code bec, std::size_t) {
                hs->timer.cancel();
                if (bec) return;
                auto msg = decodeBody(*kind, std::span(hs->body.data(), size));
                if (!msg) { closeSocket(*sock); return; }
                switch (*kind) {
                    case Kind::OpenRoom: openRoom(sock, slot, *msg); break;
                    case Kind::Join: join(sock, slot, *msg); break;
                    case Kind::Accept: accept(sock, slot, *msg); break;
                    default: closeSocket(*sock); break;
                }
            });
        });
    }

    void openRoom(const SocketPtr& sock, const SlotPtr& slot, const Message& m) {
        Room* room = nullptr;
        if (m.roomId != 0) {
            auto it = rooms.find(m.roomId);
            if (it != rooms.end() && it->second.secret == m.secret) {
                room = &it->second;
                if (room->control) room->control->close();  // the old control connection is stale
            }
        }
        if (!room) {
            if (rooms.size() >= opt.maxRooms) { sendError(sock, ErrorCode::RelayBusy); return; }
            std::uint32_t id = 0;
            do { id = static_cast<std::uint32_t>(crypto::secureRandomU64()); } while (id == 0 || rooms.contains(id));
            Room r;
            r.id = id;
            r.secret = crypto::secureRandomU64();
            room = &rooms.emplace(id, std::move(r)).first->second;
            roomCount = rooms.size();
        }
        auto c = std::make_shared<Control>(this, sock, slot, room->id);
        room->control = c;
        c->start();
        c->send(Message{Kind::RoomOpened, room->id, room->secret});
    }

    void join(const SocketPtr& sock, const SlotPtr& slot, const Message& m) {
        if (tooManyFailures(slot->ip)) { ++refused; sendError(sock, ErrorCode::RelayBusy); return; }
        auto it = rooms.find(m.roomId);
        if (it == rooms.end() || !it->second.control) {
            recordFailure(slot->ip);
            sendError(sock, ErrorCode::NoRoom);
            return;
        }
        Room& room = it->second;
        if (room.live->load() + room.pending.size() >= opt.maxStreamsPerRoom) {
            ++refused;
            sendError(sock, ErrorCode::RoomFull);
            return;
        }
        if (++room.nextStream == 0) ++room.nextStream;
        const auto stream = room.nextStream;
        auto timer = std::make_shared<asio::steady_timer>(io);
        timer->expires_after(opt.pairTimeout);
        const auto roomId = room.id;
        timer->async_wait([this, roomId, stream](std::error_code ec) {
            if (ec) return;
            auto r = rooms.find(roomId);
            if (r == rooms.end()) return;
            auto p = r->second.pending.find(stream);
            if (p == r->second.pending.end()) return;
            auto s = p->second.sock;
            r->second.pending.erase(p);
            sendError(s, ErrorCode::Timeout);
        });
        room.pending.emplace(stream, Pending{sock, slot, timer});
        room.control->send(Message{Kind::Incoming, 0, 0, stream});
    }

    void accept(const SocketPtr& hostSock, const SlotPtr& hostSlot, const Message& m) {
        auto it = rooms.find(m.roomId);
        if (it == rooms.end() || it->second.secret != m.secret) { closeSocket(*hostSock); return; }
        auto p = it->second.pending.find(m.stream);
        if (p == it->second.pending.end()) { closeSocket(*hostSock); return; }
        Pending pending = std::move(p->second);
        it->second.pending.erase(p);
        pending.timer->cancel();
        auto pipe = std::make_shared<Pipe>(this, pending.sock, hostSock, pending.slot, hostSlot, it->second.live);
        auto paired = std::make_shared<std::vector<std::uint8_t>>(encode(Message{Kind::Paired}));
        asio::async_write(*pending.sock, asio::buffer(*paired), [pipe, paired](std::error_code ec, std::size_t) {
            if (ec) { pipe->shutdown(); return; }
            pipe->start();
        });
    }

    void scheduleCleanup() {
        cleanupTimer->expires_after(std::chrono::seconds(5));
        cleanupTimer->async_wait([this](std::error_code ec) {
            if (ec) return;
            const auto now = Clock::now();
            for (auto it = rooms.begin(); it != rooms.end();) {
                if (!it->second.control && now - it->second.lostAt > opt.reclaimWindow) it = rooms.erase(it);
                else ++it;
            }
            roomCount = rooms.size();
            const auto cutoff = now - std::chrono::minutes(1);
            for (auto it = ips.begin(); it != ips.end();) {
                auto& f = it->second.failures;
                while (!f.empty() && f.front() < cutoff) f.pop_front();
                if (it->second.conns == 0 && f.empty()) it = ips.erase(it);
                else ++it;
            }
            scheduleCleanup();
        });
    }
};

RelayServer::RelayServer() : RelayServer(Options{}) {}
RelayServer::RelayServer(Options opts) : m(std::make_shared<Impl>(opts)) {}
RelayServer::~RelayServer() { stop(); }

std::optional<std::uint16_t> RelayServer::start(std::uint16_t port, bool loopbackOnly, std::string* error) {
    try {
        auto addr = loopbackOnly ? asio::ip::address(asio::ip::address_v4::loopback()) : asio::ip::address(asio::ip::address_v4::any());
        m->acceptor.emplace(m->io);
        tcp::endpoint ep(addr, port);
        m->acceptor->open(ep.protocol());
        m->acceptor->set_option(tcp::acceptor::reuse_address(true));
        m->acceptor->bind(ep);
        m->acceptor->listen();
        const auto bound = m->acceptor->local_endpoint().port();
        m->cleanupTimer.emplace(m->io);
        m->acceptNext();
        m->scheduleCleanup();
        m->thread = std::thread([impl = m.get()] { impl->io.run(); });
        return bound;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        m->acceptor.reset();
        return std::nullopt;
    }
}

void RelayServer::stop() {
    if (!m->thread.joinable()) return;
    m->io.stop();
    m->thread.join();
    // Close everything while the io_context still exists.
    for (auto& [id, room] : m->rooms) {
        if (room.control) closeSocket(*room.control->sock);
        for (auto& [s, p] : room.pending) closeSocket(*p.sock);
    }
    m->rooms.clear();
    m->cleanupTimer.reset();
    m->acceptor.reset();
}

RelayStats RelayServer::stats() const {
    return RelayStats{m->roomCount.load(), m->pipeCount.load(), m->bytes.load(), m->refused.load()};
}

} // namespace nexlevel::relay

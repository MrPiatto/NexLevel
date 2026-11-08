#include <nexlevel/core/session/client.hpp>

#include "../net/connection.hpp"

#include <chrono>
#include <future>

namespace nexlevel::session {

using asio::ip::tcp;

struct SessionClient::Impl {
    struct Event {
        enum Kind { Connected, FrameIn, Closed, ConnectFailed } kind;
        Frame frame;
        std::string reason;
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

    Impl(GameTarget t, std::string n) : target(t), name(std::move(n)) {}

    void send(MsgType type, std::vector<std::uint8_t> payload) {
        auto bytes = encodeFrame(static_cast<std::uint16_t>(type), payload);
        net.post([this, bytes = std::move(bytes)]() mutable {
            if (conn) conn->send(std::move(bytes));
        });
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

void SessionClient::connect(const std::string& host, std::uint16_t port) {
    m->state = ClientState::Connecting;
    m->error.clear();
    if (!m->netStarted) {
        m->net.start();
        m->netStarted = true;
    }
    m->net.post([this, host, port] {
        auto resolver = std::make_shared<tcp::resolver>(m->net.io());
        resolver->async_resolve(host, std::to_string(port), [this, resolver](std::error_code ec, tcp::resolver::results_type results) {
            if (ec) { m->events.push({Impl::Event::ConnectFailed, {}, "resolve: " + ec.message()}); return; }
            auto socket = std::make_shared<tcp::socket>(m->net.io());
            asio::async_connect(*socket, results, [this, socket](std::error_code ec2, const tcp::endpoint&) {
                if (ec2) { m->events.push({Impl::Event::ConnectFailed, {}, "connect: " + ec2.message()}); return; }
                m->conn = std::make_shared<net::Connection>(
                    std::move(*socket), 1,
                    [this](std::uint32_t, Frame f) { m->events.push({Impl::Event::FrameIn, std::move(f), {}}); },
                    [this](std::uint32_t, std::string r) { m->events.push({Impl::Event::Closed, {}, std::move(r)}); });
                m->conn->start();
                m->events.push({Impl::Event::Connected, {}, {}});
            });
        });
    });
}

void SessionClient::disconnect() {
    if (m->netStarted) {
        m->net.post([this] { if (m->conn) m->conn->close("disconnected by user"); });
    }
    m->state = ClientState::Disconnected;
}

std::vector<sync::NetId> SessionClient::pump() {
    std::vector<sync::NetId> changed;
    for (auto& e : m->events.drain()) {
        switch (e.kind) {
            case Impl::Event::Connected:
                m->state = ClientState::Joining;
                m->send(MsgType::Hello, encode(Hello{m->target, m->name}));
                break;
            case Impl::Event::ConnectFailed:
                m->state = ClientState::Disconnected;
                m->error = e.reason;
                break;
            case Impl::Event::Closed:
                if (m->state != ClientState::Refused) {
                    m->state = ClientState::Disconnected;
                    m->error = e.reason;
                }
                break;
            case Impl::Event::FrameIn: {
                const auto type = static_cast<MsgType>(e.frame.type);
                if (m->state == ClientState::Joining && type == MsgType::Welcome) {
                    auto w = decodeWelcome(e.frame.payload);
                    if (!w) { m->error = "malformed welcome"; disconnect(); break; }
                    m->replica.emplace(w->peerId);
                    m->ids.emplace(w->peerId);
                    for (const auto& op : w->backlog) {
                        auto c = m->replica->onSequenced(op);
                        changed.insert(changed.end(), c.begin(), c.end());
                    }
                    m->state = ClientState::Joined;
                } else if (type == MsgType::Refuse) {
                    auto r = decodeRefuse(e.frame.payload);
                    m->state = ClientState::Refused;
                    m->refuse = r ? std::optional(r->reason) : std::nullopt;
                    m->hostTarget = r ? std::optional(r->hostTarget) : std::nullopt;
                } else if (m->state == ClientState::Joined && type == MsgType::Sequenced) {
                    auto op = decodeOpMsg(e.frame.payload);
                    if (!op) { m->error = "malformed op from host"; disconnect(); break; }
                    auto c = m->replica->onSequenced(*op);
                    changed.insert(changed.end(), c.begin(), c.end());
                } else if (m->state == ClientState::Joined && type == MsgType::Rejected) {
                    auto r = decodeRejected(e.frame.payload);
                    if (r) {
                        auto c = m->replica->onRejected(r->clientSeq);
                        changed.insert(changed.end(), c.begin(), c.end());
                    }
                }
                break;
            }
        }
    }
    return changed;
}

std::optional<sync::Op> SessionClient::submitLocal(sync::Op op) {
    if (m->state != ClientState::Joined) return std::nullopt;
    auto sent = m->replica->local(std::move(op));
    m->send(MsgType::Submit, encodeOpMsg(sent));
    return sent;
}

std::optional<sync::NetId> SessionClient::newId() {
    if (!m->ids) return std::nullopt;
    return m->ids->next();
}

ClientState SessionClient::state() const noexcept { return m->state; }
const std::string& SessionClient::lastError() const noexcept { return m->error; }
std::optional<RefuseReason> SessionClient::refuseReason() const noexcept { return m->refuse; }
std::optional<GameTarget> SessionClient::hostTarget() const noexcept { return m->hostTarget; }
const sync::ClientReplica* SessionClient::replica() const noexcept { return m->replica ? &*m->replica : nullptr; }

} // namespace nexlevel::session

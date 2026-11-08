#include "session_manager.hpp"

#include <nexlevel/core/net/local_address.hpp>
#include <nexlevel/core/relay/protocol.hpp>
#include <nexlevel/core/session/room_code.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <thread>

using namespace geode::prelude;

namespace nexlevel::mod {

void notify(const std::string& text, NotificationIcon icon) {
    log::info("NexLevel notice: {}", text);
    // Shown on the next frame, and only if there is a scene: notices can be triggered while
    // cocos2d tears the editor down (e.g. quitting the game), where showing one crashes.
    geode::queueInMainThread([text, icon] {
        if (!CCDirector::sharedDirector()->getRunningScene()) return;
        Notification::create(text, icon)->show();
    });
}

namespace {
// "relay-server" setting: host or host:port. nullopt when empty.
std::optional<std::pair<std::string, std::uint16_t>> relaySetting() {
    auto v = Mod::get()->getSettingValue<std::string>("relay-server");
    v.erase(std::remove_if(v.begin(), v.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), v.end());
    if (v.empty()) return std::nullopt;
    auto colon = v.rfind(':');
    if (colon == std::string::npos) return std::pair{v, relay::kDefaultPort};
    int port = std::atoi(v.substr(colon + 1).c_str());
    if (port <= 0 || port > 65535) return std::nullopt;
    return std::pair{v.substr(0, colon), static_cast<std::uint16_t>(port)};
}
} // namespace

SessionManager* SessionManager::get() {
    static SessionManager* instance = [] {
        auto* m = new SessionManager();
        m->retain();  // lives for the whole game
        return m;
    }();
    return instance;
}

void SessionManager::ensureScheduled() {
    if (m_scheduled) return;
    CCDirector::sharedDirector()->getScheduler()->scheduleSelector(
        schedule_selector(SessionManager::tick), this, 0.f, false);
    m_scheduled = true;
}

Role SessionManager::role() const {
    if (m_host) return Role::Host;
    if (m_client) return Role::Client;
    return Role::None;
}

bool SessionManager::inSession() const {
    if (m_host) return m_host->running();
    if (m_client) {
        auto st = m_client->state();
        // While reconnecting, local edits keep being recorded and are sent once back in the room.
        return st == session::ClientState::Joined || st == session::ClientState::Reconnecting;
    }
    return false;
}

std::string SessionManager::displayName() const {
    std::string name = GJAccountManager::sharedState()->m_username;
    if (name.empty()) name = "Player";
    return name.substr(0, session::kMaxDisplayName);
}

bool SessionManager::hostRoom(std::string* error) {
    if (role() != Role::None) { if (error) *error = "Already in a session"; return false; }
    if (!m_editor) { if (error) *error = "Open the editor first"; return false; }
    auto host = std::make_unique<session::SessionHost>(static_cast<GameTarget>(NEXLEVEL_TARGET), displayName());
    std::string err;
    if (!host->start(kDefaultPort, false, &err)) {
        if (error) *error = "Could not open port " + std::to_string(kDefaultPort) + ": " + err;
        return false;
    }
    m_host = std::move(host);
    m_mapper = std::make_unique<net::PortMapper>();
    m_mapper->start(m_host->port());
    if (auto r = relaySetting()) {
        m_host->startRelay(r->first, r->second);
        log::info("NexLevel: opening the room on relay {}:{}", r->first, r->second);
    }
    m_roomLevel = m_editor->level();
    if (m_roomLevel) m_roomLevel->retain();
    ensureScheduled();
    m_editor->seedSession();
    log::info("NexLevel: room opened on port {}", kDefaultPort);
    return true;
}

bool SessionManager::join(const std::string& roomCode, std::string* error) {
    if (role() != Role::None) { if (error) *error = "Already in a room"; return false; }
    auto addr = session::decodeRoomCode(roomCode);
    if (!addr) { if (error) *error = "That room code isn't valid - check for typos"; return false; }
    auto relayServer = relaySetting();
    if (addr->relayRoom != 0 && !relayServer) {
        if (error) *error = "This code needs the relay server - set it in the NexLevel settings";
        return false;
    }
    m_client = std::make_unique<session::SessionClient>(static_cast<GameTarget>(NEXLEVEL_TARGET), displayName());
    if (relayServer) m_client->setRelayServer(relayServer->first, relayServer->second);
    m_lastClientState = session::ClientState::Idle;
    m_lastJoined = false;
    m_client->connectCode(roomCode);
    ensureScheduled();
    log::info("NexLevel: joining room {}", roomCode);
    return true;
}

void SessionManager::leave() {
    if (m_host) {
        m_host->stop();
        m_host.reset();
        releaseMapper();
        if (m_roomLevel) { m_roomLevel->release(); m_roomLevel = nullptr; }
        notify("Room closed", NotificationIcon::Info);
    } else if (m_client) {
        m_client->disconnect();
        m_client.reset();
        notify("Left the room", NotificationIcon::Info);
    }
}

bool SessionManager::kick(sync::PeerId peer) { return m_host && m_host->kick(peer); }

std::vector<session::PeerInfo> SessionManager::peers() const {
    if (m_host) return m_host->peers();
    if (m_client) return m_client->peers();
    return {};
}

std::optional<sync::PeerId> SessionManager::selfId() const {
    if (m_host) return session::SessionHost::kHostPeer;
    if (m_client) return m_client->peerId();
    return std::nullopt;
}

void SessionManager::releaseMapper() {
    if (!m_mapper) return;
    // Removing the router mapping can take a moment; never block the game for it.
    std::thread([mapper = std::move(m_mapper)]() mutable { mapper->stop(); }).detach();
}

std::optional<std::string> SessionManager::roomCode() const {
    if (!m_host) return std::nullopt;
    // Relay first: it works for everyone, on any network.
    if (m_host->relayRoomId() != 0) {
        session::RoomAddress r;
        r.relayRoom = m_host->relayRoomId();
        r.key = m_host->roomKey();
        return session::encodeRoomCode(r);
    }
    const bool relayPending = m_host->relayState() != session::RelayState::Off && m_host->relayError().empty();
    // The code carries this PC's address; players never see or type it.
    auto lan = session::parseIPv4(net::primaryLocalIPv4().value_or("127.0.0.1"))
                   .value_or(std::array<std::uint8_t, 4>{127, 0, 0, 1});
    session::RoomAddress addr{lan, m_host->port(), m_host->roomKey()};
    if (m_mapper) {
        const auto st = m_mapper->state();
        if (st == net::MapState::Working || (relayPending && st != net::MapState::Mapped)) return std::nullopt;
        if (st == net::MapState::Mapped) {
            if (auto pub = m_mapper->externalIPv4()) {
                addr.ipv4 = *pub;
                addr.port = m_mapper->externalPort();
                addr.lanOctet = lan[3] == 0 ? 1 : lan[3];
            }
        }
    }
    return session::encodeRoomCode(addr);
}

std::string SessionManager::onlineText() const {
    if (!m_host) return {};
    if (m_host->relayRoomId() != 0) {
        return m_host->relayState() == session::RelayState::Ready ? "Online: friends on any network can join"
                                                                   : "Reconnecting to the relay...";
    }
    if (m_host->relayState() != session::RelayState::Off && m_host->relayError().empty()) return "Connecting to the relay...";
    if (!m_mapper) return {};
    switch (m_mapper->state()) {
        case net::MapState::Working: return "Opening your router for online play...";
        case net::MapState::Mapped: return "Online: friends anywhere can join";
        case net::MapState::NoGateway: return "Same Wi-Fi only: router has UPnP turned off";
        case net::MapState::DoubleNat: return "Same Wi-Fi only: your network blocks hosting";
        case net::MapState::Failed: return "Same Wi-Fi only: router refused to open a port";
        case net::MapState::Idle: break;
    }
    return {};
}

std::string SessionManager::statusText() const {
    using session::ClientState;
    if (m_host) return "Hosting - " + std::to_string(m_host->peers().size()) + " in room";
    if (!m_client) return "Not in a room";
    switch (m_client->state()) {
        case ClientState::Connecting: return "Connecting...";
        case ClientState::Joining: return "Joining...";
        case ClientState::Joined: return "Connected - " + std::to_string(m_client->peers().size()) + " in room";
        case ClientState::Reconnecting: return "Reconnecting...";
        default: return "Disconnected";
    }
}

void SessionManager::attachEditor(IEditorBridge* bridge) {
    m_editor = bridge;
    if (!inSession()) return;
    if (m_host && !isRoomLevel(bridge->level())) {
        notify("NexLevel room is open for another level", NotificationIcon::Warning);
        m_editor = nullptr;  // never overwrite a different level with the room's content
        return;
    }
    bridge->rebuildFromSession();
}

void SessionManager::detachEditor(IEditorBridge* bridge) {
    if (m_editor != bridge) return;
    m_editor = nullptr;
    if (m_host) {
        notify("Room still open - players can keep building", NotificationIcon::Info);
    } else if (m_client) {
        leave();
    }
}

bool SessionManager::isRoomLevel(GJGameLevel* level) const { return m_roomLevel && level == m_roomLevel; }

std::optional<sync::NetId> SessionManager::newId() {
    if (m_host) return m_host->newId();
    if (m_client) return m_client->newId();
    return std::nullopt;
}

void SessionManager::submitLocal(sync::Op op) {
    if (m_host) m_host->submitLocal(std::move(op));
    else if (m_client) m_client->submitLocal(std::move(op));
}

const sync::Replica* SessionManager::view() const {
    if (m_host) return &m_host->state();
    if (m_client && m_client->replica()) return &m_client->replica()->view();
    return nullptr;
}

void SessionManager::endClient(const std::string& message, NotificationIcon icon) {
    notify(message, icon);
    log::info("NexLevel: {}", message);
    m_client.reset();
}

void SessionManager::tick(float) {
    // Log and announce players joining/leaving (host and client).
    auto now = peers();
    if (now != m_lastPeers) {
        for (auto& p : now) {
            if (std::find(m_lastPeers.begin(), m_lastPeers.end(), p) == m_lastPeers.end() && !m_lastPeers.empty()) {
                log::info("NexLevel: {} joined the room", p.name);
                notify(p.name + " joined", NotificationIcon::Info);
            }
        }
        for (auto& p : m_lastPeers) {
            if (std::find(now.begin(), now.end(), p) == now.end() && !now.empty()) {
                log::info("NexLevel: {} left the room", p.name);
                notify(p.name + " left", NotificationIcon::Info);
            }
        }
        m_lastPeers = now;
    }
    if (m_host) {
        auto ids = m_host->pump();
        if (m_editor && !ids.empty()) m_editor->applyRemote(ids);
        return;
    }
    if (!m_client) return;

    using session::ClientState;
    auto ids = m_client->pump();
    auto state = m_client->state();
    if (state != m_lastClientState) {
        m_lastClientState = state;
        switch (state) {
            case ClientState::Joined:
                if (!m_lastJoined) {
                    m_lastJoined = true;
                    notify("Joined the room", NotificationIcon::Success);
                    if (m_editor) m_editor->rebuildFromSession();
                    ids.clear();
                } else {
                    notify("Reconnected", NotificationIcon::Success);
                    if (m_client->takeIdentityReset() && m_editor) {  // the host no longer knew us
                        m_editor->rebuildFromSession();
                        ids.clear();
                    }
                }
                break;
            case ClientState::Reconnecting:
                log::warn("NexLevel: connection lost ({}), reconnecting", m_client->lastError());
                notify("Connection lost - reconnecting...", NotificationIcon::Warning);
                break;
            case ClientState::Refused: {
                auto reason = m_client->refuseReason();
                if (reason == session::RefuseReason::WrongGameVersion) {
                    auto host = m_client->hostTarget();
                    endClient(std::string("The host uses ") + std::string(toString(host.value_or(GameTarget::Unknown))) +
                                  " - same GD version required", NotificationIcon::Error);
                } else if (reason == session::RefuseReason::WrongRoomCode) {
                    endClient("Wrong room code - ask the host for the current one", NotificationIcon::Error);
                } else if (reason == session::RefuseReason::SessionFull) {
                    endClient("The room is full", NotificationIcon::Error);
                } else {
                    endClient("The host refused the connection", NotificationIcon::Error);
                }
                return;
            }
            case ClientState::Kicked: endClient("You were removed from the room", NotificationIcon::Warning); return;
            case ClientState::RoomClosed: endClient("The host closed the room", NotificationIcon::Info); return;
            case ClientState::Disconnected:
                // Friendly wording: players never see addresses or socket errors (they are in the log).
                log::warn("NexLevel: connection lost: {}", m_client->lastError());
                endClient(m_lastJoined ? "Connection to the room lost"
                          : m_client->lastError().find("did not answer") != std::string::npos ? "The host isn't responding - try again"
                          : m_client->lastError().find("relay: relay busy") != std::string::npos ? "The relay is busy - try again in a minute"
                          : "Room not found - check the code",
                          NotificationIcon::Error);
                return;
            default: break;
        }
    }
    if (state == ClientState::Joined && m_editor && !ids.empty()) m_editor->applyRemote(ids);
}

} // namespace nexlevel::mod

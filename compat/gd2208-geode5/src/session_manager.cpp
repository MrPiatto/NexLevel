#include "session_manager.hpp"

#include <nexlevel/core/net/local_address.hpp>

using namespace geode::prelude;

namespace nexlevel::mod {

void notify(const std::string& text, NotificationIcon icon) {
    Notification::create(text, icon)->show();
}

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
    if (m_client) return m_client->state() == session::ClientState::Joined;
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
    m_roomLevel = m_editor->level();
    if (m_roomLevel) m_roomLevel->retain();
    ensureScheduled();
    m_editor->seedSession();
    log::info("NexLevel: room opened on port {}", kDefaultPort);
    return true;
}

void SessionManager::join(const std::string& address) {
    if (role() != Role::None) return;
    std::string host = address;
    std::uint16_t port = kDefaultPort;
    if (auto colon = address.rfind(':'); colon != std::string::npos) {
        host = address.substr(0, colon);
        try { port = static_cast<std::uint16_t>(std::stoi(address.substr(colon + 1))); } catch (...) {}
    }
    m_client = std::make_unique<session::SessionClient>(static_cast<GameTarget>(NEXLEVEL_TARGET), displayName());
    m_lastClientState = session::ClientState::Idle;
    m_client->connect(host, port);
    ensureScheduled();
    log::info("NexLevel: joining {}:{}", host, port);
}

void SessionManager::leave() {
    if (m_host) {
        m_host->stop();
        m_host.reset();
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

std::optional<std::string> SessionManager::hostAddress() const {
    if (!m_host) return std::nullopt;
    auto ip = net::primaryLocalIPv4();
    return ip.value_or("this PC") + ":" + std::to_string(m_host->port());
}

std::string SessionManager::statusText() const {
    using session::ClientState;
    if (m_host) return "Hosting - " + std::to_string(m_host->peers().size()) + " in room";
    if (!m_client) return "Not in a room";
    switch (m_client->state()) {
        case ClientState::Connecting: return "Connecting...";
        case ClientState::Joining: return "Joining...";
        case ClientState::Joined: return "Connected - " + std::to_string(m_client->peers().size()) + " in room";
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
                notify("Joined the room", NotificationIcon::Success);
                if (m_editor) m_editor->rebuildFromSession();
                ids.clear();
                break;
            case ClientState::Refused: {
                auto reason = m_client->refuseReason();
                if (reason == session::RefuseReason::WrongGameVersion) {
                    auto host = m_client->hostTarget();
                    endClient(std::string("The host uses ") + std::string(toString(host.value_or(GameTarget::Unknown))) +
                                  " - same GD version required", NotificationIcon::Error);
                } else if (reason == session::RefuseReason::SessionFull) {
                    endClient("The room is full", NotificationIcon::Error);
                } else {
                    endClient("The host refused the connection", NotificationIcon::Error);
                }
                return;
            }
            case ClientState::Kicked: endClient("You were removed from the room", NotificationIcon::Warning); return;
            case ClientState::RoomClosed: endClient("The host closed the room", NotificationIcon::Info); return;
            case ClientState::Disconnected: endClient("Disconnected: " + m_client->lastError(), NotificationIcon::Error); return;
            default: break;
        }
    }
    if (state == ClientState::Joined && m_editor && !ids.empty()) m_editor->applyRemote(ids);
}

} // namespace nexlevel::mod

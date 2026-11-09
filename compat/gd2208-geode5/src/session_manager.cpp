#include "session_manager.hpp"

#include "hosting_badge.hpp"
#include "relay_directory.hpp"

#include <nexlevel/core/net/local_address.hpp>
#include <nexlevel/core/relay/protocol.hpp>
#include <nexlevel/core/session/room_code.hpp>
#include <nexlevel/core/sync/level_settings.hpp>

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

bool SessionManager::hostRoom(std::string* error, bool onCopy) {
    if (role() != Role::None) { if (error) *error = "Already in a session"; return false; }
    if (!m_editor) { if (error) *error = "Open the editor first"; return false; }
    if (onCopy && !makeHostCopy(error)) return false;
    auto host = std::make_unique<session::SessionHost>(static_cast<GameTarget>(NEXLEVEL_TARGET), displayName());
    host->setAccountId(static_cast<std::uint32_t>(std::max(0, GJAccountManager::sharedState()->m_accountID)));
    std::string err;
    if (!host->start(kDefaultPort, false, &err)) {
        if (error) *error = "Could not open port " + std::to_string(kDefaultPort) + ": " + err;
        return false;
    }
    m_host = std::move(host);
    m_mapper = std::make_unique<net::PortMapper>();
    m_mapper->start(m_host->port());
    startBestRelay();
    m_roomLevel = m_editor->level();
    m_roomStarted = std::chrono::steady_clock::now();
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
    auto custom = customRelay();
    auto directory = relayDirectory();
    if (addr->relayRoom != 0) {
        const bool known = addr->relayId == session::kCustomRelayId
                               ? custom.has_value()
                               : std::any_of(directory.begin(), directory.end(), [&](auto& r) { return r.id == addr->relayId; });
        if (!known) {
            if (error) {
                *error = addr->relayId == session::kCustomRelayId ? "This room uses a private relay - set it in NexLevel Settings"
                                                                  : "This room uses a newer relay - update NexLevel";
            }
            return false;
        }
    }
    m_client = std::make_unique<session::SessionClient>(static_cast<GameTarget>(NEXLEVEL_TARGET), displayName());
    m_client->setRelayDirectory(directory);
    if (custom) m_client->setRelayServer(custom->first, custom->second);
    m_client->setAccountId(static_cast<std::uint32_t>(std::max(0, GJAccountManager::sharedState()->m_accountID)));
    m_lastClientState = session::ClientState::Idle;
    m_lastJoined = false;
    m_roomStarted = std::chrono::steady_clock::now();
    m_client->connectCode(roomCode);
    ensureScheduled();
    log::info("NexLevel: joining room {}", roomCode);
    return true;
}

// Copies the open level (content, settings, song, name, description) into a new local level and
// points the editor at it: every save from now on (GD's own and NexLevel's autosave) goes to the copy.
bool SessionManager::makeHostCopy(std::string* error) {
    auto* original = m_editor->level();
    auto* copy = GameLevelManager::sharedState()->createNewLevel();
    if (!original || !copy) { if (error) *error = "Could not create the copy"; return false; }
    std::string name = std::string(original->m_levelName);
    if (name.size() > 15) name = name.substr(0, 15);
    copy->m_levelName = name + " (NL)";
    copy->m_levelDesc = original->m_levelDesc;
    copy->m_audioTrack = original->m_audioTrack;
    copy->m_songID = original->m_songID;
    copy->m_songIDs = original->m_songIDs;
    copy->m_sfxIDs = original->m_sfxIDs;
    copy->m_levelString = ZipUtils::compressString(m_editor->levelString(), false, 0);
    copy->retain();  // the editor now holds it
    m_editor->switchLevel(copy);
    LocalLevelManager::sharedState()->save();
    log::info("NexLevel: hosting on a copy '{}' (original '{}' untouched)", std::string(copy->m_levelName), std::string(original->m_levelName));
    notify("Hosting on a copy: \"" + std::string(copy->m_levelName) + "\" - your original is untouched", NotificationIcon::Info);
    return true;
}

void SessionManager::leave() {
    if (m_host || m_client) saveNow(true, "leaving the room");
    if (m_host) {
        ++m_roomGeneration;
        m_relayProbing = false;
        m_host->stop();
        m_host.reset();
        releaseMapper();
        if (m_roomLevel) { m_roomLevel->release(); m_roomLevel = nullptr; }
        notify("Room closed", NotificationIcon::Info);
    } else if (m_client) {
        m_client->disconnect();
        m_client.reset();
        if (m_backupLevel) { m_backupLevel->release(); m_backupLevel = nullptr; }
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

void SessionManager::setLocalPresence(float x, float y, float zoom) {
    if (m_host) m_host->setLocalPresence(x, y, zoom);
    else if (m_client) m_client->setLocalPresence(x, y, zoom);
}

std::vector<session::Presence> SessionManager::presences() const {
    if (m_host) return m_host->presences();
    if (m_client) return m_client->presences();
    return {};
}

void SessionManager::setLocalPlayerState(const session::PlayerState& state) {
    if (m_host) m_host->setLocalPlayerState(state);
    else if (m_client) m_client->setLocalPlayerState(state);
}

const std::map<sync::PeerId, session::PlayTrail>& SessionManager::playTrails() const {
    static const std::map<sync::PeerId, session::PlayTrail> none;
    if (m_host) return m_host->playTrails();
    if (m_client) return m_client->playTrails();
    return none;
}

std::string SessionManager::peerName(sync::PeerId peer) const {
    for (auto& p : peers()) {
        if (p.id == peer) return p.name;
    }
    return "Player";
}

std::optional<int> SessionManager::pingOf(sync::PeerId peer) const {
    if (m_host) {
        if (peer == session::SessionHost::kHostPeer) return 0;
        auto st = m_host->pingStats(peer);
        if (st && st->ms) return static_cast<int>(*st->ms + .5f);
        return std::nullopt;
    }
    if (m_client) {
        if (peer == session::SessionHost::kHostPeer) return 0;
        if (m_client->peerId() == peer) {
            auto st = m_client->pingStats();
            if (st.ms) return static_cast<int>(*st.ms + .5f);
        }
        auto table = m_client->pingTable();
        if (auto it = table.find(peer); it != table.end()) return it->second;
    }
    return std::nullopt;
}

std::uint32_t SessionManager::accountOf(sync::PeerId peer) const {
    for (auto& p : peers()) {
        if (p.id == peer) return p.accountId;
    }
    return 0;
}

SessionManager::ConnectionInfo SessionManager::connectionInfo() const {
    ConnectionInfo info;
    info.localIp = net::primaryLocalIPv4().value_or("-");
    if (m_host) {
        info.role = "Host";
        info.port = m_host->port();
        if (m_host->relayRoomId() != 0) info.via = "Relay";
        else if (m_mapper && m_mapper->state() == net::MapState::Mapped) info.via = "Router (UPnP)";
        else info.via = "Local network";
        if (m_mapper) {
            if (auto pub = m_mapper->externalIPv4()) {
                session::RoomAddress a;
                a.ipv4 = *pub;
                info.publicIp = a.ipString();
            }
        }
        // The host has no ping of its own: show the slowest player's connection.
        float worst = -1.f;
        for (auto& p : m_host->peers()) {
            if (p.id == session::SessionHost::kHostPeer) continue;
            auto st = m_host->pingStats(p.id);
            if (st && st->ms && st->avgMs > worst) {
                worst = st->avgMs;
                info.ping = *st;
                info.pingSubject = p.name;
            }
        }
    } else if (m_client) {
        info.role = "Player";
        info.ping = m_client->pingStats();
        info.pingSubject = "to the host";
        if (auto via = m_client->connectedVia()) {
            info.port = via->port;
            if (via->relayRoom != 0) info.via = "Relay";
            else {
                auto ip = session::parseIPv4(via->host);
                bool lan = ip && net::isPrivateIPv4(*ip);
                info.via = lan ? "Local network" : "Direct";
            }
        }
    }
    return info;
}

int SessionManager::roomSeconds() const {
    if (role() == Role::None) return 0;
    return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_roomStarted).count());
}

std::size_t SessionManager::objectCount() const {
    auto* v = view();
    if (!v) return 0;
    std::size_t n = v->objects().size();
    if (v->find(sync::kLevelSettingsId)) --n;  // the settings entry is not an object
    return n;
}

// Player's own relay if set; otherwise the nearest reachable relay of the directory (probed in
// the background so the game never waits). Without any relay the room falls back to UPnP / LAN.
void SessionManager::startBestRelay() {
    if (!m_host) return;
    if (auto custom = customRelay()) {
        m_host->startRelay(custom->first, custom->second, session::kCustomRelayId);
        log::info("NexLevel: opening the room on the custom relay {}:{}", custom->first, custom->second);
        return;
    }
    auto relays = relayDirectory();
    if (relays.empty()) return;
    m_relayProbing = true;
    const auto generation = ++m_roomGeneration;
    std::thread([relays, generation] {
        auto rtts = relay::probeRelays(relays, std::chrono::milliseconds(2500));
        geode::queueInMainThread([relays, rtts, generation] {
            auto* sm = SessionManager::get();
            if (sm->m_roomGeneration != generation || !sm->m_host) return;  // room closed meanwhile
            sm->m_relayProbing = false;
            std::optional<std::size_t> best;
            for (std::size_t i = 0; i < relays.size(); ++i) {
                if (rtts[i] && (!best || *rtts[i] < *rtts[*best])) best = i;
            }
            if (!best) {
                log::warn("NexLevel: no relay reachable, using direct connections only");
                return;
            }
            const auto& r = relays[*best];
            log::info("NexLevel: opening the room on relay {} ({}:{}, {:.0f} ms)", r.name, r.host, r.port, *rtts[*best]);
            sm->m_host->startRelay(r.host, r.port, r.id);
        });
    }).detach();
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
        r.relayId = m_host->relayId();
        r.key = m_host->roomKey();
        return session::encodeRoomCode(r);
    }
    const bool relayPending = m_relayProbing || (m_host->relayState() != session::RelayState::Off && m_host->relayError().empty());
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
    if (m_relayProbing) return "Finding the nearest relay...";
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
        // In memory now (the editor may be torn down because GD is closing); to disk on the next frame.
        saveNow(false, "host left the editor");
        geode::queueInMainThread([] { if (auto* lm = LocalLevelManager::sharedState()) lm->save(); });
        notify("Room still open - players can keep building. Autosave is on.", NotificationIcon::Info);
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
    m_roomDirty = true;
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
    saveNow(true, "room ended");  // the player's backup keeps everything built so far
    m_client.reset();
    if (m_backupLevel) { m_backupLevel->release(); m_backupLevel = nullptr; }
}

void SessionManager::tick(float dt) {
    m_badgeTimer += dt;
    if (m_badgeTimer >= 0.5f) {
        m_badgeTimer = 0.f;
        updateHostingBadge();
    }
    autosaveTick();
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
        if (!ids.empty()) m_roomDirty = true;
        if (m_editor && !ids.empty()) m_editor->applyRemote(ids);
        return;
    }
    if (!m_client) return;

    using session::ClientState;
    auto ids = m_client->pump();
    if (!ids.empty()) m_roomDirty = true;
    auto state = m_client->state();
    if (state != m_lastClientState) {
        m_lastClientState = state;
        switch (state) {
            case ClientState::Joined:
                if (!m_lastJoined) {
                    m_lastJoined = true;
                    notify("Joined the room", NotificationIcon::Success);
                    m_roomDirty = true;  // first backup of the room soon after joining
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
                          : m_client->lastError().find("different NexLevel version") != std::string::npos ? "Couldn't join - you and the host need the same NexLevel version"
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

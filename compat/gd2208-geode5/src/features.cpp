// Collaboration extras: room chat, view-only players and the "GD closed during a room" report prompt.
#include "session_manager.hpp"
#include "relay_directory.hpp"

#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/web.hpp>

#include <nexlevel/core/relay/protocol.hpp>
#include <nexlevel/core/sync/level_settings.hpp>
#include <algorithm>
#include <map>
#include <thread>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {

constexpr std::size_t kChatKeep = 60;
bool g_markerOn = false;

// Present while this game is in a room; removed when the room ends normally or GD saves on exit.
// Still there at the next start = GD closed unexpectedly during a room.
std::filesystem::path sessionMarker() { return Mod::get()->getSaveDir() / "in-room.marker"; }

void setMarker(bool on) {
    if (on == g_markerOn) return;
    g_markerOn = on;
    std::error_code ec;
    if (on) std::ofstream(sessionMarker()) << "1";
    else std::filesystem::remove(sessionMarker(), ec);
}

} // namespace

void SessionManager::sendChat(const std::string& text) {
    if (m_host) m_host->sendChat(text);
    else if (m_client) m_client->sendChat(text);
}

void SessionManager::setViewOnly(sync::PeerId peer, bool on) {
    if (m_host) m_host->setViewOnly(peer, on);
}

bool SessionManager::isViewOnly(sync::PeerId peer) const {
    if (m_host) return m_host->isViewOnly(peer);
    for (auto& p : peers()) {
        if (p.id == peer) return p.viewOnly;
    }
    return false;
}

void SessionManager::editorClosed() {
    if (m_noSaveLevel) {
        m_noSaveLevel->release();
        m_noSaveLevel = nullptr;
    }
}

// A player's room ended: forget what they could take with them.
void SessionManager::afterClientSession() {
    GameManager::get()->m_editorClipboard = "";  // no pasting the room into another level later
    // m_noSaveLevel stays locked until that editor closes; m_tempLevel is deleted by cleanupTempLevel.
}

// The level made to open a room from the main menu is deleted once nothing shows it any more.
void SessionManager::cleanupTempLevel() {
    if (!m_tempLevel || m_client) return;
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene || typeinfo_cast<CCTransitionScene*>(scene) || LevelEditorLayer::get() || PlayLayer::get() ||
        scene->getChildByType<EditLevelLayer>(0)) {
        return;
    }
    GameLevelManager::sharedState()->deleteLevel(m_tempLevel);
    m_tempLevel->release();
    m_tempLevel = nullptr;
    LocalLevelManager::sharedState()->save();
    log::info("NexLevel: removed the temporary room level");
}

// ---------------------------------------------------------------- host room settings
void SessionManager::applyHostSettings() {
    if (!m_host) return;
    m_host->setMaxPlayers(static_cast<std::size_t>(maxPlayers()));
    m_host->setSettingsLocked(Mod::get()->getSavedValue<bool>("room-settings-locked", false));
    m_public = Mod::get()->getSavedValue<bool>("room-public", false);
    m_lastListing = {};
}

void SessionManager::setPublic(bool on) {
    m_public = on;
    Mod::get()->setSavedValue<bool>("room-public", on);
    m_lastListing = {};  // send now
    if (m_host && !on) m_host->setPublicListing(std::nullopt);
}

void SessionManager::setSettingsLocked(bool on) {
    Mod::get()->setSavedValue<bool>("room-settings-locked", on);
    if (m_host) m_host->setSettingsLocked(on);
}
bool SessionManager::settingsLocked() const { return Mod::get()->getSavedValue<bool>("room-settings-locked", false); }

void SessionManager::setMaxPlayers(int n) {
    n = std::clamp(n, 1, static_cast<int>(session::SessionHost::kMaxPeers));
    Mod::get()->setSavedValue<int64_t>("room-max-players", n);
    if (m_host) m_host->setMaxPlayers(static_cast<std::size_t>(n));
    m_lastListing = {};
}
int SessionManager::maxPlayers() const {
    return static_cast<int>(std::clamp<int64_t>(Mod::get()->getSavedValue<int64_t>("room-max-players", 16), 1, 16));
}

bool SessionManager::ban(sync::PeerId peer) { return m_host && m_host->ban(peer); }

std::size_t SessionManager::undoPlayer(sync::PeerId peer) {
    if (!m_host) return 0;
    std::vector<sync::NetId> ids;
    for (const auto& [id, _] : m_host->state().objects()) {
        if (!sync::isLevelSettings(id) && id.peer() == peer) ids.push_back(id);
    }
    for (auto id : ids) m_host->submitLocal(sync::Op::erase(id));
    if (m_editor && !ids.empty()) m_editor->applyRemote(ids);  // the host's own editor follows
    m_roomDirty = true;
    return ids.size();
}

// ---------------------------------------------------------------- credits
std::vector<std::pair<std::string, std::size_t>> SessionManager::credits() const {
    std::map<sync::PeerId, std::size_t> count;
    if (const auto* room = view()) {
        for (const auto& [id, _] : room->objects()) {
            if (!sync::isLevelSettings(id)) ++count[id.peer()];
        }
    }
    std::vector<std::pair<std::string, std::size_t>> out;
    for (auto& [peer, n] : count) out.emplace_back(peerName(peer), n);
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.second > b.second; });
    return out;
}

std::string SessionManager::creditsText() const {
    std::string s = "Made together with NexLevel:";
    for (auto& [name, n] : credits()) s += "\n- " + name + " (" + std::to_string(n) + " objects)";
    return s;
}

// ---------------------------------------------------------------- markers
void SessionManager::sendMarker() {
    if (m_host) m_host->sendMarker(m_localX, m_localY);
    else if (m_client) m_client->sendMarker(m_localX, m_localY);
    if (inSession()) {
        m_markers.push_back({selfId().value_or(0), m_localX, m_localY, std::chrono::steady_clock::now()});
        notify("Marked this spot for everyone", NotificationIcon::Success);
    }
}

// ---------------------------------------------------------------- public rooms
std::string SessionManager::codeFor(const PublicRoomEntry& room) const { return session::encodeRoomCode(room.address); }

void SessionManager::refreshPublicRooms() {
    if (m_publicLoading) return;
    m_publicLoading = true;
    std::thread([relays = relayDirectory()] {
        std::vector<PublicRoomEntry> found;
        for (const auto& r : relays) {
            for (auto& pr : relay::listPublicRooms(r, std::chrono::milliseconds(2500))) {
                PublicRoomEntry e;
                e.relayId = r.id;
                e.address.relayRoom = pr.roomId;
                e.address.relayId = r.id;
                e.address.key = pr.roomKey;
                e.host = pr.host;
                e.level = pr.level;
                e.players = pr.players;
                e.maxPlayers = pr.maxPlayers;
                found.push_back(std::move(e));
            }
        }
        geode::queueInMainThread([found = std::move(found)]() mutable {
            auto* sm = SessionManager::get();
            sm->m_publicRooms = std::move(found);
            sm->m_publicLoading = false;
        });
    }).detach();
}

void SessionManager::featuresTick() {
    cleanupTempLevel();
    // Lock saving for the level open in a player's editor (joined from the editor or the main menu).
    if (m_client && m_editor && !m_noSaveLevel) {
        m_noSaveLevel = m_editor->level();
        if (m_noSaveLevel) m_noSaveLevel->retain();
    }
    const bool active = inSession();
    setMarker(active);
    if (!active) {
        m_wasViewOnly = false;
        return;
    }

    for (auto& p : peers()) m_knownNames[p.id] = p.name;  // credits keep names of players who left

    // Markers from the others: shown 8 seconds in the editor, and a notice.
    std::vector<session::Marker> incoming;
    if (m_host) incoming = m_host->takeMarkers();
    else if (m_client) incoming = m_client->takeMarkers();
    const auto nowT = std::chrono::steady_clock::now();
    for (auto& mk : incoming) {
        m_markers.push_back({mk.peer, mk.x, mk.y, nowT});
        std::string plain;
        for (char c : peerName(mk.peer)) plain += static_cast<unsigned char>(c) < 128 ? c : '?';
        notify(plain + " marked a spot - look for the circle", NotificationIcon::Info);
    }
    std::erase_if(m_markers, [&](auto& m) { return nowT - m.at > std::chrono::seconds(8); });

    // Public listing on the relay (host), refreshed every few seconds with the player count.
    if (m_host && m_public && nowT - m_lastListing > std::chrono::seconds(5)) {
        m_lastListing = nowT;
        std::string level = m_roomLevel ? std::string(m_roomLevel->m_levelName) : std::string("Level");
        const auto others = static_cast<std::uint8_t>(std::min<std::size_t>(playerCount(), 255));
        m_host->setPublicListing(relay::encodeListing(true, static_cast<std::uint8_t>(others + 1),
                                                      static_cast<std::uint8_t>(maxPlayers() + 1), m_host->roomKey(),
                                                      displayName(), level));
    }

    // Chat.
    std::vector<session::ChatMessage> lines;
    if (m_host) lines = m_host->takeChat();
    else if (m_client) lines = m_client->takeChat();
    const auto self = selfId();
    for (auto& l : lines) {
        ChatLine line{l.peer, peerName(l.peer), l.text};
        if (!m_chatOpen && (!self || l.peer != *self)) {
            // Notices use GD's bitmap font: keep plain ASCII there (the Chat tab shows the full text).
            std::string plain;
            for (char c : line.name + ": " + line.text) plain += static_cast<unsigned char>(c) < 128 ? c : '?';
            notify(plain, NotificationIcon::None);
        }
        m_chatLog.push_back(std::move(line));
        ++m_chatTotal;
        while (m_chatLog.size() > kChatKeep) m_chatLog.pop_front();
    }

    // Told once when the host turns editing off/on for this player.
    if (m_client && self) {
        const bool viewOnly = isViewOnly(*self);
        if (viewOnly != m_wasViewOnly) {
            m_wasViewOnly = viewOnly;
            notify(viewOnly ? "The host turned off editing for you - you can still watch" : "You can edit again",
                   viewOnly ? NotificationIcon::Warning : NotificationIcon::Success);
        }
    }
}

void clearSessionMarker() { setMarker(false); }

} // namespace nexlevel::mod

// At the first main menu after a start: if GD closed during a room last time, offer to report it.
class $modify(NLCrashPromptMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        static bool checked = false;
        if (checked) return true;
        checked = true;
        // Temporary room levels left behind by a crash: remove them.
        if (auto* levels = LocalLevelManager::sharedState()->m_localLevels) {
            std::vector<GJGameLevel*> leftovers;
            for (auto* l : CCArrayExt<GJGameLevel*>(levels)) {
                if (l && std::string(l->m_levelDesc) == "NexLevel room (temporary)") leftovers.push_back(l);
            }
            for (auto* l : leftovers) GameLevelManager::sharedState()->deleteLevel(l);
            if (!leftovers.empty()) LocalLevelManager::sharedState()->save();
        }
        std::error_code ec;
        const auto marker = Mod::get()->getSaveDir() / "in-room.marker";
        if (!std::filesystem::exists(marker, ec)) return true;
        std::filesystem::remove(marker, ec);
        geode::queueInMainThread([] {
            geode::createQuickPopup(
                "NexLevel",
                "Geometry Dash <cr>closed unexpectedly</c> during a NexLevel room last time.\n"
                "Help fix it: send the newest file from the <cy>logs</c> folder with a short description.",
                "Open logs", "Report", 360.f,
                [](FLAlertLayer*, bool report) {
                    if (report) {
                        web::openLinkInBrowser("https://github.com/MrPiatto/NexLevel/issues/new");
                    }
                    utils::file::openFolder(dirs::getGeodeLogDir());
                },
                true, true);
        });
        return true;
    }
};

// Collaboration extras: room chat, view-only players and the "GD closed during a room" report prompt.
#include "session_manager.hpp"

#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/web.hpp>

#include <filesystem>
#include <fstream>

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

void SessionManager::featuresTick() {
    const bool active = inSession();
    setMarker(active);
    if (!active) {
        m_wasViewOnly = false;
        return;
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

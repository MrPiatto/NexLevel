// Autosave: nobody loses work in a NexLevel room.
//  - Host: the room is written into the level it was opened from every kInterval (when something
//    changed), when the host leaves the editor, when the room closes and when GD saves/quits. This also
//    works while the host is in the menus and the others keep building (the level string is rebuilt
//    from the shared room state).
//  - Player: the room is copied into a local backup level "NL - <host>", so a player keeps a copy even
//    if the host disappears. The player's own level is never touched by autosave.
#include "session_manager.hpp"

#include <Geode/modify/AppDelegate.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include <nexlevel/core/sync/level_settings.hpp>
#include <nexlevel/core/sync/level_string.hpp>

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {

constexpr auto kInterval = std::chrono::seconds(90);
bool g_saving = false;

std::string plainLevelString(const gd::string& stored) {
    std::string s = stored;
    if (s.rfind("H4sI", 0) == 0) return std::string(ZipUtils::decompressString(stored, false, 0));
    return s;
}

} // namespace

std::size_t SessionManager::playerCount() const {
    auto list = peers();
    auto self = selfId();
    std::size_t n = 0;
    for (auto& p : list) {
        if (!self || p.id != *self) ++n;
    }
    return n;
}

GJGameLevel* SessionManager::backupLevel() {
    if (m_backupLevel) return m_backupLevel;
    if (!m_client || !m_lastJoined) return nullptr;
    const auto& list = m_client->peers();
    std::string hostName = list.empty() ? std::string("room") : list.front().name;
    std::string name = ("NL - " + hostName).substr(0, 20);
    // Reuse the backup of an earlier session with the same host instead of piling up copies.
    if (auto* levels = LocalLevelManager::sharedState()->m_localLevels) {
        for (auto* l : CCArrayExt<GJGameLevel*>(levels)) {
            if (l && std::string(l->m_levelName) == name) { m_backupLevel = l; break; }
        }
    }
    if (!m_backupLevel) {
        m_backupLevel = GameLevelManager::sharedState()->createNewLevel();
        if (!m_backupLevel) return nullptr;
        m_backupLevel->m_levelName = name;
        m_backupLevel->m_levelDesc = "NexLevel backup of a collab room";
        log::info("NexLevel: created backup level '{}'", name);
    }
    m_backupLevel->retain();
    return m_backupLevel;
}

void SessionManager::saveNow(bool toDisk, const char* reason) {
    if (g_saving) return;
    g_saving = true;
    struct Reset { ~Reset() { g_saving = false; } } reset;

    const sync::Replica* room = view();
    GJGameLevel* target = nullptr;
    if (m_host) target = m_roomLevel;
    else if (m_client && m_lastJoined) target = backupLevel();
    if (!room || !target) return;

    std::string str;
    auto* editor = m_host && m_editor ? LevelEditorLayer::get() : nullptr;
    if (editor && editor->m_level == target) {
        str = editor->getLevelString();  // the host's editor is the room, with every local setting
    } else {
        auto before = plainLevelString(target->m_levelString);
        str = sync::buildLevelString(*room, sync::levelStringHeader(before));
        if (const auto* settings = room->find(sync::kLevelSettingsId)) {
            auto song = sync::propsToLevelSettings(settings->props).song;
            target->m_audioTrack = song.audioTrack;
            target->m_songID = song.songID;
            target->m_songIDs = song.songIDs;
            target->m_sfxIDs = song.sfxIDs;
        }
    }
    if (str.size() <= 1) return;  // nothing to save (never overwrite a level with an empty one)
    target->m_levelString = ZipUtils::compressString(str, false, 0);
    target->m_isVerified = 0;  // content changed: has to be verified again before uploading
    target->m_isVerifiedRaw = false;
    if (toDisk) LocalLevelManager::sharedState()->save();
    m_roomDirty = false;
    m_lastSave = std::chrono::steady_clock::now();
    log::info("NexLevel: autosaved '{}' ({}, {} bytes)", std::string(target->m_levelName), reason, str.size());
}

void SessionManager::autosaveTick() {
    if (!m_roomDirty || !inSession()) return;
    if (std::chrono::steady_clock::now() - m_lastSave < kInterval) return;
    saveNow(true, "autosave");
}

} // namespace nexlevel::mod

using nexlevel::mod::SessionManager;

// GD saves the game when it closes, is minimised on some platforms, or the player saves manually:
// put the room into the level first so the save file has the latest version.
class $modify(NLAppDelegate, AppDelegate) {
    void trySaveGame(bool force) {
        auto* sm = SessionManager::get();
        if (sm->inSession()) sm->saveNow(false, "game save");
        AppDelegate::trySaveGame(force);
    }
};

// Closing GD while hosting ends the room for everyone: say so before quitting.
class $modify(NLMenuLayer, MenuLayer) {
    void onQuit(CCObject* sender) {
        auto* sm = SessionManager::get();
        if (!sm->hosting()) {
            MenuLayer::onQuit(sender);
            return;
        }
        const auto others = sm->playerCount();
        std::string body = "You are <cg>hosting a NexLevel room</c>";
        if (others > 0) body += " with <cy>" + std::to_string(others) + (others == 1 ? " player" : " players") + "</c>";
        body += ".\nClosing Geometry Dash <cr>ends the room for everyone</c>.\nThe level is saved first, nothing is lost.";
        geode::createQuickPopup("Stop hosting?", body, "Cancel", "Close GD", [](FLAlertLayer*, bool close) {
            if (!close) return;
            auto* sm = SessionManager::get();
            sm->saveNow(true, "closing the game");
            sm->leave();
            if (auto* menu = MenuLayer::get()) menu->endGame();
        });
    }
};

#pragma once
// Owns the NexLevel session (host room or client connection) for the whole game, not just the editor.
// The host's room stays open when the host leaves the editor; players keep building and the host keeps
// sequencing their ops. Pumped every frame by the cocos2d scheduler on the main thread.
#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>

#include <nexlevel/core/session/client.hpp>
#include <nexlevel/core/session/host.hpp>
#include <nexlevel/core/net/upnp.hpp>

#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nexlevel::mod {

inline constexpr std::uint16_t kDefaultPort = 47777;

enum class Role { None, Host, Client };

// Implemented by the editor adapter of the active editor.
class IEditorBridge {
public:
    virtual ~IEditorBridge() = default;
    virtual void applyRemote(const std::vector<sync::NetId>& ids) = 0;  // apply session view to these objects
    virtual void rebuildFromSession() = 0;                             // replace editor content with session view
    virtual void adoptFromSession() = 0;                               // editor loaded the room's level string: pair objects
    virtual void seedSession() = 0;                                    // host: publish current editor content
    virtual GJGameLevel* level() const = 0;
    virtual void switchLevel(GJGameLevel* level) = 0;                  // editor now saves into `level`
    virtual std::string levelString() const = 0;                       // current editor content
};

class SessionManager : public cocos2d::CCObject {
public:
    static SessionManager* get();

    Role role() const;
    bool inSession() const;  // host room open, or client joined

    // Needs an attached editor. onCopy: the room runs on a new local copy of the open level
    // ("<name> (NL)"), the original stays exactly as it was; otherwise on the open level itself.
    bool hostRoom(std::string* error, bool onCopy = false);
    bool join(const std::string& roomCode, std::string* error = nullptr);  // false: cannot join (error says why)
    // From the main menu: joins, then makes one local level "NL - <host>" holding the room
    // and opens the editor on it. The player's own levels are never touched.
    bool joinFromMenu(const std::string& roomCode, std::string* error = nullptr);
    void leave();                               // client: leave; host: close the room
    bool kick(sync::PeerId peer);

    std::vector<session::PeerInfo> peers() const;
    std::optional<sync::PeerId> selfId() const;
    // Host only: what players type to join. nullopt while the router port is still being opened
    // (a few seconds), so nobody copies a code that only works on the local network.
    std::optional<std::string> roomCode() const;
    std::string onlineText() const;  // host only: who can join (internet / same network only)
    std::string statusText() const;
    std::string displayName() const;

    // Editor presence: where this player is looking, and where the others are.
    void setLocalPresence(float x, float y, float zoom);
    std::vector<session::Presence> presences() const;
    std::string peerName(sync::PeerId peer) const;

    // Playtest spectating.
    void setLocalPlayerState(const session::PlayerState& state);
    const std::map<sync::PeerId, session::PlayTrail>& playTrails() const;

    // Ping and connection details (Collab menu, Info screen, editor ping label).
    std::optional<int> pingOf(sync::PeerId peer) const;  // ms to the host; nullopt = not measured yet
    std::uint32_t accountOf(sync::PeerId peer) const;     // GD account id, 0 = none
    struct ConnectionInfo {
        std::string role;          // "Host" / "Player"
        std::string via;           // "Relay" / "Direct" / "Local network" / "Router (UPnP)"
        std::string localIp;       // this PC on the local network
        std::string publicIp;      // empty if unknown
        std::uint16_t port = 0;    // host: room port; player: port connected to
        session::PingStats ping;   // player: to the host; host: to its relay
        std::string pingSubject;   // whose ping `ping` is
    };
    ConnectionInfo connectionInfo() const;
    int roomSeconds() const;          // since the room was opened / joined
    std::size_t objectCount() const;  // objects in the shared level

    // Editor plumbing.
    void attachEditor(IEditorBridge* bridge);
    // True only for the editor that is linked to the room (another level's editor opened while
    // hosting is NOT: nothing from it may reach the room).
    bool isAttached(const IEditorBridge* bridge) const { return bridge && bridge == m_editor && inSession(); }
    void detachEditor(IEditorBridge* bridge);
    bool isRoomLevel(GJGameLevel* level) const;  // host: the level the room was opened from
    // Called by LevelEditorLayer::init before GD reads the level: the host's room level gets the
    // latest room state written into it, so the editor opens it directly (fast) and only pairs objects.
    void beforeEditorLoads(GJGameLevel* level);
    std::optional<sync::NetId> newId();
    void submitLocal(sync::Op op);
    const sync::Replica* view() const;

    // Room chat (features.cpp). Lines are kept for the Chat tab; new ones from others pop up as notices.
    struct ChatLine {
        sync::PeerId peer = 0;
        std::string name;
        std::string text;
    };
    void sendChat(const std::string& text);
    const std::deque<ChatLine>& chatLog() const { return m_chatLog; }
    std::size_t chatCount() const { return m_chatTotal; }
    void setChatOpen(bool open) { m_chatOpen = open; }

    // Players can't save someone else's room: locked for the level open in a player's editor while in
    // the room and until that editor closes (GD's Save does nothing, the original level stays as it was).
    bool saveLocked(GJGameLevel* level) const { return level && level == m_noSaveLevel; }
    void editorClosed();

    // Permissions (host: turn editing off for a player; everyone: see who is view-only).
    void setViewOnly(sync::PeerId peer, bool on);
    bool isViewOnly(sync::PeerId peer) const;

    // Autosave (autosave.cpp). The host's room is written into the level it was opened from, also
    // while the host is outside the editor; a player's copy goes into the level open in their editor.
    // toDisk: also write the save file (CCLocalLevels.dat) right away.
    void saveNow(bool toDisk, const char* reason);
    bool hosting() const { return m_host != nullptr; }
    std::size_t playerCount() const;  // others in the room (not counting this player)

private:
    void autosaveTick();
    void featuresTick();
    std::deque<ChatLine> m_chatLog;
    std::size_t m_chatTotal = 0;
    bool m_chatOpen = false;
    bool m_wasViewOnly = false;
    bool makeHostCopy(std::string* error);
    void ensureScheduled();
    void tick(float dt);
    void releaseMapper();
    void startBestRelay();
    void endClient(const std::string& message, geode::NotificationIcon icon);

    std::unique_ptr<session::SessionHost> m_host;
    std::unique_ptr<session::SessionClient> m_client;
    std::unique_ptr<net::PortMapper> m_mapper;  // host: opens the router port automatically (UPnP)
    session::ClientState m_lastClientState = session::ClientState::Idle;
    IEditorBridge* m_editor = nullptr;
    GJGameLevel* m_roomLevel = nullptr;  // retained while the room is open
    bool m_scheduled = false;
    bool m_lastJoined = false;
    std::chrono::steady_clock::time_point m_roomStarted{};
    bool m_relayProbing = false;
    std::uint64_t m_roomGeneration = 0;
    std::vector<session::PeerInfo> m_lastPeers;
    GJGameLevel* m_tempLevel = nullptr;  // player: level made to open a room from the main menu (deleted after)
    GJGameLevel* m_noSaveLevel = nullptr;  // player: level whose editor shows someone's room (saving locked)
    void cleanupTempLevel();
    void afterClientSession();
    bool m_roomDirty = false;              // room changed since the last autosave
    std::chrono::steady_clock::time_point m_lastSave{};
    float m_badgeTimer = 0.f;
    bool m_adoptNext = false;        // the next editor to attach has the room's level string loaded
    bool m_menuJoinPending = false;  // joined from the main menu: open the editor once the level arrived
    void openMenuJoinEditor();
};

void notify(const std::string& text, geode::NotificationIcon icon = geode::NotificationIcon::Info);
// GD is closing normally: forget the "in a room" marker used to detect unexpected closes.
void clearSessionMarker();

} // namespace nexlevel::mod

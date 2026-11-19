#pragma once
// Owns the NexLevel session (host room or client connection) for the whole game, not just the editor.
// The host's room stays open when the host leaves the editor; players keep building and the host keeps
// sequencing their ops. Pumped every frame by the cocos2d scheduler on the main thread.
#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>

#include <nexlevel/core/session/client.hpp>
#include <nexlevel/core/session/host.hpp>
#include <nexlevel/core/net/upnp.hpp>
#include <nexlevel/core/session/room_code.hpp>

#include <chrono>
#include <deque>
#include <filesystem>
#include <set>
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
    std::string joinStatusText() const;  // for the loading screen while joining ("" = nothing to say)
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
    void setChatOpen(bool open) {
        m_chatOpen = open;
        if (open) m_chatUnread = 0;
    }
    std::size_t chatUnread() const { return m_chatUnread; }

    // Risky-action alerts, shown to everyone else in the room (mass delete, copy attempts, level
    // settings changes, undoing a player's work). Mass deletes and settings changes are noticed
    // from this player's own ops (submitLocal); the others are sent where they happen.
    void sendAlert(session::AlertKind kind, std::uint32_t count = 0, sync::PeerId subject = 0);

    // Players can't save someone else's room: locked for the level open in a player's editor while in
    // the room and until that editor closes (GD's Save does nothing, the original level stays as it was).
    bool saveLocked(GJGameLevel* level) const { return level && level == m_noSaveLevel; }
    void editorClosed();

    // Host room settings (saved for next time): public listing, settings lock, max players, bans.
    // Room list visibility: Hidden (code only), Private (listed, players ask to join), Public.
    enum class Visibility { Hidden = 0, Private = 1, Public = 2 };
    void setVisibility(Visibility v);
    Visibility visibility() const { return m_visibility; }
    bool isPublic() const { return m_visibility == Visibility::Public; }
    void setSettingsLocked(bool on);
    bool settingsLocked() const;
    void setMaxPlayers(int n);
    int maxPlayers() const;
    bool ban(sync::PeerId peer);
    // Bans are kept between rooms (saved), and can be undone.
    using BanEntry = session::SessionHost::BanEntry;
    std::vector<BanEntry> savedBans() const;
    void unban(const BanEntry& entry);

    // History: who placed / deleted / edited what, a line every few seconds per player (extras.cpp).
    struct HistoryLine {
        std::string time;
        sync::PeerId peer = 0;
        std::string name;
        std::string text;
    };
    const std::deque<HistoryLine>& history() const { return m_history; }
    std::size_t historyCount() const { return m_historyTotal; }
    // Chat: "/mute Name", "/unmute Name" (local only) and "@Name" mentions.
    bool isMuted(const std::string& name) const;
    // Host restore points: a copy of the room every 5 minutes in the mod's folder (newest first).
    struct RestorePoint {
        std::filesystem::path file;
        std::string label;
        long long time = 0;
    };
    std::vector<RestorePoint> restorePoints() const;
    bool openRestorePoint(const RestorePoint& point, std::string* name);  // as a new level in My Levels
    // Host: remove every object this player placed (still in the level). Returns how many.
    std::size_t undoPlayer(sync::PeerId peer);

    // Credits: objects in the level per player (who placed them).
    std::vector<std::pair<std::string, std::size_t>> credits() const;
    std::string creditsText() const;

    // "Look here" markers and following another player's view.
    void sendMarker();
    struct MarkerShown {
        sync::PeerId peer = 0;
        float x = 0, y = 0;
        std::chrono::steady_clock::time_point at{};
    };
    const std::vector<MarkerShown>& markers() const { return m_markers; }
    void setFollow(std::optional<sync::PeerId> peer) { m_follow = peer; }
    std::optional<sync::PeerId> following() const { return m_follow; }

    // Public rooms on the official relays (main menu).
    struct PublicRoomEntry {
        std::uint8_t relayId = 0;
        session::RoomAddress address;
        std::string host, level;
        int players = 0, maxPlayers = 0;
        bool privateRoom = false;
        int protocol = 0;  // 0 = older NexLevel
    };
    void refreshPublicRooms();
    const std::vector<PublicRoomEntry>& publicRooms() const { return m_publicRooms; }
    bool publicRoomsLoading() const { return m_publicLoading; }
    std::string codeFor(const PublicRoomEntry& room) const;
    // Private room: ask its host through the relay; joins by itself when accepted (notices otherwise).
    void askToJoin(const PublicRoomEntry& room, bool fromEditor);
    bool askPending() const { return m_askPending; }

    // Permissions (host: turn editing off for a player; everyone: see who is view-only).
    void setViewOnly(sync::PeerId peer, bool on);
    bool isViewOnly(sync::PeerId peer) const;

    // Autosave (autosave.cpp). The host's room is written into the level it was opened from, also
    // while the host is outside the editor; a player's copy goes into the level open in their editor.
    // toDisk: also write the save file (CCLocalLevels.dat) right away.
    void saveNow(bool toDisk, const char* reason);
    // Closing the game while in a room: save into the level and close the room, quietly.
    void shutdownForQuit();
    bool quitting() const { return m_quitting; }
    bool hosting() const { return m_host != nullptr; }
    std::size_t playerCount() const;  // others in the room (not counting this player)

private:
    void autosaveTick();
    void featuresTick();
    std::deque<ChatLine> m_chatLog;
    bool m_quitting = false;
    std::size_t m_chatUnread = 0;
    std::size_t m_eraseBurst = 0;
    std::chrono::steady_clock::time_point m_lastErase{};
    std::chrono::steady_clock::time_point m_lastSettingsAlert{};
    void alertsTick();
    void extrasTick();
    void sessionSummary();
    void snapshotIfDue(const std::string& compressedLevel);
    void persistBans(const std::vector<BanEntry>& list);
    bool chatCommand(const std::string& text);
    bool mentionsMe(const std::string& text) const;
    std::deque<HistoryLine> m_history;
    std::size_t m_historyTotal = 0;
    std::chrono::steady_clock::time_point m_lastHistory{};
    std::chrono::steady_clock::time_point m_lastSnapshot{};
    std::map<sync::PeerId, std::size_t> m_sessionPlaced;
    std::set<std::string> m_muted;
    Visibility m_visibility = Visibility::Private;
    // Join requests for this host's private room, shown one at a time.
    std::deque<session::SessionHost::JoinRequest> m_joinQueue;
    bool m_joinPanelOpen = false;
    bool m_askPending = false;
    void joinRequestsTick();
    float m_localX = 0, m_localY = 0;
    std::vector<MarkerShown> m_markers;
    std::optional<sync::PeerId> m_follow;
    std::vector<PublicRoomEntry> m_publicRooms;
    bool m_publicLoading = false;
    std::map<sync::PeerId, std::string> m_knownNames;
    std::chrono::steady_clock::time_point m_lastListing{};
    void applyHostSettings();
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
bool editorInUse();  // editor_tracker.cpp
// GD is closing normally: forget the "in a room" marker used to detect unexpected closes.
void clearSessionMarker();

} // namespace nexlevel::mod

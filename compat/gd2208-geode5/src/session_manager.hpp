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
        session::PingStats ping;   // player: to the host; host: the slowest player
        std::string pingSubject;   // whose ping `ping` is
    };
    ConnectionInfo connectionInfo() const;
    int roomSeconds() const;          // since the room was opened / joined
    std::size_t objectCount() const;  // objects in the shared level

    // Editor plumbing.
    void attachEditor(IEditorBridge* bridge);
    void detachEditor(IEditorBridge* bridge);
    bool isRoomLevel(GJGameLevel* level) const;  // host: the level the room was opened from
    std::optional<sync::NetId> newId();
    void submitLocal(sync::Op op);
    const sync::Replica* view() const;

    // Autosave (autosave.cpp). The host's room is written into the level it was opened from, also
    // while the host is outside the editor; a player's copy goes to a local "NL - <host>" backup level.
    // toDisk: also write the save file (CCLocalLevels.dat) right away.
    void saveNow(bool toDisk, const char* reason);
    bool hosting() const { return m_host != nullptr; }
    std::size_t playerCount() const;  // others in the room (not counting this player)

private:
    void autosaveTick();
    bool makeHostCopy(std::string* error);
    GJGameLevel* backupLevel();
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
    GJGameLevel* m_backupLevel = nullptr;  // player: retained while in the room
    bool m_roomDirty = false;              // room changed since the last autosave
    std::chrono::steady_clock::time_point m_lastSave{};
    float m_badgeTimer = 0.f;
};

void notify(const std::string& text, geode::NotificationIcon icon = geode::NotificationIcon::Info);

} // namespace nexlevel::mod

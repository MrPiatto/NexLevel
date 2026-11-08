#pragma once
// Owns the NexLevel session (host room or client connection) for the whole game, not just the editor.
// The host's room stays open when the host leaves the editor; players keep building and the host keeps
// sequencing their ops. Pumped every frame by the cocos2d scheduler on the main thread.
#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>

#include <nexlevel/core/session/client.hpp>
#include <nexlevel/core/session/host.hpp>

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
};

class SessionManager : public cocos2d::CCObject {
public:
    static SessionManager* get();

    Role role() const;
    bool inSession() const;  // host room open, or client joined

    bool hostRoom(std::string* error);          // needs an attached editor
    bool join(const std::string& roomCode);     // false: not a valid code
    void leave();                               // client: leave; host: close the room
    bool kick(sync::PeerId peer);

    std::vector<session::PeerInfo> peers() const;
    std::optional<sync::PeerId> selfId() const;
    std::optional<std::string> roomCode() const;     // host only: what players type to join
    std::string statusText() const;
    std::string displayName() const;

    // Editor plumbing.
    void attachEditor(IEditorBridge* bridge);
    void detachEditor(IEditorBridge* bridge);
    bool isRoomLevel(GJGameLevel* level) const;  // host: the level the room was opened from
    std::optional<sync::NetId> newId();
    void submitLocal(sync::Op op);
    const sync::Replica* view() const;

private:
    void ensureScheduled();
    void tick(float dt);
    void endClient(const std::string& message, geode::NotificationIcon icon);

    std::unique_ptr<session::SessionHost> m_host;
    std::unique_ptr<session::SessionClient> m_client;
    session::ClientState m_lastClientState = session::ClientState::Idle;
    IEditorBridge* m_editor = nullptr;
    GJGameLevel* m_roomLevel = nullptr;  // retained while the room is open
    bool m_scheduled = false;
    bool m_lastJoined = false;
    std::vector<session::PeerInfo> m_lastPeers;
};

void notify(const std::string& text, geode::NotificationIcon icon = geode::NotificationIcon::Info);

} // namespace nexlevel::mod

#pragma once
// The NexLevel bar at the top of the editor: a small tab that drops down a strip with the room
// status and quick buttons (menu, players, chat, look here). Replaces the old button in the Esc menu.
#include <Geode/Geode.hpp>

namespace nexlevel::mod {

// Once per editor frame. Hidden while this player is playtesting.
void updateEditorBar(EditorUI* ui, bool playtesting);

// The chat key (setting "chat-key") pressed in the editor: opens the room chat. True if handled.
bool editorBarKey(cocos2d::enumKeyCodes key);

} // namespace nexlevel::mod

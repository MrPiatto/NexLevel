#pragma once
#include <Geode/Geode.hpp>

namespace nexlevel::mod {

// Small "Ping 32 ms" label in the editor while in a room (setting "show-ping").
void updatePingHud(cocos2d::CCNode* editorUI);

// Who placed the single selected object (0 = nothing selected / not a room object). Set by the tracker.
void setSelectionCreator(std::uint16_t peer);

} // namespace nexlevel::mod

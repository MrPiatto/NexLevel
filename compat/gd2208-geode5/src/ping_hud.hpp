#pragma once
#include <Geode/Geode.hpp>

namespace nexlevel::mod {

// Small "Ping 32 ms" label in the editor while in a room (setting "show-ping").
void updatePingHud(cocos2d::CCNode* editorUI);

} // namespace nexlevel::mod

#pragma once
// Colours shared by the NexLevel menu, the editor overlay and the ping label.
#include <Geode/Geode.hpp>

#include <nexlevel/core/session/ping.hpp>

#include <string>

namespace nexlevel::mod::ui {

inline const cocos2d::ccColor3B kPurpleSoft{190, 140, 255};
inline const cocos2d::ccColor3B kGrey{170, 160, 190};

// Each player's colour, the same everywhere: avatar, view frame, playtest icon and trail.
cocos2d::ccColor3B peerColor(std::uint16_t peer);
cocos2d::ccColor3B pingColor(int ms);
cocos2d::ccColor3B qualityColor(session::Quality q);

} // namespace nexlevel::mod::ui

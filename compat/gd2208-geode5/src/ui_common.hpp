#pragma once
// Shared look of the NexLevel menus (purple / black).
#include <Geode/Geode.hpp>

#include <nexlevel/core/session/ping.hpp>

#include <string>

namespace nexlevel::mod::ui {

inline const cocos2d::ccColor3B kPurple{150, 70, 255};
inline const cocos2d::ccColor3B kPurpleSoft{190, 140, 255};
inline const cocos2d::ccColor3B kPurpleDark{90, 40, 160};
inline const cocos2d::ccColor3B kPanel{14, 6, 28};
inline const cocos2d::ccColor3B kGrey{170, 160, 190};
inline const cocos2d::ccColor3B kRed{220, 50, 90};

cocos2d::CCLabelBMFont* label(const std::string& text, const char* font, float scale, cocos2d::ccColor3B color = {255, 255, 255});
CCMenuItemSpriteExtra* button(const char* caption, cocos2d::ccColor3B color, float scale, cocos2d::CCObject* target, cocos2d::SEL_MenuHandler sel);
// A sprite from GD's sheets, or nullptr if this GD build does not have it (never crash on a name).
cocos2d::CCSprite* optionalSprite(const char* frameName);

cocos2d::ccColor3B pingColor(int ms);
cocos2d::ccColor3B qualityColor(session::Quality q);
const char* qualityIcon(session::Quality q);  // GD difficulty face frame name, may be missing

} // namespace nexlevel::mod::ui

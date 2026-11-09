#include "ui_common.hpp"

using namespace geode::prelude;

namespace nexlevel::mod::ui {

CCLabelBMFont* label(const std::string& text, const char* font, float scale, ccColor3B color) {
    auto* l = CCLabelBMFont::create(text.c_str(), font);
    l->setScale(scale);
    l->setColor(color);
    return l;
}

CCMenuItemSpriteExtra* button(const char* caption, ccColor3B color, float scale, CCObject* target, SEL_MenuHandler sel) {
    auto* spr = ButtonSprite::create(caption, "bigFont.fnt", "GJ_button_04.png", scale);
    spr->m_BGSprite->setColor(color);
    return CCMenuItemSpriteExtra::create(spr, target, sel);
}

CCSprite* optionalSprite(const char* frameName) {
    if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frameName)) return nullptr;
    return CCSprite::createWithSpriteFrameName(frameName);
}

ccColor3B peerColor(std::uint16_t peer) {
    static const ccColor3B colors[] = {
        {140, 90, 255}, {20, 200, 180}, {255, 110, 200}, {90, 230, 150}, {255, 200, 80}, {255, 120, 120}, {120, 160, 255}, {200, 255, 110},
    };
    return colors[peer % (sizeof(colors) / sizeof(colors[0]))];
}

ccColor3B pingColor(int ms) {
    if (ms < 70) return {110, 255, 140};
    if (ms < 150) return {255, 220, 90};
    return {255, 90, 110};
}

ccColor3B qualityColor(session::Quality q) {
    switch (q) {
        case session::Quality::Demon: return {255, 70, 90};
        case session::Quality::Insane: return {255, 120, 220};
        case session::Quality::High: return {255, 170, 60};
        case session::Quality::Normal: return {90, 220, 255};
        case session::Quality::Bad: return {150, 150, 150};
        case session::Quality::Unknown: break;
    }
    return {170, 160, 190};
}

const char* qualityIcon(session::Quality q) {
    switch (q) {
        case session::Quality::Demon: return "diffIcon_06_btn_001.png";
        case session::Quality::Insane: return "diffIcon_05_btn_001.png";
        case session::Quality::High: return "diffIcon_04_btn_001.png";
        case session::Quality::Normal: return "diffIcon_02_btn_001.png";
        case session::Quality::Bad: return "diffIcon_01_btn_001.png";
        case session::Quality::Unknown: break;
    }
    return "diffIcon_00_btn_001.png";
}

} // namespace nexlevel::mod::ui

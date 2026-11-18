#include "ping_hud.hpp"

#include "session_manager.hpp"
#include "ui_common.hpp"

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {
constexpr int kHudTag = 0x4E5850;  // "NXP"
std::uint16_t g_selectionCreator = 0;
}

void setSelectionCreator(std::uint16_t peer) { g_selectionCreator = peer; }

void updatePingHud(CCNode* editorUI) {
    if (!editorUI) return;
    auto* sm = SessionManager::get();
    auto* hud = static_cast<CCLabelBMFont*>(editorUI->getChildByTag(kHudTag));
    const bool show = sm->inSession() && Mod::get()->getSettingValue<bool>("show-ping");
    if (!show) {
        if (hud) hud->setVisible(false);
        return;
    }
    if (!hud) {
        hud = CCLabelBMFont::create("", "bigFont.fnt");
        hud->setTag(kHudTag);
        hud->setID("ping-label"_spr);
        hud->setScale(.3f);
        hud->setAnchorPoint({.5f, 1.f});
        const auto win = CCDirector::sharedDirector()->getWinSize();
        hud->setPosition({win.width / 2.f, win.height - 34.f});
        editorUI->addChild(hud, 1000);
    }
    hud->setVisible(true);
    std::string text;
    ccColor3B color = ui::kGrey;
    if (sm->role() == Role::Host) {
        const auto n = sm->peers().size();
        text = "Hosting - " + std::to_string(n) + (n == 1 ? " player" : " players");
        color = ui::kPurpleSoft;
    } else if (auto self = sm->selfId()) {
        if (auto ms = sm->pingOf(*self)) {
            text = "Ping " + std::to_string(*ms) + " ms";
            color = ui::pingColor(*ms);
        } else {
            text = "Ping ...";
        }
    }
    if (g_selectionCreator != 0) text += "   |   Placed by " + sm->peerName(g_selectionCreator);
    if (sm->selfId() && sm->isViewOnly(*sm->selfId())) {
        text = "View only - " + text;
        color = ui::kGrey;
    }
    if (hud->getString() != text) hud->setString(text.c_str());
    hud->setColor(color);
}

} // namespace nexlevel::mod

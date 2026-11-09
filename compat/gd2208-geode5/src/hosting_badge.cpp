#include "hosting_badge.hpp"

#include "session_manager.hpp"
#include "ui_kit.hpp"

#include <Geode/ui/OverlayManager.hpp>

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {

constexpr int kBadgeTag = 0x4E584C42;  // "NXLB"
constexpr float kW = 254.f;
constexpr float kH = 18.f;

CCNode* findBadge() { return OverlayManager::get()->getChildByTag(kBadgeTag); }

CCNode* createBadge() {
    auto* badge = CCNode::create();
    badge->setTag(kBadgeTag);
    badge->setID("hosting-badge"_spr);
    badge->setContentSize({kW, kH});
    badge->setAnchorPoint({0.5f, 0.f});
    badge->setZOrder(1000);
    auto* bg = kit::card({kW, kH}, kH / 2.f, kit::rgba(14, 16, 34, 0.86f), kit::rgba(kit::kTeal, 0.7f), 1.f);
    bg->setPosition({kW / 2.f, kH / 2.f});
    badge->addChild(bg);
    auto* dot = CCDrawNode::create();
    kit::fillCircle(dot, {11.f, kH / 2.f}, 3.5f, kit::rgba(kit::kGood));
    dot->setTag(1);
    badge->addChild(dot);
    auto* label = kit::text("", 8.f, kit::kText, 1);
    label->setAnchorPoint({0.f, 0.5f});
    label->setPosition({20.f, kH / 2.f});
    label->setTag(2);
    badge->addChild(label);
    OverlayManager::get()->addChild(badge);
    return badge;
}

} // namespace

void updateHostingBadge() {
    auto* sm = SessionManager::get();
    // The editor has its own room label (ping HUD); the badge is for everywhere else.
    const bool show = sm->hosting() && !LevelEditorLayer::get();
    auto* badge = findBadge();
    if (!show) {
        if (badge) badge->setVisible(false);
        return;
    }
    if (!badge) badge = createBadge();
    auto win = CCDirector::sharedDirector()->getWinSize();
    badge->setPosition({win.width / 2.f, 3.f});
    badge->setVisible(true);
    const auto n = sm->playerCount();
    std::string s = "Hosting NexLevel room - " + std::to_string(n) + (n == 1 ? " player" : " players") +
                    " - keep GD open";
    if (auto* label = static_cast<CCLabelTTF*>(badge->getChildByTag(2))) kit::setTextIfChanged(label, s);
    // Gentle pulse on the dot so it reads as "live".
    if (auto* dot = badge->getChildByTag(1)) {
        dot->setVisible(!dot->isVisible() || n > 0);
    }
}

} // namespace nexlevel::mod

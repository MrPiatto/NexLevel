// cocos2d APIs: CCLabelTTF (wrapped text), CCDrawNode, CCMoveTo/CCFadeOut/CCSequence actions.
// Geode: OverlayManager (a node drawn above every scene), queueInMainThread.
// GD (bindings 2.2081): FMODAudioEngine::playEffect(path, speed, unknown, volume).
#include "toast.hpp"

#include "ui_kit.hpp"

#include <Geode/ui/OverlayManager.hpp>

#include <algorithm>
#include <vector>

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {

constexpr float kWidth = 168.f;
constexpr float kMargin = 6.f;
constexpr float kGap = 4.f;
constexpr std::size_t kMaxShown = 4;
constexpr float kStay = 4.5f;

ccColor3B accentFor(ToastKind kind) {
    switch (kind) {
        case ToastKind::Success: return kit::kGood;
        case ToastKind::Warning: return kit::kWarn;
        case ToastKind::Error: return kit::kDanger;
        case ToastKind::Chat: return kit::kTeal;
        case ToastKind::Info: break;
    }
    return kit::kViolet;
}

const char* soundFor(ToastKind kind) {
    switch (kind) {
        case ToastKind::Warning:
        case ToastKind::Error: return "chestClick.ogg";
        default: return "counter003.ogg";
    }
}

// Holds the cards; lives in the OverlayManager so it stays on screen when the scene changes.
class ToastStack : public CCNode {
public:
    static ToastStack* get() {
        auto* overlay = OverlayManager::get();
        if (!overlay) return nullptr;
        if (auto* existing = typeinfo_cast<ToastStack*>(overlay->getChildByID("toasts"_spr))) return existing;
        auto* stack = new ToastStack();
        if (!stack->init()) {
            delete stack;
            return nullptr;
        }
        stack->autorelease();
        stack->setID("toasts"_spr);
        overlay->addChild(stack, 1000);
        return stack;
    }

    void push(const std::string& title, const std::string& text, ToastKind kind) {
        const auto win = CCDirector::sharedDirector()->getWinSize();
        auto* card = CCNode::create();

        auto* titleLabel = kit::text(title, 8.f, kit::kText, 1);
        CCLabelTTF* body = nullptr;
        float h = 12.f + titleLabel->getContentSize().height * titleLabel->getScale();
        if (!text.empty()) {
            body = kit::wrappedText(text, 6.5f, kWidth - 18.f, kit::kMuted);
            h += body->getContentSize().height + 1.f;
        }
        h = std::max(h, 24.f);

        const auto accent = accentFor(kind);
        auto* bg = CCDrawNode::create();
        auto pts = kit::roundedRectPoints(kWidth, h, 6.f, 6);
        bg->drawPolygon(pts.data(), static_cast<unsigned>(pts.size()), kit::rgba(14, 12, 30, .94f), 1.1f, kit::rgba(kit::kViolet, 1.f));
        // Small accent bar on the left in the kind's colour.
        CCPoint bar[4] = {{5.f, 5.f}, {7.f, 5.f}, {7.f, h - 5.f}, {5.f, h - 5.f}};
        bg->drawPolygon(bar, 4, kit::rgba(accent, 1.f), 0.f, kit::rgba(accent, 0.f));
        card->addChild(bg);
        card->setContentSize({kWidth, h});

        titleLabel->setAnchorPoint({0.f, 1.f});
        titleLabel->setPosition({12.f, h - 5.f});
        card->addChild(titleLabel);
        if (body) {
            body->setAnchorPoint({0.f, 1.f});
            body->setPosition({12.f, h - 6.f - titleLabel->getContentSize().height});
            card->addChild(body);
        }

        this->addChild(card);
        m_cards.insert(m_cards.begin(), card);
        while (m_cards.size() > kMaxShown) {
            m_cards.back()->removeFromParent();
            m_cards.pop_back();
        }
        // Slide in from the right, wait, fade, then go.
        card->setPosition({win.width + 4.f, win.height});
        card->runAction(CCSequence::create(CCDelayTime::create(kStay), CCCallFuncN::create(this, callfuncN_selector(ToastStack::dismiss)),
                                           nullptr));
        layout(true);

        if (Mod::get()->getSettingValue<bool>("toast-sound")) {
            if (auto* audio = FMODAudioEngine::sharedEngine()) audio->playEffect(soundFor(kind), 1.f, 0.f, .45f);
        }
    }

private:
    std::vector<CCNode*> m_cards;  // newest first; owned as children

    void layout(bool animate) {
        const auto win = CCDirector::sharedDirector()->getWinSize();
        float y = win.height - kMargin;
        for (auto* card : m_cards) {
            const auto size = card->getContentSize();
            const CCPoint target{win.width - kMargin - size.width, y - size.height};
            card->stopActionByTag(7);
            if (animate) {
                auto* move = CCEaseOut::create(CCMoveTo::create(.18f, target), 2.f);
                move->setTag(7);
                card->runAction(move);
            } else {
                card->setPosition(target);
            }
            y -= size.height + kGap;
        }
    }

    void dismiss(CCNode* card) {
        auto it = std::find(m_cards.begin(), m_cards.end(), card);
        if (it == m_cards.end()) return;
        m_cards.erase(it);
        const auto win = CCDirector::sharedDirector()->getWinSize();
        card->runAction(CCSequence::create(CCEaseIn::create(CCMoveTo::create(.18f, {win.width + 4.f, card->getPositionY()}), 2.f),
                                           CCCallFunc::create(card, callfunc_selector(CCNode::removeFromParent)), nullptr));
        layout(true);
    }
};

} // namespace

void toast(const std::string& title, const std::string& text, ToastKind kind) {
    log::info("NexLevel notice: {}{}{}", title, text.empty() ? "" : " - ", text);
    // Next frame, and only with a running scene: notices can come while cocos2d tears a scene
    // down (e.g. quitting the game), where adding nodes is not safe.
    geode::queueInMainThread([title, text, kind] {
        if (!CCDirector::sharedDirector()->getRunningScene()) return;
        if (auto* stack = ToastStack::get()) stack->push(title, text, kind);
    });
}

} // namespace nexlevel::mod

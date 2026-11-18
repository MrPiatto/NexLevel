// cocos2d APIs: CCLayerColor, CCSprite, CCDrawNode, CCMoveTo/CCEaseOut actions, CCTouchDispatcher
// (swallows touches while shown). Geode: OverlayManager (drawn above every scene).
#include "loading_screen.hpp"

#include "session_manager.hpp"
#include "ui_kit.hpp"

#include <Geode/ui/OverlayManager.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <cmath>

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {

constexpr float kMinSeconds = 5.f;
constexpr float kMaxSeconds = 120.f;  // safety net: never stay stuck on the screen
constexpr float kSlide = .5f;
constexpr float kFeather = 110.f;  // soft edge on both sides, so the moving screen has no hard border

// "Whoosh" with the slide (resources/sounds), at the notification-sound setting.
void whoosh(const char* file) {
    if (!Mod::get()->getSettingValue<bool>("toast-sound")) return;
    std::error_code ec;
    auto dir = Mod::get()->getResourcesDir();
    auto path = dir / file;
    if (!std::filesystem::exists(path, ec)) path = dir / (std::string(GEODE_MOD_ID) + "." + file);
    if (!std::filesystem::exists(path, ec)) return;
    if (auto* audio = FMODAudioEngine::sharedEngine()) audio->playEffect(geode::utils::string::pathToString(path), 1.f, 0.f, .7f);
}

class JoinLoading : public CCLayerColor {
public:
    static JoinLoading* create() {
        auto* ret = new JoinLoading();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    static JoinLoading* current() {
        auto* overlay = OverlayManager::get();
        return overlay ? typeinfo_cast<JoinLoading*>(overlay->getChildByID("join-loading"_spr)) : nullptr;
    }

    void markReady() { m_ready = true; }

    void leave() {
        if (m_leaving) return;
        m_leaving = true;
        whoosh("whoosh_out.ogg");
        const auto win = CCDirector::sharedDirector()->getWinSize();
        this->stopAllActions();
        this->runAction(CCSequence::create(CCEaseSineIn::create(CCMoveTo::create(kSlide, {-win.width - kFeather, 0.f})),
                                           CCCallFunc::create(this, callfunc_selector(JoinLoading::removeFromParent)), nullptr));
    }

    bool init() override {
        if (!CCLayerColor::initWithColor({0, 0, 0, 255})) return false;
        const auto win = CCDirector::sharedDirector()->getWinSize();
        this->setContentSize(win);
        // Soft edges just outside the screen-sized black area: transparent -> black on the left,
        // black -> transparent on the right. Off screen while it is still, visible while it slides.
        if (auto* left = CCLayerGradient::create({0, 0, 0, 0}, {0, 0, 0, 255}, {1.f, 0.f})) {
            left->setContentSize({kFeather, win.height});
            left->setPosition({-kFeather, 0.f});
            this->addChild(left, -1);
        }
        if (auto* right = CCLayerGradient::create({0, 0, 0, 255}, {0, 0, 0, 0}, {1.f, 0.f})) {
            right->setContentSize({kFeather, win.height});
            right->setPosition({win.width, 0.f});
            this->addChild(right, -1);
        }

        m_logo = CCSprite::create("wordmark.png"_spr);
        if (m_logo && m_logo->getContentSize().width > 0.f) {
            m_logo->setScale(win.width * .58f / m_logo->getContentSize().width);
            m_logo->setPosition({win.width / 2.f, win.height * .58f});
            this->addChild(m_logo);
            m_logoScale = m_logo->getScale();
        } else {
            auto* name = kit::text("NEXLEVEL", 34.f, kit::kText, 1);
            name->setPosition({win.width / 2.f, win.height * .58f});
            this->addChild(name);
            m_logo = nullptr;
        }
        m_status = kit::text("Connecting to the room...", 9.f, kit::kMuted, 1);
        m_status->setPosition({win.width / 2.f, win.height * .34f});
        this->addChild(m_status);
        m_bar = CCDrawNode::create();
        this->addChild(m_bar);
        auto* tagline = kit::text("Create Together. Build Beyond.", 7.f, {90, 80, 120});
        tagline->setPosition({win.width / 2.f, 14.f});
        this->addChild(tagline);

        // Slide in from the right.
        whoosh("whoosh_in.ogg");
        this->setPosition({win.width + kFeather, 0.f});
        this->runAction(CCEaseSineOut::create(CCMoveTo::create(kSlide, {0.f, 0.f})));
        this->setTouchEnabled(true);
        this->scheduleUpdate();
        m_start = std::chrono::steady_clock::now();
        return true;
    }

    // Nothing under the loading screen can be touched while it is up.
    void registerWithTouchDispatcher() override {
        CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -10000, true);
    }
    bool ccTouchBegan(CCTouch*, CCEvent*) override { return true; }

    void update(float) override {
        const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_start).count();
        auto* sm = SessionManager::get();
        if (!m_leaving) {
            const auto text = sm->joinStatusText();
            if (!text.empty()) kit::setTextIfChanged(m_status, text);
            if ((m_ready && t >= kMinSeconds) || t >= kMaxSeconds) leave();
        }
        // Logo breathing, and a violet segment running along the bar.
        if (m_logo) m_logo->setScale(m_logoScale * (1.f + .015f * std::sin(t * 2.6f)));
        const auto win = CCDirector::sharedDirector()->getWinSize();
        const float w = 200.f, y = win.height * .27f, x0 = win.width / 2.f - w / 2.f;
        m_bar->clear();
        m_bar->drawSegment({x0, y}, {x0 + w, y}, 1.5f, kit::rgba(40, 34, 70, 1.f));
        const float head = std::fmod(t * .7f, 1.4f) - .2f;  // -0.2 .. 1.2
        const float a = std::clamp(head - .25f, 0.f, 1.f), b = std::clamp(head, 0.f, 1.f);
        if (b > a) m_bar->drawSegment({x0 + w * a, y}, {x0 + w * b, y}, 1.5f, kit::rgba(kit::kViolet, 1.f));
    }

private:
    CCSprite* m_logo = nullptr;
    float m_logoScale = 1.f;
    CCLabelTTF* m_status = nullptr;
    CCDrawNode* m_bar = nullptr;
    bool m_ready = false;
    bool m_leaving = false;
    std::chrono::steady_clock::time_point m_start{};
};

} // namespace

void showJoinLoading() {
    auto* overlay = OverlayManager::get();
    if (!overlay) return;
    if (auto* old = JoinLoading::current()) old->removeFromParent();
    if (auto* screen = JoinLoading::create()) {
        screen->setID("join-loading"_spr);
        overlay->addChild(screen, 900);  // under NexLevel's notifications (1000)
    }
}

void finishJoinLoading() {
    if (auto* screen = JoinLoading::current()) screen->markReady();
}

void cancelJoinLoading() {
    if (auto* screen = JoinLoading::current()) screen->leave();
}

} // namespace nexlevel::mod

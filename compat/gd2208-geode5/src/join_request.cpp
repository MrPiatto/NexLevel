// cocos2d APIs: CCDrawNode (warning triangle), CCLabelTTF. Geode: Popup, Anchor layout.
#include "join_request.hpp"

#include "ui_kit.hpp"

#include <utility>

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {

constexpr float kW = 300.f, kH = 168.f;

class JoinRequestPanel : public geode::Popup {
public:
    static JoinRequestPanel* create(const std::string& name, std::function<void(bool)> answer) {
        auto* ret = new JoinRequestPanel();
        ret->m_name = name;
        ret->m_answer = std::move(answer);
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

private:
    std::string m_name;
    std::function<void(bool)> m_answer;

    bool init() override {
        if (!Popup::init(kW, kH, "square02b_001.png", {0, 0, 80, 80})) return false;
        m_bgSprite->setVisible(false);
        if (m_closeBtn) m_closeBtn->setVisible(false);
        auto* panel = kit::card({kW, kH}, 14.f, kit::rgba(16, 18, 38, .97f), kit::rgba(kit::kViolet, 1.f), 1.1f);
        m_mainLayer->addChildAtPosition(panel, Anchor::Center);
        panel->setZOrder(-10);

        m_mainLayer->addChildAtPosition(kit::text("Join request", 14.f, kit::kText, 1), Anchor::Top, {0.f, -22.f});
        auto* who = kit::text(m_name, 11.f, kit::kTeal, 1);
        m_mainLayer->addChildAtPosition(who, Anchor::Top, {0.f, -46.f});
        m_mainLayer->addChildAtPosition(kit::text("wants to join your room", 8.f, kit::kMuted), Anchor::Top, {0.f, -62.f});

        // Warning box: a yellow triangle with "!" and the advice.
        auto* box = kit::card({kW - 30.f, 34.f}, 9.f, kit::rgba(60, 46, 10, .55f), kit::rgba(kit::kWarn, .8f), .8f);
        m_mainLayer->addChildAtPosition(box, Anchor::Center, {0.f, -10.f});
        if (auto* warnIcon = kit::icon("warning", 18.f, kit::kWarn)) {
            m_mainLayer->addChildAtPosition(warnIcon, Anchor::Center, {-kW / 2.f + 32.f, -10.f});
        }
        auto* warn = kit::wrappedText("Only accept people you know. Don't share your code with strangers.", 7.f, kW - 80.f, kit::kWarn);
        warn->setAnchorPoint({0.f, .5f});
        m_mainLayer->addChildAtPosition(warn, Anchor::Center, {-kW / 2.f + 48.f, -10.f});

        auto* decline = kit::button("Decline", {110.f, 24.f}, kit::Style::Secondary, this, menu_selector(JoinRequestPanel::onDecline), 9.f);
        m_buttonMenu->addChildAtPosition(decline, Anchor::Bottom, {-62.f, 22.f});
        auto* accept = kit::button("Accept", {110.f, 24.f}, kit::Style::Primary, this, menu_selector(JoinRequestPanel::onAccept), 9.f);
        m_buttonMenu->addChildAtPosition(accept, Anchor::Bottom, {62.f, 22.f});
        return true;
    }

    void finish(bool accept) {
        if (auto answer = std::exchange(m_answer, nullptr)) answer(accept);
    }
    void onAccept(CCObject*) {
        finish(true);
        this->onClose(nullptr);
    }
    void onDecline(CCObject*) {
        finish(false);
        this->onClose(nullptr);
    }
    void onExit() override {
        finish(false);  // closed any other way (Esc, scene change): not accepted
        Popup::onExit();
    }
};

} // namespace

void showJoinRequestPanel(const std::string& name, std::function<void(bool)> answer) {
    if (auto* panel = JoinRequestPanel::create(name, std::move(answer))) panel->show();
    else answer(false);
}

} // namespace nexlevel::mod

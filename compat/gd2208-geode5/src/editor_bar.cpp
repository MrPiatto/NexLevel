// cocos2d APIs: CCMenu, CCMoveTo/CCEaseOut actions, CCDrawNode. GD: CCMenuItemSpriteExtra.
#include "editor_bar.hpp"

#include "nexmenu.hpp"
#include "session_manager.hpp"
#include "toast.hpp"
#include "ui_common.hpp"
#include "ui_kit.hpp"

#include <cctype>
#include <string>

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {

constexpr float kTabW = 96.f, kTabH = 15.f;
constexpr float kPanelW = 330.f, kPanelH = 34.f;

void openMenu(NexMenu::Tab tab) {
    if (auto* menu = NexMenu::create(true)) {
        menu->show();
        menu->openTab(tab);
    }
}

class EditorBar : public CCNode {
public:
    static EditorBar* create() {
        auto* ret = new EditorBar();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init() override {
        if (!CCNode::init()) return false;
        const auto win = CCDirector::sharedDirector()->getWinSize();

        // Drop-down strip (built when opened), starts above the screen.
        m_panel = CCNode::create();
        m_panel->setPosition({win.width / 2.f, win.height + kPanelH});
        m_panel->setVisible(false);
        this->addChild(m_panel, 0);

        m_tabMenu = CCMenu::create();
        m_tabMenu->setPosition({0.f, 0.f});
        this->addChild(m_tabMenu, 1);
        buildTab();
        return true;
    }

    void refreshBar(bool playtesting) {
        this->setVisible(!playtesting);
        if (playtesting && m_open) close(false);
        auto* sm = SessionManager::get();
        const std::string sig = std::to_string(static_cast<int>(sm->role())) + "|" + std::to_string(sm->chatUnread()) + "|" +
                                std::to_string(sm->playerCount()) + "|" + (m_open ? "o" : "c");
        if (sig != m_sig) {
            m_sig = sig;
            buildTab();
            if (m_open) buildPanel();
        }
        if (m_open && m_statusLabel) kit::setTextIfChanged(m_statusLabel, statusText());
    }

private:
    CCMenu* m_tabMenu = nullptr;
    CCNode* m_panel = nullptr;
    CCLabelTTF* m_statusLabel = nullptr;
    bool m_open = false;
    std::string m_sig;

    static std::string statusText() {
        auto* sm = SessionManager::get();
        if (!sm->inSession()) return "Not in a room";
        const auto n = sm->playerCount();
        std::string s = sm->role() == Role::Host ? "Hosting" : "In " + sm->peerName(session::SessionHost::kHostPeer) + "'s room";
        s += "  -  " + std::to_string(n + 1) + (n == 0 ? " player" : " players");
        if (sm->role() == Role::Client) {
            if (auto self = sm->selfId()) {
                if (auto ms = sm->pingOf(*self)) s += "  -  " + std::to_string(*ms) + " ms";
            }
        }
        return s;
    }

    void buildTab() {
        m_tabMenu->removeAllChildren();
        auto* sm = SessionManager::get();
        const auto win = CCDirector::sharedDirector()->getWinSize();
        auto* tab = CCNode::create();
        tab->setContentSize({kTabW, kTabH});
        // Hangs from the top edge: rounded at the bottom (the top is off screen).
        auto* bg = kit::card({kTabW, kTabH + 8.f}, 7.f, kit::rgba(16, 14, 34, .92f), kit::rgba(kit::kViolet, 1.f), .9f);
        bg->setPosition({kTabW / 2.f, (kTabH + 8.f) / 2.f});
        tab->addChild(bg);
        auto* dot = CCDrawNode::create();
        kit::fillCircle(dot, {10.f, kTabH / 2.f}, 2.6f, kit::rgba(sm->inSession() ? kit::kGood : kit::kMuted, 1.f));
        tab->addChild(dot);
        auto* label = kit::text(std::string("NexLevel  ") + (m_open ? "^" : "v"), 7.f, kit::kText, 1);
        label->setPosition({kTabW / 2.f + 2.f, kTabH / 2.f});
        tab->addChild(label);
        if (const auto unread = sm->chatUnread(); unread > 0) {  // unread chat messages
            auto* badge = CCDrawNode::create();
            kit::fillCircle(badge, {kTabW - 9.f, kTabH / 2.f}, 5.5f, kit::rgba(kit::kTeal, 1.f));
            tab->addChild(badge);
            auto* n = kit::text(unread > 9 ? "9+" : std::to_string(unread), 5.5f, {10, 20, 30}, 1);
            n->setPosition({kTabW - 9.f, kTabH / 2.f});
            tab->addChild(n);
        }
        auto* item = CCMenuItemSpriteExtra::create(tab, this, menu_selector(EditorBar::onTab));
        item->m_scaleMultiplier = 1.04f;
        item->setPosition({win.width / 2.f, win.height - kTabH / 2.f});
        m_tabMenu->addChild(item);
    }

    void addPanelButton(CCMenu* menu, const std::string& caption, kit::Style style, SEL_MenuHandler sel, float x, int tag = 0) {
        auto* b = kit::button(caption, {54.f, 18.f}, style, this, sel, 7.f);
        b->setTag(tag);
        b->setPosition({x, 0.f});
        menu->addChild(b);
    }

    void buildPanel() {
        m_panel->removeAllChildren();
        auto* sm = SessionManager::get();
        auto* card = kit::card({kPanelW, kPanelH}, 9.f, kit::rgba(16, 14, 34, .95f), kit::rgba(kit::kViolet, 1.f), .9f);
        m_panel->addChild(card);
        m_statusLabel = kit::text(statusText(), 7.f, sm->inSession() ? kit::kText : kit::kMuted, 1);
        m_statusLabel->setAnchorPoint({0.f, .5f});
        m_statusLabel->setPosition({-kPanelW / 2.f + 10.f, 0.f});
        if (m_statusLabel->getContentSize().width > 120.f) m_statusLabel->setScale(120.f / m_statusLabel->getContentSize().width);
        m_panel->addChild(m_statusLabel);
        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        m_panel->addChild(menu);
        const float right = kPanelW / 2.f - 32.f;
        if (!sm->inSession()) {
            addPanelButton(menu, "Join", kit::Style::Secondary, menu_selector(EditorBar::onMenu), right - 58.f);
            addPanelButton(menu, "Host", kit::Style::Primary, menu_selector(EditorBar::onMenu), right);
            return;
        }
        const auto unread = sm->chatUnread();
        addPanelButton(menu, "Look here", kit::Style::Secondary, menu_selector(EditorBar::onLookHere), right - 174.f);
        addPanelButton(menu, "Players", kit::Style::Secondary, menu_selector(EditorBar::onPlayers), right - 116.f);
        addPanelButton(menu, unread ? "Chat (" + std::to_string(unread) + ")" : std::string("Chat"),
                       unread ? kit::Style::Primary : kit::Style::Secondary, menu_selector(EditorBar::onChat), right - 58.f);
        addPanelButton(menu, "Menu", kit::Style::Primary, menu_selector(EditorBar::onMenu), right);
    }

    void open() {
        const auto win = CCDirector::sharedDirector()->getWinSize();
        m_open = true;
        buildPanel();
        m_panel->setVisible(true);
        m_panel->stopAllActions();
        m_panel->runAction(CCEaseOut::create(CCMoveTo::create(.16f, {win.width / 2.f, win.height - kTabH - kPanelH / 2.f - 3.f}), 2.f));
        m_sig.clear();
    }

    void close(bool animate) {
        const auto win = CCDirector::sharedDirector()->getWinSize();
        m_open = false;
        m_panel->stopAllActions();
        const CCPoint hidden{win.width / 2.f, win.height + kPanelH};
        if (animate) {
            m_panel->runAction(CCSequence::create(CCEaseIn::create(CCMoveTo::create(.14f, hidden), 2.f), CCHide::create(), nullptr));
        } else {
            m_panel->setPosition(hidden);
            m_panel->setVisible(false);
        }
        m_sig.clear();
    }

    void onTab(CCObject*) {
        if (m_open) close(true);
        else open();
    }
    void onMenu(CCObject*) {
        close(true);
        openMenu(NexMenu::Tab::Room);
    }
    void onPlayers(CCObject*) {
        close(true);
        openMenu(NexMenu::Tab::Players);
    }
    void onChat(CCObject*) {
        close(true);
        openMenu(NexMenu::Tab::Chat);
    }
    void onLookHere(CCObject*) {
        close(true);
        SessionManager::get()->sendMarker();
    }
};

} // namespace

void updateEditorBar(EditorUI* ui, bool playtesting) {
    if (!ui) return;
    auto* bar = typeinfo_cast<EditorBar*>(ui->getChildByID("editor-bar"_spr));
    if (!bar) {
        bar = EditorBar::create();
        if (!bar) return;
        bar->setID("editor-bar"_spr);
        ui->addChild(bar, 1002);
    }
    bar->refreshBar(playtesting);
}

bool editorBarKey(enumKeyCodes key) {
    auto setting = Mod::get()->getSettingValue<std::string>("chat-key");
    if (setting.empty()) return false;
    const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(setting[0])));
    if (letter < 'A' || letter > 'Z' || static_cast<int>(key) != static_cast<int>(letter)) return false;
    if (auto* kb = CCKeyboardDispatcher::get(); kb && (kb->getControlKeyPressed() || kb->getAltKeyPressed() || kb->getShiftKeyPressed())) {
        return false;  // Ctrl/Alt/Shift + letter stay GD's shortcuts
    }
    auto* sm = SessionManager::get();
    if (!sm->inSession()) return false;  // the key does what it normally does outside a room
    openMenu(NexMenu::Tab::Chat);
    return true;
}

} // namespace nexlevel::mod

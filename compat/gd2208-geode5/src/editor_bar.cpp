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

constexpr float kTabW = 112.f, kTabH = 17.f;
constexpr float kPanelW = 330.f, kPanelH = 34.f;

// True if any popup (NexLevel's or GD's) is open over the editor.
bool popupOpen() {
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return true;
    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (typeinfo_cast<FLAlertLayer*>(child)) return true;
    }
    return false;
}

void openMenu(NexMenu::Tab tab) {
    if (popupOpen()) return;
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
        m_face = CCNode::create();
        m_face->setContentSize({kTabW, kTabH});
        auto* item = CCMenuItemSpriteExtra::create(m_face, this, menu_selector(EditorBar::onTab));
        item->m_scaleMultiplier = 1.04f;
        item->setPosition({win.width / 2.f, win.height - kTabH / 2.f});
        m_tabMenu->addChild(item);
        drawTab();
        return true;
    }

    void refreshBar(bool playtesting) {
        this->setVisible(!playtesting);
        if (playtesting && m_open) close(false);
        auto* sm = SessionManager::get();
        const std::string sig = std::to_string(static_cast<int>(sm->role())) + "|" + std::to_string(sm->chatUnread()) + "|" +
                                (m_open ? "o" : "c");
        if (sig != m_sig) {
            m_sig = sig;
            drawTab();  // picture only: the button itself stays
        }
        // Joined / left a room while the strip is open: close it (its buttons are rebuilt on the next open).
        if (m_open && sm->inSession() != m_panelInSession) close(true);
        if (m_open && m_statusLabel) kit::setTextIfChanged(m_statusLabel, statusText());
    }

private:
    CCMenu* m_tabMenu = nullptr;
    CCNode* m_face = nullptr;  // the tab's picture (redrawn), inside a button that is never replaced
    bool m_panelInSession = false;
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

    void drawTab() {
        m_face->removeAllChildren();  // pictures only, no buttons in here
        auto* sm = SessionManager::get();
        auto* tab = m_face;
        // Slanted like the logo, hanging from the top edge (its top is off screen).
        auto* bg = kit::buttonBackground({kTabW, kTabH + 8.f}, kit::Style::Secondary);
        bg->setPosition({kTabW / 2.f, (kTabH + 8.f) / 2.f});
        tab->addChild(bg);
        auto* marks = CCDrawNode::create();
        // In a room: green dot; not: grey.
        kit::fillCircle(marks, {14.f, kTabH / 2.f}, 2.6f, kit::rgba(sm->inSession() ? kit::kGood : kit::kMuted, 1.f));
        // Chevron: down when closed, up when open.
        const float cx = kTabW / 2.f + 30.f, cy = kTabH / 2.f, d = m_open ? -1.f : 1.f;
        const auto chev = kit::rgba(200, 180, 255, 1.f);
        marks->drawSegment({cx - 2.5f, cy + 1.2f * d}, {cx, cy - 1.3f * d}, .6f, chev);
        marks->drawSegment({cx, cy - 1.3f * d}, {cx + 2.5f, cy + 1.2f * d}, .6f, chev);
        tab->addChild(marks);
        if (auto* logo = CCSprite::create((std::string(GEODE_MOD_ID) + "/wordmark.png").c_str()); logo && logo->getContentSize().height > 0.f) {
            logo->setScale(12.f / logo->getContentSize().height);
            logo->setPosition({kTabW / 2.f - 4.f, kTabH / 2.f});
            tab->addChild(logo);
        } else {
            auto* label = kit::text("NexLevel", 7.f, kit::kText, 1);
            label->setPosition({kTabW / 2.f - 4.f, kTabH / 2.f});
            tab->addChild(label);
        }
        if (const auto unread = sm->chatUnread(); unread > 0) {  // unread chat messages
            auto* badge = CCDrawNode::create();
            kit::fillCircle(badge, {kTabW / 2.f + 44.f, kTabH / 2.f}, 5.f, kit::rgba(196, 60, 255, 1.f));
            tab->addChild(badge);
            auto* n = kit::text(unread > 9 ? "9+" : std::to_string(unread), 5.5f, {255, 255, 255}, 1);
            n->setPosition({kTabW / 2.f + 44.f, kTabH / 2.f});
            tab->addChild(n);
        }
    }

    void addPanelButton(CCMenu* menu, const std::string& caption, kit::Style style, SEL_MenuHandler sel, float x, int tag = 0) {
        auto* b = kit::button(caption, {54.f, 18.f}, style, this, sel, 7.f);
        b->setTag(tag);
        b->setPosition({x, 0.f});
        menu->addChild(b);
    }

    // Only called from open(), i.e. from the tab's own button (never while a strip button is pressed).
    void buildPanel() {
        m_panel->removeAllChildren();
        auto* sm = SessionManager::get();
        m_panelInSession = sm->inSession();
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
        addPanelButton(menu, "Look here", kit::Style::Secondary, menu_selector(EditorBar::onLookHere), right - 174.f);
        addPanelButton(menu, "Players", kit::Style::Secondary, menu_selector(EditorBar::onPlayers), right - 116.f);
        addPanelButton(menu, "Chat", kit::Style::Secondary, menu_selector(EditorBar::onChat), right - 58.f);
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

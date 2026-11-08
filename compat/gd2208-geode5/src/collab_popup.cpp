#include "collab_popup.hpp"

#include "session_manager.hpp"

#include <Geode/modify/EditorPauseLayer.hpp>

#include <cctype>

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {
const ccColor3B kPurple{150, 70, 255};
const ccColor3B kPurpleSoft{190, 140, 255};
const ccColor3B kPurpleDark{90, 40, 160};
const ccColor3B kPanel{14, 6, 28};
const ccColor3B kGrey{170, 160, 190};
const ccColor3B kRed{220, 50, 90};

CCLabelBMFont* label(const std::string& text, const char* font, float scale, ccColor3B color = {255, 255, 255}) {
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
} // namespace

CollabPopup* CollabPopup::create() {
    auto* ret = new CollabPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CollabPopup::init() {
    if (!Popup::init(390.f, 290.f, "square02b_001.png", {0, 0, 80, 80})) return false;

    m_bgSprite->setColor(kPanel);
    m_bgSprite->setOpacity(250);
    auto* frame = NineSlice::create("square02b_001.png", {0, 0, 80, 80});
    frame->setContentSize(m_size + CCSize(8.f, 8.f));
    frame->setColor(kPurple);
    m_mainLayer->addChildAtPosition(frame, Anchor::Center);
    frame->setZOrder(-10);
    m_bgSprite->setZOrder(-5);

    this->setTitle("NexLevel Collab", "bigFont.fnt", .75f, 22.f);
    if (m_title) m_title->setColor(kPurpleSoft);
    m_mainLayer->addChildAtPosition(label("by Forg3 Studios", "goldFont.fnt", .45f, kGrey), Anchor::Top, {0.f, -44.f});

    rebuild();
    this->schedule(schedule_selector(CollabPopup::refresh), .5f);
    return true;
}

std::string CollabPopup::signature() const {
    auto* sm = SessionManager::get();
    std::string sig = std::to_string(static_cast<int>(sm->role())) + "|" + sm->statusText() + "|" +
                      sm->roomCode().value_or("") + "|" + sm->onlineText();
    for (auto& p : sm->peers()) sig += "|" + std::to_string(p.id) + p.name;
    return sig;
}

void CollabPopup::refresh(float) {
    if (signature() != m_shown) rebuild();
}

void CollabPopup::rebuild() {
    std::string keptCode = m_codeInput ? std::string(m_codeInput->getString()) : Mod::get()->getSavedValue<std::string>("last-code", "");
    for (auto* n : m_dynamic) n->removeFromParent();
    m_dynamic.clear();
    m_codeInput = nullptr;
    m_shown = signature();

    auto* sm = SessionManager::get();
    const auto role = sm->role();

    m_mainLayer->addChildAtPosition(track(label(sm->statusText(), "bigFont.fnt", .45f, kPurpleSoft)), Anchor::Top, {0.f, -66.f});

    if (role == Role::None) {
        m_buttonMenu->addChildAtPosition(track(button("Host room", kPurple, .85f, this, menu_selector(CollabPopup::onHost))),
            Anchor::Center, {0.f, 42.f});
        m_mainLayer->addChildAtPosition(track(label("- or join a friend -", "chatFont.fnt", .65f, kGrey)),
            Anchor::Center, {0.f, 8.f});

        m_codeInput = track(TextInput::create(170.f, "Room code"));
        m_codeInput->setFilter("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ- ");
        m_codeInput->setMaxCharCount(20);
        m_codeInput->setString(keptCode);
        m_mainLayer->addChildAtPosition(m_codeInput, Anchor::Center, {-60.f, -26.f});
        m_buttonMenu->addChildAtPosition(track(button("Paste", kPurpleDark, .5f, this, menu_selector(CollabPopup::onPaste))),
            Anchor::Center, {64.f, -26.f});
        m_buttonMenu->addChildAtPosition(track(button("Join", kPurple, .7f, this, menu_selector(CollabPopup::onJoin))),
            Anchor::Center, {130.f, -26.f});

        m_mainLayer->addChildAtPosition(track(label("Joining replaces the level open in your editor", "chatFont.fnt", .55f, kRed)),
            Anchor::Center, {0.f, -58.f});
        m_mainLayer->addChildAtPosition(track(label("Everyone needs the same GD version", "chatFont.fnt", .55f, kGrey)),
            Anchor::Bottom, {0.f, 20.f});
        return;
    }

    if (role == Role::Host) {
        m_mainLayer->addChildAtPosition(track(label("ROOM CODE", "goldFont.fnt", .4f, kGrey)), Anchor::Top, {0.f, -84.f});
        if (auto code = sm->roomCode()) {
            const float scale = code->size() > 15 ? .52f : .6f;  // direct online codes are 2 characters longer
            m_mainLayer->addChildAtPosition(track(label(*code, "bigFont.fnt", scale, kPurpleSoft)), Anchor::Top, {-30.f, -104.f});
            m_buttonMenu->addChildAtPosition(track(button("Copy", kPurpleDark, .5f, this, menu_selector(CollabPopup::onCopy))),
                Anchor::Top, {125.f, -104.f});
        } else {
            m_mainLayer->addChildAtPosition(track(label("Preparing...", "bigFont.fnt", .5f, kGrey)), Anchor::Top, {0.f, -104.f});
        }
        const auto online = sm->onlineText();
        const bool good = online.rfind("Online", 0) == 0;
        m_mainLayer->addChildAtPosition(track(label(online, "chatFont.fnt", .5f, good ? kPurpleSoft : kGrey)), Anchor::Top, {0.f, -121.f});
    }

    // Players in the room.
    auto peers = sm->peers();
    auto self = sm->selfId();
    const std::size_t maxRows = 5;
    for (std::size_t i = 0; i < peers.size() && i < maxRows; ++i) {
        const auto& p = peers[i];
        float y = (role == Role::Host ? 8.f : 22.f) - static_cast<float>(i) * 24.f;
        std::string name = p.name;
        if (self && p.id == *self) name += " (you)";
        auto* nameLabel = track(label(name, "bigFont.fnt", .4f));
        nameLabel->setAnchorPoint({0.f, .5f});
        m_mainLayer->addChildAtPosition(nameLabel, Anchor::Center, {-160.f, y});
        if (p.id == session::SessionHost::kHostPeer) {
            m_mainLayer->addChildAtPosition(track(label("HOST", "goldFont.fnt", .45f, kPurpleSoft)), Anchor::Center, {95.f, y});
        } else if (role == Role::Host) {
            auto* kick = track(button("Kick", kRed, .5f, this, menu_selector(CollabPopup::onKick)));
            kick->setTag(p.id);
            m_buttonMenu->addChildAtPosition(kick, Anchor::Center, {140.f, y});
        }
    }
    if (peers.size() > maxRows) {
        m_mainLayer->addChildAtPosition(track(label("+" + std::to_string(peers.size() - maxRows) + " more", "chatFont.fnt", .6f, kGrey)),
            Anchor::Center, {0.f, (role == Role::Host ? 8.f : 22.f) - static_cast<float>(maxRows) * 24.f});
    }

    const char* leaveText = role == Role::Host ? "Close room" : "Leave";
    m_buttonMenu->addChildAtPosition(track(button(leaveText, kRed, .7f, this, menu_selector(CollabPopup::onLeave))),
        Anchor::Bottom, {0.f, 42.f});
    if (role == Role::Host) {
        m_mainLayer->addChildAtPosition(track(label("The room stays open if you leave the editor", "chatFont.fnt", .55f, kGrey)),
            Anchor::Bottom, {0.f, 16.f});
    }
}

void CollabPopup::onHost(CCObject*) {
    std::string err;
    if (SessionManager::get()->hostRoom(&err)) {
        notify("Room open - share the room code", NotificationIcon::Success);
    } else {
        notify(err, NotificationIcon::Error);
    }
    rebuild();
}

void CollabPopup::onJoin(CCObject*) {
    if (!m_codeInput) return;
    std::string code = m_codeInput->getString();
    if (code.empty()) {
        notify("Enter the room code", NotificationIcon::Warning);
        return;
    }
    std::string error;
    if (!SessionManager::get()->join(code, &error)) {
        notify(error, NotificationIcon::Error);
        return;
    }
    Mod::get()->setSavedValue<std::string>("last-code", code);
    rebuild();
}

void CollabPopup::onPaste(CCObject*) {
    if (!m_codeInput) return;
    std::string text = utils::clipboard::read();
    std::string cleaned;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') cleaned += c;
    }
    m_codeInput->setString(cleaned.substr(0, 20));
}

void CollabPopup::onCopy(CCObject*) {
    if (auto code = SessionManager::get()->roomCode()) {
        utils::clipboard::write(*code);
        notify("Room code copied", NotificationIcon::Success);
    }
}

void CollabPopup::onLeave(CCObject*) {
    if (SessionManager::get()->role() == Role::Host) {
        Ref<CollabPopup> self = this;
        createQuickPopup("Close room?", "Everyone in the room will be disconnected.", "Cancel", "Close",
            [self](FLAlertLayer*, bool close) {
                if (!close) return;
                SessionManager::get()->leave();
                self->rebuild();
            });
        return;
    }
    SessionManager::get()->leave();
    rebuild();
}

void CollabPopup::onKick(CCObject* sender) {
    auto peer = static_cast<sync::PeerId>(static_cast<CCNode*>(sender)->getTag());
    if (SessionManager::get()->kick(peer)) notify("Player removed", NotificationIcon::Info);
    rebuild();
}

} // namespace nexlevel::mod

// "Collab" button in the editor pause menu (Esc).
class $modify(NLEditorPauseLayer, EditorPauseLayer) {
    bool init(LevelEditorLayer* layer) {
        if (!EditorPauseLayer::init(layer)) return false;
        auto* spr = ButtonSprite::create("Collab", "bigFont.fnt", "GJ_button_04.png", .8f);
        spr->m_BGSprite->setColor({150, 70, 255});
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(NLEditorPauseLayer::onNexLevelCollab));
        auto* menu = CCMenu::create();
        menu->addChild(btn);
        menu->setID("nexlevel-collab-menu"_spr);
        auto win = CCDirector::sharedDirector()->getWinSize();
        menu->setPosition({win.width / 2.f, win.height - 28.f});
        this->addChild(menu, 10);
        return true;
    }

    void onNexLevelCollab(CCObject*) {
        if (auto* popup = nexlevel::mod::CollabPopup::create()) popup->show();
    }
};

#pragma once
// The NexLevel menu: one modern panel with tabs (Room / Players / Network / Settings).
// Opened from the editor pause menu. Structure is rebuilt only when it changes (role, players,
// tab); live values (pings, gauge, chart, uptime) update in place twice per second.
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include "ui_kit.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace nexlevel::mod {

class NexMenu : public geode::Popup {
public:
    enum class Tab { Room, Players, Chat, Network, Settings };
    // fromEditor = false: opened from the main menu. Hosting/joining needs the level's editor, so the
    // Room tab explains that instead of showing Host/Join (an open room is still shown and managed).
    static NexMenu* create(bool fromEditor = true);

protected:
    bool init();
    void buildTab();
    void refresh(float);
    void updateLive();
    std::string structure() const;

    // placement helpers (coordinates from the panel's bottom-left)
    template <class T>
    T* add(T* node, float x, float y) {
        m_mainLayer->addChildAtPosition(node, geode::Anchor::BottomLeft, {x, y});
        m_dynamic.push_back(node);
        return node;
    }
    template <class T>
    T* addButton(T* node, float x, float y) {
        m_buttonMenu->addChildAtPosition(node, geode::Anchor::BottomLeft, {x, y});
        m_dynamic.push_back(node);
        return node;
    }
    cocos2d::CCLabelTTF* addText(const std::string& s, float size, float x, float y, cocos2d::ccColor3B c = kit::kText,
                                 int weight = 0, cocos2d::CCPoint anchor = {0.f, .5f});

    void buildRoom();
    void buildPlayers();
    void buildNetwork();
    void buildSettings();
    void buildChat();
    void onExit() override;

    void onTab(cocos2d::CCObject* sender);
    void onClose2(cocos2d::CCObject*);
    void onHost(cocos2d::CCObject*);
    void onJoin(cocos2d::CCObject*);
    void onPaste(cocos2d::CCObject*);
    void onCopy(cocos2d::CCObject*);
    void onLeave(cocos2d::CCObject*);
    void onKick(cocos2d::CCObject* sender);
    void onProfile(cocos2d::CCObject* sender);
    void onToggleIp(cocos2d::CCObject*);
    void onToggleSetting(cocos2d::CCObject* sender);
    void onSaveRelay(cocos2d::CCObject*);
    void onSendChat(cocos2d::CCObject*);
    void onGoTo(cocos2d::CCObject* sender);
    void onToggleViewOnly(cocos2d::CCObject* sender);


    Tab m_tab = Tab::Room;
    bool m_fromEditor = true;
    std::vector<cocos2d::CCNode*> m_dynamic;
    std::string m_structure;
    bool m_showIp = false;

    // header
    cocos2d::CCLabelTTF* m_status = nullptr;
    cocos2d::CCDrawNode* m_statusDot = nullptr;

    // live widgets of the current tab (null when not shown)
    geode::TextInput* m_codeInput = nullptr;
    geode::TextInput* m_relayInput = nullptr;
    geode::TextInput* m_chatInput = nullptr;
    cocos2d::CCLabelTTF* m_codeText = nullptr;
    cocos2d::CCLabelTTF* m_onlineText = nullptr;
    cocos2d::CCLabelTTF* m_statPlayers = nullptr;
    cocos2d::CCLabelTTF* m_statObjects = nullptr;
    cocos2d::CCLabelTTF* m_statUptime = nullptr;
    struct PingRow {
        std::uint16_t peer;
        cocos2d::CCLabelTTF* label;
        kit::Meter* meter;
    };
    std::vector<PingRow> m_pingRows;
    kit::Gauge* m_gauge = nullptr;
    kit::Chart* m_chart = nullptr;
    cocos2d::CCLabelTTF* m_qualityText = nullptr;
    cocos2d::CCLabelTTF* m_tilePing = nullptr;
    cocos2d::CCLabelTTF* m_tileAvg = nullptr;
    cocos2d::CCLabelTTF* m_tileJitter = nullptr;
    cocos2d::CCLabelTTF* m_tileLoss = nullptr;
    cocos2d::CCLabelTTF* m_viaText = nullptr;
    cocos2d::CCLabelTTF* m_ipText = nullptr;
    cocos2d::CCLabelTTF* m_portText = nullptr;
};

} // namespace nexlevel::mod

#include "nexmenu.hpp"

#include "session_manager.hpp"
#include "ui_common.hpp"

#include <Geode/modify/EditorPauseLayer.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>

using namespace geode::prelude;
namespace kit = nexlevel::mod::kit;

namespace nexlevel::mod {

namespace {
constexpr float W = 460.f, H = 290.f;
constexpr float kContentTop = H - 84.f;  // below header + tab bar

const ccColor3B kPeerColors[] = {
    {140, 90, 255}, {20, 200, 180}, {255, 110, 200}, {90, 230, 150}, {255, 200, 80}, {255, 120, 120}, {120, 160, 255}, {200, 255, 110},
};
ccColor3B peerColor(std::uint16_t id) { return kPeerColors[id % (sizeof(kPeerColors) / sizeof(kPeerColors[0]))]; }

std::string msText(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.0f ms", v);
    return b;
}
std::string uptimeText(int s) {
    char b[32];
    if (s >= 3600) std::snprintf(b, sizeof b, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}
float qualityValue(session::Quality q) {
    switch (q) {
        case session::Quality::Demon: return 1.f;
        case session::Quality::Insane: return .8f;
        case session::Quality::High: return .6f;
        case session::Quality::Normal: return .4f;
        case session::Quality::Bad: return .2f;
        case session::Quality::Unknown: break;
    }
    return 0.f;
}
} // namespace

NexMenu* NexMenu::create() {
    auto* ret = new NexMenu();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

CCLabelTTF* NexMenu::addText(const std::string& s, float size, float x, float y, ccColor3B c, int weight, CCPoint anchor) {
    auto* l = kit::text(s, size, c, weight);
    l->setAnchorPoint(anchor);
    return add(l, x, y);
}

bool NexMenu::init() {
    if (!Popup::init(W, H, "square02b_001.png", {0, 0, 80, 80})) return false;
    // Our own panel instead of GD's square sprite and close button.
    m_bgSprite->setVisible(false);
    if (m_closeBtn) m_closeBtn->setVisible(false);
    auto* glow = kit::card({W + 6.f, H + 6.f}, 17.f, kit::rgba(kit::kViolet, .10f), kit::rgba(kit::kViolet, .0f), 0.f);
    m_mainLayer->addChildAtPosition(glow, Anchor::Center);
    glow->setZOrder(-12);
    auto* panel = kit::card({W, H}, 14.f, kit::rgba(16, 18, 38, .97f), kit::rgba(kit::kViolet, .5f), .7f);
    m_mainLayer->addChildAtPosition(panel, Anchor::Center);
    panel->setZOrder(-10);

    buildTab();
    this->schedule(schedule_selector(NexMenu::refresh), .5f);
    return true;
}

std::string NexMenu::structure() const {
    auto* sm = SessionManager::get();
    std::string s = std::to_string(static_cast<int>(m_tab)) + "|" + std::to_string(static_cast<int>(sm->role())) + "|" +
                    (sm->roomCode() ? "c" : "-") + "|" + (m_showIp ? "i" : "-");
    for (auto& p : sm->peers()) s += "|" + std::to_string(p.id) + p.name;
    return s;
}

void NexMenu::refresh(float) {
    if (structure() != m_structure) {
        buildTab();
        return;
    }
    updateLive();
}

void NexMenu::buildTab() {
    // Keep what the player typed across rebuilds.
    std::string typedCode = m_codeInput ? std::string(m_codeInput->getString()) : Mod::get()->getSavedValue<std::string>("last-code", "");
    for (auto* n : m_dynamic) n->removeFromParent();
    m_dynamic.clear();
    m_codeInput = m_relayInput = nullptr;
    m_codeText = m_onlineText = m_statPlayers = m_statObjects = m_statUptime = nullptr;
    m_pingRows.clear();
    m_gauge = nullptr;
    m_chart = nullptr;
    m_qualityText = m_tilePing = m_tileAvg = m_tileJitter = m_tileLoss = m_viaText = m_ipText = m_portText = nullptr;
    m_structure = structure();

    // ---- header ----
    addText("NEXLEVEL", 17.f, 22.f, H - 24.f, kit::kText, 1);
    addText("Create Together. Build Beyond.  -  Forg3 Studios", 7.f, 23.f, H - 41.f, kit::kMuted);
    m_statusDot = add(CCDrawNode::create(), W - 128.f, H - 25.f);
    m_status = addText("", 8.f, W - 120.f, H - 25.f, kit::kMuted, 1);
    addButton(kit::button("X", {20.f, 20.f}, kit::Style::Ghost, this, menu_selector(NexMenu::onClose2), 9.f), W - 24.f, H - 24.f);

    // ---- tab bar ----
    const char* names[] = {"Room", "Players", "Network", "Settings"};
    const float tabW = 96.f, barW = tabW * 4.f + 8.f;
    add(kit::card({barW, 26.f}, 13.f, kit::rgba(30, 34, 64, .9f), kit::rgba(kit::kViolet, .25f), .6f), W / 2.f, H - 64.f);
    for (int i = 0; i < 4; ++i) {
        const bool sel = static_cast<int>(m_tab) == i;
        auto* b = kit::button(names[i], {tabW - 4.f, 20.f}, sel ? kit::Style::Primary : kit::Style::Ghost, this,
                              menu_selector(NexMenu::onTab), 8.5f);
        if (!sel) {  // ghost tabs: text only, no outline
            if (auto* bg = b->getNormalImage()) {
                if (auto* children = bg->getChildren(); children && children->count() > 0) {
                    static_cast<CCNode*>(children->objectAtIndex(0))->setVisible(false);
                }
            }
        }
        b->setTag(i);
        addButton(b, W / 2.f - barW / 2.f + 4.f + tabW * (static_cast<float>(i) + .5f), H - 64.f);
    }

    switch (m_tab) {
        case Tab::Room: buildRoom(); break;
        case Tab::Players: buildPlayers(); break;
        case Tab::Network: buildNetwork(); break;
        case Tab::Settings: buildSettings(); break;
    }
    if (m_codeInput) m_codeInput->setString(typedCode);
    updateLive();
}

// ---------------------------------------------------------------- Room
void NexMenu::buildRoom() {
    auto* sm = SessionManager::get();
    const auto role = sm->role();
    const auto glass = kit::rgba(30, 34, 66, .85f);
    const auto edge = kit::rgba(kit::kViolet, .3f);

    if (role == Role::None) {
        // Host card
        add(kit::card({206.f, 176.f}, 12.f, glass, edge, .8f), 16.f + 103.f, 22.f + 88.f);
        addText("Host a room", 12.f, 30.f, 180.f, kit::kText, 1);
        addText("Open this level to your friends.", 7.5f, 30.f, 162.f, kit::kMuted);
        addText("They join with a short code,", 7.5f, 30.f, 151.f, kit::kMuted);
        addText("no IP, no router settings.", 7.5f, 30.f, 140.f, kit::kMuted);
        addButton(kit::button("Host room", {150.f, 28.f}, kit::Style::Primary, this, menu_selector(NexMenu::onHost), 10.f),
                  16.f + 103.f, 92.f);
        addText("Up to 16 players - encrypted", 7.f, 16.f + 103.f, 60.f, kit::kMuted, 0, {.5f, .5f});

        // Join card
        add(kit::card({206.f, 176.f}, 12.f, glass, edge, .8f), 238.f + 103.f, 22.f + 88.f);
        addText("Join a room", 12.f, 252.f, 180.f, kit::kText, 1);
        addText("Paste the code your friend sent you.", 7.5f, 252.f, 162.f, kit::kMuted);
        add(kit::card({178.f, 24.f}, 8.f, kit::rgba(12, 14, 30, 1.f), kit::rgba(kit::kTeal, .45f), .7f), 238.f + 103.f, 132.f);
        m_codeInput = TextInput::create(170.f, "ROOM CODE", "chatFont.fnt");
        m_codeInput->hideBG();
        m_codeInput->setFilter("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ- ");
        m_codeInput->setMaxCharCount(20);
        add(m_codeInput, 238.f + 103.f, 132.f);
        addButton(kit::button("Paste", {60.f, 22.f}, kit::Style::Secondary, this, menu_selector(NexMenu::onPaste), 8.f), 286.f, 100.f);
        addButton(kit::button("Join", {100.f, 22.f}, kit::Style::Primary, this, menu_selector(NexMenu::onJoin), 9.f), 380.f, 100.f);
        addText("Joining replaces the level open in your editor", 6.8f, 238.f + 103.f, 66.f, kit::kDanger, 0, {.5f, .5f});
        addText("Everyone needs the same Geometry Dash version", 7.f, W / 2.f, 12.f, kit::kMuted, 0, {.5f, .5f});
        return;
    }

    // In a room: code card, stats, leave.
    const bool host = role == Role::Host;
    add(kit::card({428.f, 74.f}, 12.f, glass, edge, .8f), W / 2.f, kContentTop - 37.f);
    if (host) {
        addText("ROOM CODE", 7.f, 32.f, kContentTop - 16.f, kit::kMuted, 1);
        m_codeText = addText("Preparing...", 19.f, 32.f, kContentTop - 37.f, kit::kText, 1);
        m_onlineText = addText("", 7.5f, 32.f, kContentTop - 59.f, kit::kMuted);
        addButton(kit::button("Copy", {84.f, 26.f}, kit::Style::Primary, this, menu_selector(NexMenu::onCopy), 9.5f),
                  W - 70.f, kContentTop - 37.f);
    } else {
        addText("CONNECTED TO", 7.f, 32.f, kContentTop - 16.f, kit::kMuted, 1);
        m_codeText = addText(sm->peerName(session::SessionHost::kHostPeer) + "'s room", 16.f, 32.f, kContentTop - 37.f, kit::kText, 1);
        m_onlineText = addText("", 7.5f, 32.f, kContentTop - 59.f, kit::kMuted);
    }

    const float tileY = kContentTop - 110.f;
    const char* captions[] = {"PLAYERS", "OBJECTS", "UPTIME"};
    CCLabelTTF** values[] = {&m_statPlayers, &m_statObjects, &m_statUptime};
    for (int i = 0; i < 3; ++i) {
        const float cx = 16.f + 70.f + static_cast<float>(i) * 144.f;
        add(kit::card({136.f, 54.f}, 10.f, glass, edge, .7f), cx, tileY);
        *values[i] = addText("-", 15.f, cx, tileY + 6.f, kit::kText, 1, {.5f, .5f});
        addText(captions[i], 6.5f, cx, tileY - 13.f, kit::kMuted, 1, {.5f, .5f});
    }

    addButton(kit::button(host ? "Close room" : "Leave room", {150.f, 26.f}, kit::Style::Danger, this,
                          menu_selector(NexMenu::onLeave), 9.5f), W / 2.f, 40.f);
    addText(host ? "The room stays open if you leave the editor" : "Your edits are shared live with everyone", 7.f, W / 2.f, 16.f,
            kit::kMuted, 0, {.5f, .5f});
}

// ---------------------------------------------------------------- Players
void NexMenu::buildPlayers() {
    auto* sm = SessionManager::get();
    auto peers = sm->peers();
    if (peers.empty()) {
        addText("No room yet", 12.f, W / 2.f, 140.f, kit::kText, 1, {.5f, .5f});
        addText("Host or join a room in the Room tab.", 8.f, W / 2.f, 122.f, kit::kMuted, 0, {.5f, .5f});
        return;
    }
    addText("PLAYERS  " + std::to_string(peers.size()) + "/16", 7.5f, 24.f, kContentTop - 6.f, kit::kMuted, 1);
    addText("PING", 7.f, 286.f, kContentTop - 6.f, kit::kMuted, 1, {.5f, .5f});
    const auto self = sm->selfId();
    const bool host = sm->role() == Role::Host;
    const std::size_t maxRows = 6;
    for (std::size_t i = 0; i < peers.size() && i < maxRows; ++i) {
        const auto& p = peers[i];
        const float y = kContentTop - 26.f - static_cast<float>(i) * 29.f;
        add(kit::card({428.f, 25.f}, 8.f, kit::rgba(30, 34, 66, i % 2 ? .55f : .85f), kit::rgba(kit::kViolet, .15f), .5f), W / 2.f, y);
        add(kit::avatar(p.name, peerColor(p.id), 9.f), 32.f, y);
        auto* name = addText(p.name, 9.f, 48.f, y, kit::kText, 1);
        if (name->getContentSize().width > 120.f) name->setScale(120.f / name->getContentSize().width);
        float chipX = 48.f + std::min(name->getContentSize().width * name->getScale(), 120.f) + 6.f;
        auto addChip = [&](const std::string& t, ccColor3B c) {
            auto* ch = kit::chip(t, c, 6.f);
            ch->setAnchorPoint({0.f, .5f});
            add(ch, chipX, y);
            chipX += ch->getContentSize().width + 4.f;
        };
        if (p.id == session::SessionHost::kHostPeer) addChip("HOST", kit::kViolet);
        if (self && p.id == *self) addChip("YOU", kit::kTeal);

        if (p.id != session::SessionHost::kHostPeer) {
            auto* meter = add(kit::Meter::create({56.f, 5.f}), 262.f, y);
            auto* label = addText("...", 8.f, 306.f, y, kit::kMuted, 1, {.5f, .5f});
            m_pingRows.push_back({p.id, label, meter});
        } else {
            addText("host", 7.5f, 286.f, y, kit::kMuted, 0, {.5f, .5f});
        }
        if (p.accountId != 0) {
            auto* b = kit::button("Profile", {50.f, 17.f}, kit::Style::Secondary, this, menu_selector(NexMenu::onProfile), 7.f);
            b->setTag(static_cast<int>(p.accountId));
            addButton(b, 356.f, y);
        }
        if (host && p.id != session::SessionHost::kHostPeer) {
            auto* b = kit::button("Kick", {40.f, 17.f}, kit::Style::Danger, this, menu_selector(NexMenu::onKick), 7.f);
            b->setTag(p.id);
            addButton(b, 412.f, y);
        }
    }
    if (peers.size() > maxRows) {
        addText("+" + std::to_string(peers.size() - maxRows) + " more", 8.f, W / 2.f, 14.f, kit::kMuted, 0, {.5f, .5f});
    }
}

// ---------------------------------------------------------------- Network
void NexMenu::buildNetwork() {
    const auto glass = kit::rgba(30, 34, 66, .85f);
    const auto edge = kit::rgba(kit::kViolet, .3f);
    // Left column: quality gauge + connection details.
    add(kit::card({150.f, 192.f}, 12.f, glass, edge, .8f), 16.f + 75.f, 18.f + 96.f);
    addText("INTERNET QUALITY", 6.5f, 91.f, kContentTop - 8.f, kit::kMuted, 1, {.5f, .5f});
    m_gauge = add(kit::Gauge::create(36.f), 91.f, kContentTop - 56.f);
    m_qualityText = addText("Measuring...", 11.f, 91.f, kContentTop - 102.f, kit::kText, 1, {.5f, .5f});
    m_viaText = addText("Connection: -", 7.f, 26.f, 62.f, kit::kMuted);
    m_ipText = addText("IP: hidden", 7.f, 26.f, 48.f, kit::kMuted);
    m_portText = addText("PORT: -", 7.f, 26.f, 34.f, kit::kMuted);
    addButton(kit::button(m_showIp ? "Hide IP" : "Show IP", {58.f, 16.f}, kit::Style::Secondary, this,
                          menu_selector(NexMenu::onToggleIp), 6.5f), 128.f, 48.f);

    // Right column: ping chart + stat tiles.
    add(kit::card({270.f, 192.f}, 12.f, glass, edge, .8f), 174.f + 135.f, 18.f + 96.f);
    addText("PING  -  LAST 60 SECONDS", 6.5f, 186.f, kContentTop - 8.f, kit::kMuted, 1);
    m_chart = add(kit::Chart::create({250.f, 96.f}, session::RttTracker::kHistory), 309.f, kContentTop - 70.f);
    const char* captions[] = {"PING", "AVG", "JITTER", "LOSS"};
    CCLabelTTF** values[] = {&m_tilePing, &m_tileAvg, &m_tileJitter, &m_tileLoss};
    for (int i = 0; i < 4; ++i) {
        const float cx = 184.f + 31.f + static_cast<float>(i) * 63.f;
        add(kit::card({58.f, 40.f}, 8.f, kit::rgba(16, 18, 38, .9f), kit::rgba(kit::kTeal, .22f), .6f), cx, 46.f);
        *values[i] = addText("-", 10.f, cx, 50.f, kit::kText, 1, {.5f, .5f});
        addText(captions[i], 5.5f, cx, 35.f, kit::kMuted, 1, {.5f, .5f});
    }
}

// ---------------------------------------------------------------- Settings
void NexMenu::buildSettings() {
    const auto glass = kit::rgba(30, 34, 66, .85f);
    const auto edge = kit::rgba(kit::kViolet, .25f);
    struct Row {
        const char* key;
        const char* title;
        const char* desc;
    } rows[] = {
        {"show-ping", "Ping in the editor", "Your ping, shown at the top while you are in a room"},
        {"show-frames", "Players' view", "A coloured frame with each player's name where they work"},
    };
    float y = kContentTop - 22.f;
    for (int i = 0; i < 2; ++i, y -= 42.f) {
        add(kit::card({428.f, 36.f}, 10.f, glass, edge, .6f), W / 2.f, y);
        addText(rows[i].title, 9.5f, 32.f, y + 6.f, kit::kText, 1);
        addText(rows[i].desc, 7.f, 32.f, y - 7.f, kit::kMuted);
        auto* toggle = kit::Toggle::create(Mod::get()->getSettingValue<bool>(rows[i].key));
        auto* item = CCMenuItemSpriteExtra::create(toggle, this, menu_selector(NexMenu::onToggleSetting));
        item->m_scaleMultiplier = 1.05f;
        item->setTag(i);
        addButton(item, W - 44.f, y);
    }
    // Relay server
    add(kit::card({428.f, 58.f}, 10.f, glass, edge, .6f), W / 2.f, y - 10.f);
    addText("Relay server", 9.5f, 32.f, y + 8.f, kit::kText, 1);
    addText("Rooms through a relay work on any network, including mobile data", 7.f, 32.f, y - 5.f, kit::kMuted);
    add(kit::card({250.f, 20.f}, 7.f, kit::rgba(12, 14, 30, 1.f), kit::rgba(kit::kTeal, .4f), .6f), 32.f + 125.f, y - 24.f);
    m_relayInput = TextInput::create(240.f, "host or host:port (empty = off)", "chatFont.fnt");
    m_relayInput->hideBG();
    m_relayInput->setFilter("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:");
    m_relayInput->setMaxCharCount(64);
    m_relayInput->setString(Mod::get()->getSettingValue<std::string>("relay-server"));
    add(m_relayInput, 32.f + 125.f, y - 24.f);
    addButton(kit::button("Save", {60.f, 20.f}, kit::Style::Primary, this, menu_selector(NexMenu::onSaveRelay), 8.f), 330.f, y - 24.f);
    addText("NexLevel " + Mod::get()->getVersion().toVString() + "  -  protocol v" + std::to_string(kProtocolVersion), 6.5f, W / 2.f, 12.f,
            kit::kMuted, 0, {.5f, .5f});
}

// ---------------------------------------------------------------- live values
void NexMenu::updateLive() {
    auto* sm = SessionManager::get();
    const auto role = sm->role();

    // Header status.
    ccColor3B dot = kit::kMuted;
    std::string status = "OFFLINE";
    if (role == Role::Host) { status = "HOSTING"; dot = kit::kGood; }
    else if (role == Role::Client) {
        status = sm->inSession() ? "CONNECTED" : "CONNECTING";
        dot = sm->inSession() ? kit::kGood : kit::kWarn;
    }
    kit::setTextIfChanged(m_status, status);
    if (m_status) m_status->setColor(dot);
    if (m_statusDot) {
        m_statusDot->clear();
        kit::fillCircle(m_statusDot, {0, 0}, 5.f, kit::rgba(dot, .2f));
        kit::fillCircle(m_statusDot, {0, 0}, 3.f, kit::rgba(dot, 1.f));
    }

    // Room tab.
    if (m_codeText && role == Role::Host) {
        auto code = sm->roomCode();
        kit::setTextIfChanged(m_codeText, code ? *code : "Preparing...");
        m_codeText->setColor(code ? kit::kText : kit::kMuted);
    }
    if (m_onlineText) {
        std::string t = role == Role::Host ? sm->onlineText() : sm->statusText();
        kit::setTextIfChanged(m_onlineText, t);
        m_onlineText->setColor(t.rfind("Online", 0) == 0 || t.rfind("Connected", 0) == 0 ? kit::kGood : kit::kMuted);
    }
    if (m_statPlayers) kit::setTextIfChanged(m_statPlayers, std::to_string(sm->peers().size()) + "/16");
    if (m_statObjects) kit::setTextIfChanged(m_statObjects, std::to_string(sm->objectCount()));
    if (m_statUptime) kit::setTextIfChanged(m_statUptime, uptimeText(sm->roomSeconds()));

    // Players tab.
    for (auto& row : m_pingRows) {
        auto ms = sm->pingOf(row.peer);
        kit::setTextIfChanged(row.label, ms ? std::to_string(*ms) + " ms" : "...");
        const auto c = ms ? ui::pingColor(*ms) : kit::kMuted;
        row.label->setColor(c);
        row.meter->setValue(ms ? 1.f - std::min(*ms, 250) / 250.f : 0.f, c);
    }

    // Network tab.
    if (m_gauge || m_chart) {
        auto info = sm->connectionInfo();
        const auto q = session::qualityFor(info.ping);
        if (m_gauge) {
            m_gauge->setValue(qualityValue(q), ui::qualityColor(q), info.ping.ms ? msText(*info.ping.ms) : "-",
                              role == Role::Host && !info.pingSubject.empty() ? info.pingSubject : "ping");
        }
        if (m_qualityText) {
            kit::setTextIfChanged(m_qualityText, role == Role::None ? "Not in a room" : session::toString(q));
            m_qualityText->setColor(ui::qualityColor(q));
        }
        if (m_chart) m_chart->setData(info.ping.history, 100.f);
        kit::setTextIfChanged(m_tilePing, info.ping.ms ? msText(*info.ping.ms) : "-");
        kit::setTextIfChanged(m_tileAvg, info.ping.ms ? msText(info.ping.avgMs) : "-");
        kit::setTextIfChanged(m_tileJitter, info.ping.ms ? msText(info.ping.jitterMs) : "-");
        kit::setTextIfChanged(m_tileLoss, info.ping.ms ? std::to_string(static_cast<int>(info.ping.lossPercent + .5f)) + "%" : "-");
        kit::setTextIfChanged(m_viaText, "Connection: " + (info.via.empty() ? std::string("-") : info.via));
        kit::setTextIfChanged(m_ipText, "IP: " + (m_showIp ? info.localIp + (info.publicIp.empty() ? "" : " / " + info.publicIp) : std::string("hidden")));
        kit::setTextIfChanged(m_portText, "PORT: " + (info.port ? std::to_string(info.port) : std::string("-")));
    }
}

// ---------------------------------------------------------------- actions
void NexMenu::onTab(CCObject* sender) {
    m_tab = static_cast<Tab>(static_cast<CCNode*>(sender)->getTag());
    buildTab();
}

void NexMenu::onClose2(CCObject* sender) { this->onClose(sender); }

void NexMenu::onHost(CCObject*) {
    std::string err;
    if (SessionManager::get()->hostRoom(&err)) notify("Room open - share the room code", NotificationIcon::Success);
    else notify(err, NotificationIcon::Error);
    buildTab();
}

void NexMenu::onJoin(CCObject*) {
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
    buildTab();
}

void NexMenu::onPaste(CCObject*) {
    if (!m_codeInput) return;
    std::string cleaned;
    for (char c : utils::clipboard::read()) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') cleaned += c;
    }
    m_codeInput->setString(cleaned.substr(0, 20));
}

void NexMenu::onCopy(CCObject*) {
    if (auto code = SessionManager::get()->roomCode()) {
        utils::clipboard::write(*code);
        notify("Room code copied", NotificationIcon::Success);
    }
}

void NexMenu::onLeave(CCObject*) {
    if (SessionManager::get()->role() == Role::Host) {
        Ref<NexMenu> self = this;
        createQuickPopup("Close room?", "Everyone in the room will be disconnected.", "Cancel", "Close", [self](FLAlertLayer*, bool close) {
            if (!close) return;
            SessionManager::get()->leave();
            self->buildTab();
        });
        return;
    }
    SessionManager::get()->leave();
    buildTab();
}

void NexMenu::onKick(CCObject* sender) {
    auto peer = static_cast<sync::PeerId>(static_cast<CCNode*>(sender)->getTag());
    if (SessionManager::get()->kick(peer)) notify("Player removed", NotificationIcon::Info);
    buildTab();
}

void NexMenu::onProfile(CCObject* sender) {
    const int account = static_cast<CCNode*>(sender)->getTag();
    if (account <= 0) return;
    ProfilePage::create(account, account == GJAccountManager::sharedState()->m_accountID)->show();
}

void NexMenu::onToggleIp(CCObject*) {
    m_showIp = !m_showIp;
    buildTab();
}

void NexMenu::onToggleSetting(CCObject* sender) {
    static const char* keys[] = {"show-ping", "show-frames"};
    auto* item = static_cast<CCMenuItemSpriteExtra*>(sender);
    const int i = item->getTag();
    if (i < 0 || i > 1) return;
    const bool on = !Mod::get()->getSettingValue<bool>(keys[i]);
    Mod::get()->setSettingValue<bool>(keys[i], on);
    if (auto* t = typeinfo_cast<kit::Toggle*>(item->getNormalImage())) t->setOn(on);
}

void NexMenu::onSaveRelay(CCObject*) {
    if (!m_relayInput) return;
    Mod::get()->setSettingValue<std::string>("relay-server", std::string(m_relayInput->getString()));
    notify("Relay saved - used for the next room", NotificationIcon::Success);
}

} // namespace nexlevel::mod

// "Collab" button in the editor pause menu (Esc).
class $modify(NLEditorPauseLayer, EditorPauseLayer) {
    bool init(LevelEditorLayer* layer) {
        if (!EditorPauseLayer::init(layer)) return false;
        auto* btn = kit::button("NexLevel Collab", {120.f, 24.f}, kit::Style::Primary, this,
                                menu_selector(NLEditorPauseLayer::onNexLevelCollab), 9.f);
        auto* menu = CCMenu::create();
        menu->addChild(btn);
        menu->setID("nexlevel-collab-menu"_spr);
        auto win = CCDirector::sharedDirector()->getWinSize();
        menu->setPosition({win.width / 2.f, win.height - 28.f});
        this->addChild(menu, 10);
        return true;
    }

    void onNexLevelCollab(CCObject*) {
        if (auto* menu = nexlevel::mod::NexMenu::create()) menu->show();
    }
};

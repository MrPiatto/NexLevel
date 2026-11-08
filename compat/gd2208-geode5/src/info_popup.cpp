#include "info_popup.hpp"

#include "session_manager.hpp"
#include "ui_common.hpp"

#include <algorithm>
#include <cstdio>

using namespace geode::prelude;
using namespace nexlevel::mod::ui;

namespace nexlevel::mod {

namespace {
constexpr float kGraphW = 300.f, kGraphH = 70.f;

std::string fmtMs(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.0f ms", v);
    return b;
}
} // namespace

InfoPopup* InfoPopup::create() {
    auto* ret = new InfoPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool InfoPopup::init() {
    if (!Popup::init(380.f, 270.f, "square02b_001.png", {0, 0, 80, 80})) return false;
    m_bgSprite->setColor(kPanel);
    m_bgSprite->setOpacity(250);
    auto* frame = NineSlice::create("square02b_001.png", {0, 0, 80, 80});
    frame->setContentSize(m_size + CCSize(8.f, 8.f));
    frame->setColor(kPurple);
    m_mainLayer->addChildAtPosition(frame, Anchor::Center);
    frame->setZOrder(-10);
    m_bgSprite->setZOrder(-5);
    this->setTitle("Connection info", "bigFont.fnt", .65f, 20.f);
    if (m_title) m_title->setColor(kPurpleSoft);

    m_graph = CCDrawNode::create();
    m_mainLayer->addChildAtPosition(m_graph, Anchor::Bottom, {-kGraphW / 2.f, 22.f});

    rebuild();
    this->schedule(schedule_selector(InfoPopup::refresh), .5f);
    return true;
}

void InfoPopup::refresh(float) { rebuild(); }

void InfoPopup::onToggleIp(CCObject*) {
    m_showIp = !m_showIp;
    rebuild();
}

void InfoPopup::rebuild() {
    for (auto* n : m_dynamic) n->removeFromParent();
    m_dynamic.clear();
    auto* sm = SessionManager::get();
    auto info = sm->connectionInfo();

    auto line = [&](const std::string& k, const std::string& v, float y, ccColor3B vc = {255, 255, 255}) {
        auto* kl = track(label(k, "goldFont.fnt", .45f, kGrey));
        kl->setAnchorPoint({1.f, .5f});
        m_mainLayer->addChildAtPosition(kl, Anchor::Top, {-20.f, y});
        auto* vl = track(label(v, "bigFont.fnt", .38f, vc));
        vl->setAnchorPoint({0.f, .5f});
        vl->limitLabelWidth(170.f, .38f, .2f);
        m_mainLayer->addChildAtPosition(vl, Anchor::Top, {-10.f, y});
        return vl;
    };

    line("Role", info.role.empty() ? "Not in a room" : info.role, -48.f);
    line("Connection", info.via.empty() ? "-" : info.via, -66.f);
    std::string ip = m_showIp ? info.localIp + (info.publicIp.empty() ? "" : "  /  " + info.publicIp) : "hidden";
    auto* ipLabel = line("IP", ip, -84.f, m_showIp ? ccColor3B{255, 255, 255} : kGrey);
    (void)ipLabel;
    auto* toggle = track(button(m_showIp ? "Hide" : "Show", kPurpleDark, .4f, this, menu_selector(InfoPopup::onToggleIp)));
    m_buttonMenu->addChildAtPosition(toggle, Anchor::Top, {150.f, -84.f});
    line("PORT", info.port ? std::to_string(info.port) : "-", -102.f);

    const auto q = session::qualityFor(info.ping);
    auto* ql = line("Internet quality", session::toString(q), -124.f, qualityColor(q));
    (void)ql;
    if (auto* icon = optionalSprite(qualityIcon(q))) {
        icon->setScale(.55f);
        track(icon);
        m_mainLayer->addChildAtPosition(icon, Anchor::Top, {150.f, -124.f});
    }
    std::string stats = info.ping.ms
        ? "Ping " + fmtMs(*info.ping.ms) + "   avg " + fmtMs(info.ping.avgMs) + "   jitter " + fmtMs(info.ping.jitterMs) +
              "   loss " + std::to_string(static_cast<int>(info.ping.lossPercent + .5f)) + "%"
        : std::string(info.role == "Host" ? "Waiting for players..." : "Measuring...");
    m_mainLayer->addChildAtPosition(track(label(stats, "chatFont.fnt", .6f, kGrey)), Anchor::Top, {0.f, -146.f});
    if (!info.pingSubject.empty() && info.role == "Host") {
        m_mainLayer->addChildAtPosition(track(label("Slowest player: " + info.pingSubject, "chatFont.fnt", .5f, kGrey)),
            Anchor::Top, {0.f, -160.f});
    }

    // Ping graph (last 60 seconds).
    m_graph->clear();
    CCPoint box[4] = {{0, 0}, {kGraphW, 0}, {kGraphW, kGraphH}, {0, kGraphH}};
    m_graph->drawPolygon(box, 4, ccColor4F{.06f, .02f, .12f, .9f}, .5f, ccColor4F{.6f, .3f, 1.f, .6f});
    const auto& h = info.ping.history;
    float top = 100.f;
    for (float v : h) top = std::max(top, v * 1.2f);
    for (float ref : {50.f, 100.f, 200.f}) {  // reference lines
        if (ref >= top) continue;
        float y = ref / top * kGraphH;
        m_graph->drawSegment({0, y}, {kGraphW, y}, .3f, ccColor4F{.6f, .5f, .8f, .25f});
        auto* rl = track(label(std::to_string(static_cast<int>(ref)), "chatFont.fnt", .35f, kGrey));
        rl->setAnchorPoint({1.f, .5f});
        m_mainLayer->addChildAtPosition(rl, Anchor::Bottom, {-kGraphW / 2.f - 3.f, 22.f + y});
    }
    if (h.size() >= 2) {
        const float step = kGraphW / static_cast<float>(session::RttTracker::kHistory - 1);
        const float x0 = kGraphW - step * static_cast<float>(h.size() - 1);
        for (std::size_t i = 1; i < h.size(); ++i) {
            CCPoint a{x0 + step * static_cast<float>(i - 1), std::min(h[i - 1] / top, 1.f) * kGraphH};
            CCPoint b{x0 + step * static_cast<float>(i), std::min(h[i] / top, 1.f) * kGraphH};
            const auto c = pingColor(static_cast<int>(h[i]));
            m_graph->drawSegment(a, b, .9f, ccColor4F{c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f});
        }
    }
}

} // namespace nexlevel::mod

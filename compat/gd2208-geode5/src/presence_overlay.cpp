// cocos2d APIs used: CCDrawNode::create/clear/drawPolygon, CCLabelBMFont, CCNode::convertToNodeSpace,
// CCDirector::getWinSize. GD: GJBaseGameLayer::m_objectLayer (bindings 2.2081).
#include "presence_overlay.hpp"

#include "session_manager.hpp"

#include <cmath>
#include <map>

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {

constexpr int kOverlayTag = 0x4E584C;  // "NXL"

const ccColor3B kColors[] = {
    {196, 120, 255}, {80, 220, 255}, {255, 110, 200}, {120, 255, 160},
    {255, 200, 80},  {255, 120, 120}, {150, 160, 255}, {220, 255, 110},
};
ccColor3B colorFor(sync::PeerId peer) { return kColors[peer % (sizeof(kColors) / sizeof(kColors[0]))]; }

struct Shown {
    float x = 0, y = 0, zoom = 1;
    bool init = false;
};

class PresenceOverlay : public CCNode {
public:
    static PresenceOverlay* create() {
        auto* ret = new PresenceOverlay();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init() override {
        if (!CCNode::init()) return false;
        m_draw = CCDrawNode::create();
        this->addChild(m_draw);
        return true;
    }

    void redraw(float myZoom) {
        auto* sm = SessionManager::get();
        auto list = sm->presences();
        m_draw->clear();
        std::map<sync::PeerId, bool> seen;
        const auto win = CCDirector::sharedDirector()->getWinSize();
        for (auto& p : list) {
            seen[p.peer] = true;
            auto& s = m_shown[p.peer];
            if (!s.init) { s = {p.x, p.y, p.zoom, true}; }
            // Ease towards the latest position so movement looks smooth at 10 updates per second.
            s.x += (p.x - s.x) * .35f;
            s.y += (p.y - s.y) * .35f;
            s.zoom += (p.zoom - s.zoom) * .35f;

            const float hw = win.width / 2.f / s.zoom, hh = win.height / 2.f / s.zoom;
            CCPoint verts[4] = {{s.x - hw, s.y - hh}, {s.x + hw, s.y - hh}, {s.x + hw, s.y + hh}, {s.x - hw, s.y + hh}};
            const auto c = colorFor(p.peer);
            ccColor4F border{c.r / 255.f, c.g / 255.f, c.b / 255.f, .9f};
            ccColor4F fill{c.r / 255.f, c.g / 255.f, c.b / 255.f, .05f};
            m_draw->drawPolygon(verts, 4, fill, 1.5f / myZoom, border);

            auto*& label = m_labels[p.peer];
            if (!label) {
                label = CCLabelBMFont::create(sm->peerName(p.peer).c_str(), "bigFont.fnt");
                label->setAnchorPoint({0.f, 0.f});
                label->setColor(c);
                this->addChild(label);
            }
            label->setScale(.45f / myZoom);
            label->setPosition({s.x - hw + 4.f / myZoom, s.y + hh + 2.f / myZoom});
        }
        // Players who left.
        for (auto it = m_labels.begin(); it != m_labels.end();) {
            if (!seen.contains(it->first)) {
                it->second->removeFromParent();
                m_shown.erase(it->first);
                it = m_labels.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    CCDrawNode* m_draw = nullptr;
    std::map<sync::PeerId, CCLabelBMFont*> m_labels;
    std::map<sync::PeerId, Shown> m_shown;
};

} // namespace

void updatePresence(LevelEditorLayer* layer, bool paused) {
    if (!layer || !layer->m_objectLayer) return;
    auto* sm = SessionManager::get();
    auto* objectLayer = layer->m_objectLayer;
    auto* overlay = static_cast<PresenceOverlay*>(objectLayer->getChildByTag(kOverlayTag));
    if (!sm->inSession()) {
        if (overlay) overlay->removeFromParent();
        return;
    }
    const float zoom = objectLayer->getScale();
    if (!paused) {
        const auto win = CCDirector::sharedDirector()->getWinSize();
        auto centre = objectLayer->convertToNodeSpace(CCPoint{win.width / 2.f, win.height / 2.f});
        sm->setLocalPresence(centre.x, centre.y, zoom);
    }
    if (!overlay) {
        overlay = PresenceOverlay::create();
        if (!overlay) return;
        overlay->setTag(kOverlayTag);
        objectLayer->addChild(overlay, 10000);
    }
    overlay->setVisible(!paused);
    if (!paused) overlay->redraw(zoom > 0.01f ? zoom : 1.f);
}

} // namespace nexlevel::mod

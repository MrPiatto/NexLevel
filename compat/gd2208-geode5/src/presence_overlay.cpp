// cocos2d APIs used: CCDrawNode::create/clear/drawPolygon/drawSegment, CCLabelBMFont,
// CCNode::convertToNodeSpace, CCDirector::getWinSize.
// GD (bindings 2.2081): GJBaseGameLayer::m_objectLayer, SimplePlayer::create / updatePlayerFrame / setColor.
#include "presence_overlay.hpp"

#include "session_manager.hpp"
#include "ui_common.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {

constexpr int kOverlayTag = 0x4E584C;  // "NXL"

ccColor3B colorFor(sync::PeerId peer) { return ui::peerColor(peer); }

// One playtesting player as seen from the editor: their trail and their icon(s).
struct TrailView {
    CCDrawNode* draw = nullptr;
    SimplePlayer* icon1 = nullptr;
    SimplePlayer* icon2 = nullptr;
    CCLabelBMFont* name = nullptr;
    std::uint16_t session = 0xFFFF;
    std::size_t drawn1 = 0, drawn2 = 0;
    int mode1 = -1, mode2 = -1;
    int icon1Id = -1, icon2Id = -1;
};

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
        m_trailLayer = CCNode::create();
        this->addChild(m_trailLayer, 0);
        m_frameLayer = CCNode::create();
        this->addChild(m_frameLayer, 1);
        m_draw = CCDrawNode::create();
        m_frameLayer->addChild(m_draw);
        return true;
    }

    void showFrames(bool on) { m_frameLayer->setVisible(on); }
    void showTrails(bool on) { m_trailLayer->setVisible(on); }

    // Other players' playtests: trails grow incrementally (only new segments are drawn).
    void redrawPlaytests(float myZoom) {
        auto* sm = SessionManager::get();
        const auto& trails = sm->playTrails();
        const auto now = std::chrono::steady_clock::now();
        for (auto& [peer, t] : trails) {
            auto& v = m_trails[peer];
            const auto c = colorFor(peer);
            if (!v.draw) {
                v.draw = CCDrawNode::create();
                m_trailLayer->addChild(v.draw, 0);
                v.name = CCLabelBMFont::create(sm->peerName(peer).c_str(), "bigFont.fnt");
                v.name->setColor(c);
                m_trailLayer->addChild(v.name, 2);
            }
            if (v.session != t.last.session || t.points1.size() < v.drawn1 || t.points2.size() < v.drawn2) {
                v.draw->clear();  // new test (or trimmed trail): redraw from scratch
                v.session = t.last.session;
                v.drawn1 = v.drawn2 = 0;
            }
            auto drawNew = [&](const std::vector<std::pair<float, float>>& pts, std::size_t& drawn) {
                for (std::size_t i = std::max<std::size_t>(drawn, 1); i < pts.size(); ++i) {
                    CCPoint a{pts[i - 1].first, pts[i - 1].second}, b{pts[i].first, pts[i].second};
                    v.draw->drawSegment(a, b, 3.f, ccColor4F{c.r / 255.f, c.g / 255.f, c.b / 255.f, .22f});  // glow
                    v.draw->drawSegment(a, b, 1.2f, ccColor4F{c.r / 255.f, c.g / 255.f, c.b / 255.f, .95f});
                }
                drawn = std::max(drawn, pts.size());
            };
            drawNew(t.points1, v.drawn1);
            drawNew(t.points2, v.drawn2);

            // Icons only while they are actually playing (and updates keep coming).
            const bool live = t.last.playing() && now - t.updated < std::chrono::milliseconds(1500);
            // Each player's own icon in their own colour; the trail and name use their room colour.
            const ccColor3B iconColor{t.last.r, t.last.g, t.last.b};
            auto placeIcon = [&](SimplePlayer*& icon, int& mode, int& iconId, bool show, int newMode, int newIcon, float x, float y, float rot,
                                 float scale, bool upside) {
                if (!show) {
                    if (icon) icon->setVisible(false);
                    return;
                }
                // Data from another player: only game modes and icon ids this game actually has
                // (an unknown IconType or an icon past the last one has no texture to load).
                newMode = std::clamp(newMode, 0, 7);  // cube .. swing
                const int maxIcon = std::max(1, GameManager::get()->countForType(static_cast<IconType>(newMode)));
                newIcon = std::clamp(newIcon, 1, maxIcon);
                if (!icon) {
                    icon = SimplePlayer::create(std::clamp(newIcon, 1, std::max(1, GameManager::get()->countForType(IconType::Cube))));
                    if (!icon) return;
                    m_trailLayer->addChild(icon, 1);
                }
                if (mode != newMode || iconId != newIcon) {
                    icon->updatePlayerFrame(newIcon, static_cast<IconType>(newMode));
                    mode = newMode;
                    iconId = newIcon;
                }
                icon->setColor(iconColor);
                icon->setVisible(true);
                icon->setPosition({x, y});
                icon->setRotation(rot);
                const float sc = scale > 0.05f ? scale : 1.f;
                icon->setScaleX(sc);
                icon->setScaleY(upside ? -sc : sc);
            };
            using PS = session::PlayerState;
            placeIcon(v.icon1, v.mode1, v.icon1Id, live, t.last.mode1, t.last.icon1, t.last.x1, t.last.y1, t.last.rot1, t.last.scale1,
                      t.last.flags & PS::UpsideDown1);
            placeIcon(v.icon2, v.mode2, v.icon2Id, live && (t.last.flags & PS::Dual), t.last.mode2, t.last.icon2, t.last.x2, t.last.y2,
                      t.last.rot2, t.last.scale2, t.last.flags & PS::UpsideDown2);
            v.name->setVisible(live);
            v.name->setScale(.4f / myZoom);
            v.name->setPosition({t.last.x1, t.last.y1 + 24.f});
        }
        // Players who left.
        for (auto it = m_trails.begin(); it != m_trails.end();) {
            if (!trails.contains(it->first)) {
                for (CCNode* n : {static_cast<CCNode*>(it->second.draw), static_cast<CCNode*>(it->second.icon1),
                                  static_cast<CCNode*>(it->second.icon2), static_cast<CCNode*>(it->second.name)}) {
                    if (n) n->removeFromParent();
                }
                it = m_trails.erase(it);
            } else {
                ++it;
            }
        }
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
            ccColor4F fill{0.f, 0.f, 0.f, 0.f};  // outline only: the view itself stays clear
            m_draw->drawPolygon(verts, 4, fill, 1.5f / myZoom, border);

            auto*& label = m_labels[p.peer];
            if (!label) {
                label = CCLabelBMFont::create(sm->peerName(p.peer).c_str(), "bigFont.fnt");
                label->setAnchorPoint({0.f, 0.f});
                label->setColor(c);
                m_frameLayer->addChild(label);
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
    CCNode* m_trailLayer = nullptr;
    CCNode* m_frameLayer = nullptr;
    std::map<sync::PeerId, TrailView> m_trails;
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
    const bool showFrames = Mod::get()->getSettingValue<bool>("show-frames");
    const bool showTrails = Mod::get()->getSettingValue<bool>("show-trails");
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
    // While this player is playtesting the overlay is hidden; everyone else's view keeps updating.
    overlay->setVisible(!paused);
    if (paused) return;
    const float z = zoom > 0.01f ? zoom : 1.f;
    overlay->showFrames(showFrames);
    overlay->showTrails(showTrails);
    if (showFrames) overlay->redraw(z);
    overlay->redrawPlaytests(z);  // keeps trails complete even while hidden
}

} // namespace nexlevel::mod

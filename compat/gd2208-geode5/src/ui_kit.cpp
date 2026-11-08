#include "ui_kit.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>

using namespace geode::prelude;

namespace nexlevel::mod::kit {

namespace {
constexpr float kPi = 3.14159265f;

#ifdef GEODE_IS_WINDOWS
constexpr const char* kFontRegular = "Segoe UI";
constexpr const char* kFontBold = "Segoe UI Semibold";
#else
constexpr const char* kFontRegular = "Arial";
constexpr const char* kFontBold = "Arial";
#endif

// Vertical inset of a rounded rect's edge at x (0 in the straight part).
float cornerInset(float x, float w, float r) {
    float d = 0.f;
    if (x < r) d = r - x;
    else if (x > w - r) d = x - (w - r);
    else return 0.f;
    d = std::min(d, r);
    return r - std::sqrt(std::max(0.f, r * r - d * d));
}

std::vector<CCPoint> dedupe(std::vector<CCPoint> pts) {
    // Coincident consecutive points (e.g. a pill whose corner centres meet) make cocos2d's
    // anti-aliased polygon extrusion shoot out spikes: drop them.
    std::vector<CCPoint> out;
    for (auto& p : pts) {
        if (!out.empty() && std::fabs(out.back().x - p.x) < .01f && std::fabs(out.back().y - p.y) < .01f) continue;
        out.push_back(p);
    }
    while (out.size() > 2 && std::fabs(out.front().x - out.back().x) < .01f && std::fabs(out.front().y - out.back().y) < .01f) out.pop_back();
    return out;
}

void arc(std::vector<CCPoint>& pts, CCPoint c, float r, float a0, float a1, int steps) {
    for (int i = 0; i <= steps; ++i) {
        const float a = a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(steps);
        pts.push_back({c.x + std::cos(a) * r, c.y + std::sin(a) * r});
    }
}

ccColor4F lerp(ccColor3B a, ccColor3B b, float t, float alpha) {
    return {(a.r + (b.r - a.r) * t) / 255.f, (a.g + (b.g - a.g) * t) / 255.f, (a.b + (b.b - a.b) * t) / 255.f, alpha};
}

// Fill a rounded rect with a left-to-right gradient: the two rounded ends are solid convex
// polygons, the straight middle is cut into rectangles (no thin slivers, so no spikes).
void drawGradientRoundedRect(CCDrawNode* draw, float w, float h, float r, ccColor3B from, ccColor3B to, float alpha) {
    r = std::min(r, std::min(w, h) / 2.f);
    const ccColor4F none{0, 0, 0, 0};
    const float ov = .4f;  // overlap between pieces hides seams
    const float tl = r / w * .5f, tr = 1.f - tl;
    std::vector<CCPoint> left{{r + ov, 0.f}};
    arc(left, {r, r}, r, -kPi / 2.f, -kPi, 8);
    arc(left, {r, h - r}, r, kPi, kPi / 2.f, 8);
    left.push_back({r + ov, h});
    left = dedupe(left);
    draw->drawPolygon(left.data(), static_cast<unsigned int>(left.size()), lerp(from, to, tl, alpha), 0.f, none);
    std::vector<CCPoint> right{{w - r - ov, h}};
    arc(right, {w - r, h - r}, r, kPi / 2.f, 0.f, 8);
    arc(right, {w - r, r}, r, 0.f, -kPi / 2.f, 8);
    right.push_back({w - r - ov, 0.f});
    right = dedupe(right);
    draw->drawPolygon(right.data(), static_cast<unsigned int>(right.size()), lerp(from, to, tr, alpha), 0.f, none);
    const float mw = w - 2.f * r;
    if (mw <= 0.f) return;
    const int strips = std::max(4, static_cast<int>(mw / 4.f));
    for (int i = 0; i < strips; ++i) {
        const float xa = r + mw * static_cast<float>(i) / static_cast<float>(strips);
        const float xb = std::min(w - r + ov, r + mw * static_cast<float>(i + 1) / static_cast<float>(strips) + ov);
        const float t = tl + (tr - tl) * (static_cast<float>(i) + .5f) / static_cast<float>(strips);
        CCPoint quad[4] = {{xa, 0.f}, {xb, 0.f}, {xb, h}, {xa, h}};
        draw->drawPolygon(quad, 4, lerp(from, to, t, alpha), 0.f, none);
    }
}

std::vector<CCPoint> circlePoints(CCPoint c, float r) {
    std::vector<CCPoint> pts;
    const int n = std::clamp(static_cast<int>(r * 3.f), 16, 48);
    for (int i = 0; i < n; ++i) {
        const float a = 2.f * kPi * static_cast<float>(i) / static_cast<float>(n);
        pts.push_back({c.x + std::cos(a) * r, c.y + std::sin(a) * r});
    }
    return pts;
}

CCNode* sized(CCSize size) {
    auto* n = CCNode::create();
    n->setContentSize(size);
    n->setAnchorPoint({.5f, .5f});
    return n;
}
} // namespace

void fillCircle(CCDrawNode* draw, CCPoint centre, float radius, ccColor4F color) {
    if (radius <= 0.f) return;
    auto pts = circlePoints(centre, radius);
    draw->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), color, 0.f, ccColor4F{0, 0, 0, 0});
}

ccColor4F rgba(ccColor3B c, float a) { return {c.r / 255.f, c.g / 255.f, c.b / 255.f, a}; }
ccColor4F rgba(int r, int g, int b, float a) { return {r / 255.f, g / 255.f, b / 255.f, a}; }

std::vector<CCPoint> roundedRectPoints(float w, float h, float r, int steps) {
    r = std::min(r, std::min(w, h) / 2.f);
    std::vector<CCPoint> pts;
    const CCPoint centres[4] = {{w - r, r}, {w - r, h - r}, {r, h - r}, {r, r}};
    const float startAngle[4] = {-kPi / 2.f, 0.f, kPi / 2.f, kPi};
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i <= steps; ++i) {
            const float a = startAngle[c] + (kPi / 2.f) * static_cast<float>(i) / static_cast<float>(steps);
            pts.push_back({centres[c].x + std::cos(a) * r, centres[c].y + std::sin(a) * r});
        }
    }
    return dedupe(std::move(pts));
}

CCNode* card(CCSize size, float radius, ccColor4F fill, ccColor4F border, float borderWidth) {
    auto* node = sized(size);
    auto* draw = CCDrawNode::create();
    auto pts = roundedRectPoints(size.width, size.height, radius);
    draw->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), fill, borderWidth, border);
    node->addChild(draw);
    return node;
}

CCNode* gradientPill(CCSize size, float radius, ccColor3B from, ccColor3B to, GLubyte opacity) {
    auto* node = sized(size);
    auto* draw = CCDrawNode::create();
    drawGradientRoundedRect(draw, size.width, size.height, radius, from, to, opacity / 255.f);
    // Thin light outline, like glass buttons.
    auto pts = roundedRectPoints(size.width, size.height, radius, 8);
    draw->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), ccColor4F{0, 0, 0, 0}, .45f, rgba(255, 255, 255, .35f));
    node->addChild(draw);
    return node;
}

CCLabelTTF* text(const std::string& s, float size, ccColor3B color, int weight) {
    auto* l = CCLabelTTF::create(s.c_str(), weight ? kFontBold : kFontRegular, size);
    l->setColor(color);
    return l;
}

void setTextIfChanged(CCLabelTTF* label, const std::string& s) {
    if (!label) return;
    const char* cur = label->getString();
    if (cur && s == cur) return;
    label->setString(s.c_str());
}

CCMenuItemSpriteExtra* button(const std::string& caption, CCSize size, Style style, CCObject* target, SEL_MenuHandler sel,
                              float fontSize) {
    CCNode* bg = nullptr;
    ccColor3B textColor = kText;
    switch (style) {
        case Style::Primary: bg = gradientPill(size, size.height / 2.f, kTeal, kViolet); break;
        case Style::Secondary: bg = card(size, size.height / 2.f, rgba(40, 46, 80, .9f), rgba(kViolet, .55f), .8f); break;
        case Style::Danger: bg = gradientPill(size, size.height / 2.f, {255, 70, 110}, {190, 50, 150}); break;
        case Style::Ghost:
            bg = card(size, size.height / 2.f, rgba(0, 0, 0, 0.f), rgba(kMuted, .45f), .7f);
            textColor = kMuted;
            break;
    }
    auto* label = text(caption, fontSize, textColor, 1);
    const float maxW = size.width - 8.f;
    if (label->getContentSize().width > maxW) label->setScale(maxW / label->getContentSize().width);
    label->setPosition({size.width / 2.f, size.height / 2.f});
    bg->addChild(label);
    auto* item = CCMenuItemSpriteExtra::create(bg, target, sel);
    item->m_scaleMultiplier = 1.06f;  // subtle press feedback for wide buttons
    return item;
}

CCNode* chip(const std::string& caption, ccColor3B color, float fontSize) {
    auto* label = text(caption, fontSize, color, 1);
    const CCSize size{label->getContentSize().width + 10.f, fontSize + 6.f};
    auto* node = card(size, size.height / 2.f, rgba(color, .14f), rgba(color, .55f), .6f);
    label->setPosition({size.width / 2.f, size.height / 2.f});
    node->addChild(label);
    return node;
}

CCNode* avatar(const std::string& name, ccColor3B color, float radius) {
    auto* node = sized({radius * 2.f, radius * 2.f});
    auto* draw = CCDrawNode::create();
    fillCircle(draw, {radius, radius}, radius, rgba(color, .25f));
    fillCircle(draw, {radius, radius}, radius - 1.2f, rgba(18, 20, 40, 1.f));
    fillCircle(draw, {radius, radius}, radius - 2.2f, rgba(color, .35f));
    node->addChild(draw);
    std::string initial = name.empty() ? "?" : name.substr(0, 1);
    for (auto& ch : initial) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    auto* l = text(initial, radius * 1.05f, kText, 1);
    l->setPosition({radius, radius});
    node->addChild(l);
    return node;
}

// ---------- Toggle ----------
Toggle* Toggle::create(bool on) {
    auto* ret = new Toggle();
    if (ret->init(on)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}
bool Toggle::init(bool on) {
    if (!CCNode::init()) return false;
    setContentSize({30.f, 15.f});
    setAnchorPoint({.5f, .5f});
    m_draw = CCDrawNode::create();
    addChild(m_draw);
    setOn(on);
    return true;
}
void Toggle::setOn(bool on) {
    m_on = on;
    m_draw->clear();
    const float w = 30.f, h = 15.f, r = h / 2.f;
    if (on) {
        drawGradientRoundedRect(m_draw, w, h, r, kTeal, kViolet, 1.f);
    } else {
        auto pts = roundedRectPoints(w, h, r);
        m_draw->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), rgba(60, 64, 100, 1.f), .6f, rgba(kMuted, .4f));
    }
    const float knobX = on ? w - r : r;
    fillCircle(m_draw, {knobX, r}, r - 2.f, rgba(245, 246, 255, 1.f));
}

// ---------- Gauge ----------
Gauge* Gauge::create(float radius) {
    auto* ret = new Gauge();
    if (ret->init(radius)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}
bool Gauge::init(float radius) {
    if (!CCNode::init()) return false;
    m_radius = radius;
    setContentSize({radius * 2.f, radius * 2.f});
    setAnchorPoint({.5f, .5f});
    m_draw = CCDrawNode::create();
    addChild(m_draw);
    m_centre = text("-", radius * .42f, kText, 1);
    m_centre->setPosition({radius, radius + radius * .1f});
    addChild(m_centre);
    m_caption = text("", radius * .22f, kMuted);
    m_caption->setPosition({radius, radius - radius * .32f});
    addChild(m_caption);
    setValue(0.f, kViolet, "-", "");
    return true;
}
void Gauge::setValue(float v, ccColor3B color, const std::string& centre, const std::string& caption) {
    v = std::clamp(v, 0.f, 1.f);
    m_draw->clear();
    const CCPoint c{m_radius, m_radius};
    fillCircle(m_draw, c, m_radius, rgba(40, 46, 80, .55f));
    fillCircle(m_draw, c, m_radius - 6.f, rgba(16, 18, 36, 1.f));
    const float ringR = m_radius - 3.f;
    const int segs = 64;
    // Track, then the value arc clockwise from the top.
    for (int i = 0; i < segs; ++i) {
        const float a0 = kPi / 2.f - 2.f * kPi * i / segs, a1 = kPi / 2.f - 2.f * kPi * (i + 1) / segs;
        m_draw->drawSegment({c.x + std::cos(a0) * ringR, c.y + std::sin(a0) * ringR},
                            {c.x + std::cos(a1) * ringR, c.y + std::sin(a1) * ringR}, 1.2f, rgba(70, 76, 120, .7f));
    }
    const int filled = static_cast<int>(segs * v + .5f);
    for (int i = 0; i < filled; ++i) {
        const float t = static_cast<float>(i) / segs;
        ccColor3B mix{static_cast<GLubyte>(kTeal.r + (color.r - kTeal.r) * t), static_cast<GLubyte>(kTeal.g + (color.g - kTeal.g) * t),
                      static_cast<GLubyte>(kTeal.b + (color.b - kTeal.b) * t)};
        const float a0 = kPi / 2.f - 2.f * kPi * i / segs, a1 = kPi / 2.f - 2.f * kPi * (i + 1) / segs;
        m_draw->drawSegment({c.x + std::cos(a0) * ringR, c.y + std::sin(a0) * ringR},
                            {c.x + std::cos(a1) * ringR, c.y + std::sin(a1) * ringR}, 2.2f, rgba(mix, 1.f));
    }
    if (filled > 0) {  // knob at the end of the arc
        const float a = kPi / 2.f - 2.f * kPi * v;
        fillCircle(m_draw, {c.x + std::cos(a) * ringR, c.y + std::sin(a) * ringR}, 3.6f, rgba(color, 1.f));
        fillCircle(m_draw, {c.x + std::cos(a) * ringR, c.y + std::sin(a) * ringR}, 1.6f, rgba(255, 255, 255, 1.f));
    }
    setTextIfChanged(m_centre, centre);
    setTextIfChanged(m_caption, caption);
}

// ---------- Chart ----------
Chart* Chart::create(CCSize size, std::size_t capacity) {
    auto* ret = new Chart();
    if (ret->init(size, capacity)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}
bool Chart::init(CCSize size, std::size_t capacity) {
    if (!CCNode::init()) return false;
    setContentSize(size);
    setAnchorPoint({.5f, .5f});
    m_capacity = std::max<std::size_t>(capacity, 2);
    m_draw = CCDrawNode::create();
    addChild(m_draw);
    for (int i = 0; i < 3; ++i) {
        auto* l = text("", 6.f, kMuted);
        l->setAnchorPoint({0.f, 0.f});
        addChild(l);
        m_refLabels.push_back(l);
    }
    setData({}, 100.f);
    return true;
}
void Chart::setData(const std::vector<float>& values, float minTop) {
    const auto size = getContentSize();
    const float w = size.width, h = size.height;
    m_draw->clear();
    auto pts = roundedRectPoints(w, h, 6.f);
    m_draw->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), rgba(16, 18, 38, .85f), .6f, rgba(kViolet, .3f));

    float top = minTop;
    for (float v : values) top = std::max(top, v * 1.2f);
    const float refs[3] = {top * .25f, top * .5f, top * .75f};
    for (int i = 0; i < 3; ++i) {
        const float y = refs[i] / top * h;
        m_draw->drawSegment({4.f, y}, {w - 4.f, y}, .25f, rgba(kMuted, .18f));
        m_refLabels[i]->setPosition({4.f, y + 1.f});
        setTextIfChanged(m_refLabels[i], std::to_string(static_cast<int>(refs[i] + .5f)));
    }
    if (values.size() < 2) return;
    const float step = (w - 8.f) / static_cast<float>(m_capacity - 1);
    const float x0 = w - 4.f - step * static_cast<float>(values.size() - 1);
    auto yOf = [&](float v) { return std::clamp(v / top, 0.f, 1.f) * (h - 6.f) + 3.f; };
    // Area under the line: teal at the bottom fading into violet.
    for (std::size_t i = 1; i < values.size(); ++i) {
        const float xa = x0 + step * static_cast<float>(i - 1), xb = x0 + step * static_cast<float>(i) + .3f;
        const float t = static_cast<float>(i) / static_cast<float>(values.size());
        ccColor3B mix{static_cast<GLubyte>(kTeal.r + (kViolet.r - kTeal.r) * t), static_cast<GLubyte>(kTeal.g + (kViolet.g - kTeal.g) * t),
                      static_cast<GLubyte>(kTeal.b + (kViolet.b - kTeal.b) * t)};
        CCPoint quad[4] = {{xa, 3.f}, {xb, 3.f}, {xb, yOf(values[i])}, {xa, yOf(values[i - 1])}};
        m_draw->drawPolygon(quad, 4, rgba(mix, .28f), 0.f, ccColor4F{0, 0, 0, 0});
    }
    for (std::size_t i = 1; i < values.size(); ++i) {
        CCPoint a{x0 + step * static_cast<float>(i - 1), yOf(values[i - 1])};
        CCPoint b{x0 + step * static_cast<float>(i), yOf(values[i])};
        m_draw->drawSegment(a, b, 2.f, rgba(kViolet, .22f));  // glow
        m_draw->drawSegment(a, b, .8f, rgba(170, 230, 255, 1.f));
    }
    const CCPoint last{x0 + step * static_cast<float>(values.size() - 1), yOf(values.back())};
    fillCircle(m_draw, last, 2.4f, rgba(kTeal, 1.f));
}

// ---------- Meter ----------
Meter* Meter::create(CCSize size) {
    auto* ret = new Meter();
    if (ret->init(size)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}
bool Meter::init(CCSize size) {
    if (!CCNode::init()) return false;
    setContentSize(size);
    setAnchorPoint({.5f, .5f});
    m_draw = CCDrawNode::create();
    addChild(m_draw);
    setValue(0.f, kTeal);
    return true;
}
void Meter::setValue(float v, ccColor3B color) {
    v = std::clamp(v, 0.f, 1.f);
    const auto s = getContentSize();
    m_draw->clear();
    auto track = roundedRectPoints(s.width, s.height, s.height / 2.f, 4);
    m_draw->drawPolygon(track.data(), static_cast<unsigned int>(track.size()), rgba(50, 56, 92, .8f), 0.f, ccColor4F{0, 0, 0, 0});
    const float fw = std::max(s.height, s.width * v);
    if (v > 0.f) drawGradientRoundedRect(m_draw, fw, s.height, s.height / 2.f, kTeal, color, 1.f);
}

} // namespace nexlevel::mod::kit

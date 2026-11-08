#pragma once
// NexLevel UI kit: a modern dark "glass" look (rounded cards, teal/purple gradients, pills,
// toggles, gauges, charts), drawn with cocos2d primitives instead of GD's button sprites.
// cocos2d APIs: CCDrawNode (drawPolygon/drawDot/drawSegment), CCClippingNode, CCLayerGradient,
// CCLabelTTF, CCMenuItemSpriteExtra (GD) for touch handling.
#include <Geode/Geode.hpp>

#include <string>
#include <vector>

namespace nexlevel::mod::kit {

// Palette (matches the reference: deep navy, glass cards, teal -> violet accents).
inline const cocos2d::ccColor3B kText{235, 238, 255};
inline const cocos2d::ccColor3B kMuted{150, 156, 190};
inline const cocos2d::ccColor3B kTeal{20, 200, 180};
inline const cocos2d::ccColor3B kViolet{140, 90, 255};
inline const cocos2d::ccColor3B kDanger{255, 80, 110};
inline const cocos2d::ccColor3B kGood{90, 230, 150};
inline const cocos2d::ccColor3B kWarn{255, 200, 80};

cocos2d::ccColor4F rgba(cocos2d::ccColor3B c, float a = 1.f);
cocos2d::ccColor4F rgba(int r, int g, int b, float a = 1.f);

// Rounded rectangle polygon (w x h, origin bottom-left).
std::vector<cocos2d::CCPoint> roundedRectPoints(float w, float h, float r, int stepsPerCorner = 6);

// A node of the given size (anchor centre) holding a rounded rect.
cocos2d::CCNode* card(cocos2d::CCSize size, float radius, cocos2d::ccColor4F fill, cocos2d::ccColor4F border, float borderWidth = 1.f);
// Rounded rect filled with a horizontal gradient (anchor centre).
cocos2d::CCNode* gradientPill(cocos2d::CCSize size, float radius, cocos2d::ccColor3B from, cocos2d::ccColor3B to, GLubyte opacity = 255);

// Modern text (system UI font). weight: 0 regular, 1 semibold.
cocos2d::CCLabelTTF* text(const std::string& s, float size, cocos2d::ccColor3B color = kText, int weight = 0);

enum class Style { Primary, Secondary, Danger, Ghost };
// A button: rounded, gradient for Primary. Tag/userObject are free for the caller.
CCMenuItemSpriteExtra* button(const std::string& caption, cocos2d::CCSize size, Style style, cocos2d::CCObject* target,
                              cocos2d::SEL_MenuHandler sel, float fontSize = 9.f);
// Small rounded label ("HOST", "32 ms").
cocos2d::CCNode* chip(const std::string& caption, cocos2d::ccColor3B color, float fontSize = 7.f);
// Circle with the first letter of a name.
cocos2d::CCNode* avatar(const std::string& name, cocos2d::ccColor3B color, float radius);

// iOS-style switch. Wrap it in a CCMenuItemSpriteExtra to make it clickable.
class Toggle : public cocos2d::CCNode {
public:
    static Toggle* create(bool on);
    void setOn(bool on);
    bool isOn() const { return m_on; }

private:
    bool init(bool on);
    cocos2d::CCDrawNode* m_draw = nullptr;
    bool m_on = false;
};

// Ring gauge (0..1) with a value label in the middle.
class Gauge : public cocos2d::CCNode {
public:
    static Gauge* create(float radius);
    void setValue(float v01, cocos2d::ccColor3B color, const std::string& centre, const std::string& caption);

private:
    bool init(float radius);
    float m_radius = 30.f;
    cocos2d::CCDrawNode* m_draw = nullptr;
    cocos2d::CCLabelTTF* m_centre = nullptr;
    cocos2d::CCLabelTTF* m_caption = nullptr;
};

// Area + line chart of recent values (newest on the right).
class Chart : public cocos2d::CCNode {
public:
    static Chart* create(cocos2d::CCSize size, std::size_t capacity);
    void setData(const std::vector<float>& values, float minTop);

private:
    bool init(cocos2d::CCSize size, std::size_t capacity);
    cocos2d::CCDrawNode* m_draw = nullptr;
    std::size_t m_capacity = 60;
    std::vector<cocos2d::CCLabelTTF*> m_refLabels;
};

// Horizontal meter (e.g. ping bar), value 0..1.
class Meter : public cocos2d::CCNode {
public:
    static Meter* create(cocos2d::CCSize size);
    void setValue(float v01, cocos2d::ccColor3B color);

private:
    bool init(cocos2d::CCSize size);
    cocos2d::CCDrawNode* m_draw = nullptr;
};

// Keep a CCLabelTTF's text without re-rendering when unchanged (TTF re-render is costly).
void setTextIfChanged(cocos2d::CCLabelTTF* label, const std::string& s);

} // namespace nexlevel::mod::kit

#pragma once
// Connection details: role, how you are connected, IP (hidden until tapped), port, internet
// quality (Demon / Insane / High / Normal / Bad) and a live ping graph.
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <vector>

namespace nexlevel::mod {

class InfoPopup : public geode::Popup {
public:
    static InfoPopup* create();

protected:
    bool init();
    void rebuild();
    void refresh(float);
    void onToggleIp(cocos2d::CCObject*);

    template <class T>
    T* track(T* node) {
        m_dynamic.push_back(node);
        return node;
    }

    std::vector<cocos2d::CCNode*> m_dynamic;
    cocos2d::CCDrawNode* m_graph = nullptr;
    bool m_showIp = false;
};

} // namespace nexlevel::mod

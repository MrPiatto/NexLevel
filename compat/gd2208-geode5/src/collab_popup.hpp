#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <string>
#include <vector>

namespace nexlevel::mod {

// Central purple/black NexLevel menu, opened from the editor pause menu.
class CollabPopup : public geode::Popup {
public:
    static CollabPopup* create();

protected:
    bool init();
    void rebuild();
    void refresh(float);
    std::string signature() const;

    void onHost(cocos2d::CCObject*);
    void onJoin(cocos2d::CCObject*);
    void onLeave(cocos2d::CCObject*);
    void onKick(cocos2d::CCObject* sender);

    template <class T>
    T* track(T* node) {
        m_dynamic.push_back(node);
        return node;
    }

    std::vector<cocos2d::CCNode*> m_dynamic;
    geode::TextInput* m_ipInput = nullptr;
    std::string m_shown;
};

} // namespace nexlevel::mod

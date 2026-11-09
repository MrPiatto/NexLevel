#include "ui_common.hpp"

using namespace geode::prelude;

namespace nexlevel::mod::ui {

ccColor3B peerColor(std::uint16_t peer) {
    static const ccColor3B colors[] = {
        {140, 90, 255}, {20, 200, 180}, {255, 110, 200}, {90, 230, 150}, {255, 200, 80}, {255, 120, 120}, {120, 160, 255}, {200, 255, 110},
    };
    return colors[peer % (sizeof(colors) / sizeof(colors[0]))];
}

ccColor3B pingColor(int ms) {
    if (ms < 70) return {110, 255, 140};
    if (ms < 150) return {255, 220, 90};
    return {255, 90, 110};
}

ccColor3B qualityColor(session::Quality q) {
    switch (q) {
        case session::Quality::Demon: return {255, 70, 90};
        case session::Quality::Insane: return {255, 120, 220};
        case session::Quality::High: return {255, 170, 60};
        case session::Quality::Normal: return {90, 220, 255};
        case session::Quality::Bad: return {150, 150, 150};
        case session::Quality::Unknown: break;
    }
    return {170, 160, 190};
}

} // namespace nexlevel::mod::ui

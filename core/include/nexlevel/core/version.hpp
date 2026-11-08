#pragma once
// NexLevel core - Geode-free. Do not include Geode or cocos2d headers anywhere under core/.
#include <cstdint>
#include <string_view>

namespace nexlevel {

// Bumped on any incompatible wire-format change. Identical on all game targets.
inline constexpr std::uint16_t kProtocolVersion = 6;  // v6: ping + account ids (v5: presence, v4: encryption, v3: chunked join)

// Game line a build targets. V1 rule: host and client must match exactly.
enum class GameTarget : std::uint8_t {
    Unknown = 0,
    GD2207 = 1,  // GD 2.2074, Geode v4
    GD2208 = 2,  // GD 2.2081, Geode v5
    GD2209 = 3,  // GD 2.209x, Geode v6
};

constexpr std::string_view toString(GameTarget t) noexcept {
    switch (t) {
        case GameTarget::GD2207: return "GD 2.207x";
        case GameTarget::GD2208: return "GD 2.208x";
        case GameTarget::GD2209: return "GD 2.209x";
        default: return "unknown";
    }
}

constexpr bool canShareSession(GameTarget a, GameTarget b) noexcept {
    return a != GameTarget::Unknown && a == b;
}

} // namespace nexlevel

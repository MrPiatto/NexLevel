#pragma once
// Spectating playtests: the latest icon state of each playtesting player and the trail they leave.
#include <nexlevel/core/session/messages.hpp>

#include <chrono>
#include <map>
#include <utility>
#include <vector>

namespace nexlevel::session {

struct PlayTrail {
    PlayerState last;                                // latest state (last.playing() false once they stopped)
    std::vector<std::pair<float, float>> points1;    // player 1 path of the current/last test
    std::vector<std::pair<float, float>> points2;    // player 2 (dual) path
    std::chrono::steady_clock::time_point updated{};
};

// Folds a received state into the trails (new session = new trail; points thinned and capped).
void recordPlayerState(std::map<sync::PeerId, PlayTrail>& trails, const PlayerState& s);

inline constexpr std::size_t kMaxTrailPoints = 8000;

} // namespace nexlevel::session

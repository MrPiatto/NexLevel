#include <nexlevel/core/session/playtest.hpp>

#include <cmath>

namespace nexlevel::session {

namespace {
void addPoint(std::vector<std::pair<float, float>>& pts, float x, float y) {
    if (!pts.empty()) {
        const float dx = x - pts.back().first, dy = y - pts.back().second;
        if (dx * dx + dy * dy < 4.f) return;  // thin: at most one point per 2 units
    }
    pts.emplace_back(x, y);
    if (pts.size() > kMaxTrailPoints) pts.erase(pts.begin(), pts.begin() + static_cast<std::ptrdiff_t>(pts.size() - kMaxTrailPoints));
}
} // namespace

void recordPlayerState(std::map<sync::PeerId, PlayTrail>& trails, const PlayerState& s) {
    auto& t = trails[s.peer];
    if (t.last.session != s.session) {  // a new playtest: start a new trail
        t.points1.clear();
        t.points2.clear();
    }
    t.last = s;
    t.updated = std::chrono::steady_clock::now();
    if (!s.playing()) return;
    addPoint(t.points1, s.x1, s.y1);
    if (s.flags & PlayerState::Dual) addPoint(t.points2, s.x2, s.y2);
}

} // namespace nexlevel::session

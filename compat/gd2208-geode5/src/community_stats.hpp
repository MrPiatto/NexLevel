#pragma once
// Public NexLevel numbers for the main-menu NexLevel menu: downloads (GitHub releases + Geode index)
// and how many players / rooms are live on the official relays right now.
#include <optional>

namespace nexlevel::mod {

struct CommunityStats {
    std::optional<int> downloads;  // total .geode downloads (not unique players)
    std::optional<int> online;     // hosts + players connected through the official relays
    std::optional<int> rooms;      // rooms open on the official relays
};

const CommunityStats& communityStats();
void refreshCommunityStats();  // at most once a minute; results arrive asynchronously

} // namespace nexlevel::mod

#pragma once
// Level-wide settings (start mode/speed, background, ground, fonts, guidelines, the base colour
// channels, song) travel as ONE special replica object with a reserved id, so they get the same
// ordering, last-writer-wins and late-join snapshot as everything else.
//
// Props layout (each setting is its own prop, so two players changing different settings never
// overwrite each other):
//   header key "k<L><n>" (e.g. kA2, kS38 legacy colours)  -> 100000 + (L-'A')*10000 + n
//   colour channel c of kS38 (one ColorAction string each) -> 500000 + c
//   song: audio track / song id / song ids / sfx ids          -> 600001..600004
#include <nexlevel/core/sync/netid.hpp>
#include <nexlevel/core/sync/object_props.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nexlevel::sync {

// Host-namespace id no allocator ever reaches (counters start at 1).
inline constexpr NetId kLevelSettingsId = NetId::make(1, NetId::kCounterMask);
inline constexpr bool isLevelSettings(NetId id) noexcept { return id == kLevelSettingsId; }

struct SongFields {
    int audioTrack = 0;
    int songID = 0;
    std::string songIDs;
    std::string sfxIDs;
    bool operator==(const SongFields&) const = default;
};

// header: LevelSettingsObject::getSaveString() ("kA2,0,kA4,1,...,kS38,1_255_..._6_1000_...|...").
PropMap levelSettingsToProps(std::string_view header, const SongFields& song);

struct LevelSettingsData {
    std::vector<std::pair<std::string, std::string>> header;  // key -> value, kS38 reassembled
    SongFields song;
};
LevelSettingsData propsToLevelSettings(const PropMap& props);

// Header keys that are editor UI state, not level content (never synced).
bool isLocalOnlySettingKey(std::string_view key) noexcept;

} // namespace nexlevel::sync

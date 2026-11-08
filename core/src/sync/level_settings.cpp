#include <nexlevel/core/sync/level_settings.hpp>

#include <charconv>
#include <map>

namespace nexlevel::sync {

namespace {
constexpr int kHeaderBase = 100000;
constexpr int kColorBase = 500000;
constexpr int kSongAudioTrack = 600001, kSongID = 600002, kSongIDs = 600003, kSfxIDs = 600004;

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        auto end = s.find(sep, start);
        if (end == std::string_view::npos) end = s.size();
        out.push_back(s.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

std::optional<int> toInt(std::string_view s) {
    int v = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || p != s.data() + s.size()) return std::nullopt;
    return v;
}

// "kA13" -> 100013; nullopt for anything else.
std::optional<int> headerKeyToProp(std::string_view key) {
    if (key.size() < 3 || key[0] != 'k' || key[1] < 'A' || key[1] > 'Z') return std::nullopt;
    auto n = toInt(key.substr(2));
    if (!n || *n < 0 || *n >= 10000) return std::nullopt;
    return kHeaderBase + (key[1] - 'A') * 10000 + *n;
}

std::string propToHeaderKey(int prop) {
    const int rel = prop - kHeaderBase;
    return std::string("k") + static_cast<char>('A' + rel / 10000) + std::to_string(rel % 10000);
}

// Channel id of one ColorAction save string ("1_255_2_255_..._6_1000_..."): the value of key 6.
std::optional<int> colorChannel(std::string_view seg) {
    auto parts = split(seg, '_');
    for (std::size_t i = 0; i + 1 < parts.size(); i += 2) {
        if (parts[i] == "6") return toInt(parts[i + 1]);
    }
    return std::nullopt;
}
} // namespace

bool isLocalOnlySettingKey(std::string_view key) noexcept {
    return key == "kS39";  // colour page shown in the editor's colour list
}

PropMap levelSettingsToProps(std::string_view header, const SongFields& song) {
    PropMap props;
    auto parts = split(header, ',');
    for (std::size_t i = 0; i + 1 < parts.size(); i += 2) {
        const auto key = parts[i];
        const auto value = parts[i + 1];
        if (isLocalOnlySettingKey(key)) continue;
        if (key == "kS38") {
            for (auto seg : split(value, '|')) {
                if (seg.empty()) continue;
                if (auto ch = colorChannel(seg)) props[kColorBase + *ch] = std::string(seg);
            }
            continue;
        }
        if (auto p = headerKeyToProp(key)) props[*p] = std::string(value);
    }
    props[kSongAudioTrack] = std::to_string(song.audioTrack);
    props[kSongID] = std::to_string(song.songID);
    props[kSongIDs] = song.songIDs;
    props[kSfxIDs] = song.sfxIDs;
    return props;
}

LevelSettingsData propsToLevelSettings(const PropMap& props) {
    LevelSettingsData d;
    std::string colors;
    for (const auto& [k, v] : props) {
        if (k >= kHeaderBase && k < kColorBase) d.header.emplace_back(propToHeaderKey(k), v);
        else if (k >= kColorBase && k < kSongAudioTrack) colors += v + "|";
        else if (k == kSongAudioTrack) d.song.audioTrack = toInt(v).value_or(0);
        else if (k == kSongID) d.song.songID = toInt(v).value_or(0);
        else if (k == kSongIDs) d.song.songIDs = v;
        else if (k == kSfxIDs) d.song.sfxIDs = v;
    }
    if (!colors.empty()) d.header.emplace_back("kS38", std::move(colors));
    return d;
}

} // namespace nexlevel::sync

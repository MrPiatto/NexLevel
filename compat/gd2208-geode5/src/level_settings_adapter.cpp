// GD 2.2081 / Geode v5. Fields and functions from geode-sdk/bindings 2.2081/GeometryDash.bro:
// LevelSettingsObject (getSaveString, the "// property kXX" fields, m_effectManager, m_level),
// GJEffectManager::getColorAction / setColorAction, ColorAction::create / setupFromString,
// GJGameLevel m_audioTrack / m_songID / m_songIDs / m_sfxIDs,
// LevelEditorLayer::levelSettingsUpdated, GJBaseGameLayer::updateLevelColors / m_effectManager.
// LevelSettingsObject::objectFromString is inline on Windows, so values are applied field by field
// (the table below is generated from the bindings' property comments).
#include "level_settings_adapter.hpp"

#include <charconv>
#include <functional>
#include <string>
#include <unordered_map>

using namespace geode::prelude;
namespace nls = nexlevel::sync;

namespace nexlevel::mod {

namespace {

int toInt(std::string const& v) {
    int out = 0;
    std::from_chars(v.data(), v.data() + v.size(), out);
    return out;
}
float toFloat(std::string const& v) {
    try { return std::stof(v); } catch (...) { return 0.f; }
}

using Setter = void (*)(LevelSettingsObject*, std::string const&);

const std::unordered_map<std::string, Setter>& setters() {
    static const std::unordered_map<std::string, Setter> table = {
        {"kA2", +[](LevelSettingsObject* s, std::string const& v) { s->m_startMode = toInt(v); }},
        {"kA4", +[](LevelSettingsObject* s, std::string const& v) { s->m_startSpeed = static_cast<Speed>(toInt(v)); }},
        {"kA3", +[](LevelSettingsObject* s, std::string const& v) { s->m_startMini = toInt(v) != 0; }},
        {"kA8", +[](LevelSettingsObject* s, std::string const& v) { s->m_startDual = toInt(v) != 0; }},
        {"kA28", +[](LevelSettingsObject* s, std::string const& v) { s->m_mirrorMode = toInt(v) != 0; }},
        {"kA29", +[](LevelSettingsObject* s, std::string const& v) { s->m_rotateGameplay = toInt(v) != 0; }},
        {"kA10", +[](LevelSettingsObject* s, std::string const& v) { s->m_twoPlayerMode = toInt(v) != 0; }},
        {"kA22", +[](LevelSettingsObject* s, std::string const& v) { s->m_platformerMode = toInt(v) != 0; }},
        {"kA13", +[](LevelSettingsObject* s, std::string const& v) { s->m_songOffset = toFloat(v); }},
        {"kA15", +[](LevelSettingsObject* s, std::string const& v) { s->m_fadeIn = toInt(v) != 0; }},
        {"kA16", +[](LevelSettingsObject* s, std::string const& v) { s->m_fadeOut = toInt(v) != 0; }},
        {"kA46", +[](LevelSettingsObject* s, std::string const& v) { s->m_dontReset = toInt(v) != 0; }},
        {"kA6", +[](LevelSettingsObject* s, std::string const& v) { s->m_backgroundIndex = toInt(v); }},
        {"kA7", +[](LevelSettingsObject* s, std::string const& v) { s->m_groundIndex = toInt(v); }},
        {"kA18", +[](LevelSettingsObject* s, std::string const& v) { s->m_fontIndex = toInt(v); }},
        {"kA25", +[](LevelSettingsObject* s, std::string const& v) { s->m_middleGroundIndex = toInt(v); }},
        {"kA9", +[](LevelSettingsObject* s, std::string const& v) { s->m_startsWithStartPos = toInt(v) != 0; }},
        {"kA11", +[](LevelSettingsObject* s, std::string const& v) { s->m_isFlipped = toInt(v) != 0; }},
        {"kA20", +[](LevelSettingsObject* s, std::string const& v) { s->m_reverseGameplay = toInt(v) != 0; }},
        {"kA21", +[](LevelSettingsObject* s, std::string const& v) { s->m_disableStartPos = toInt(v) != 0; }},
        {"kA19", +[](LevelSettingsObject* s, std::string const& v) { s->m_targetOrder = toInt(v); }},
        {"kA26", +[](LevelSettingsObject* s, std::string const& v) { s->m_targetChannel = toInt(v); }},
        {"kA14", +[](LevelSettingsObject* s, std::string const& v) { s->m_guidelineString = v; }},
        {"kA17", +[](LevelSettingsObject* s, std::string const& v) { s->m_groundLineIndex = toInt(v); }},
        {"kA23", +[](LevelSettingsObject* s, std::string const& v) { s->m_propertykA23 = toInt(v) != 0; }},
        {"kA24", +[](LevelSettingsObject* s, std::string const& v) { s->m_propertykA24 = toInt(v) != 0; }},
        {"kA43", +[](LevelSettingsObject* s, std::string const& v) { s->m_noTimePenalty = toInt(v) != 0; }},
        {"kA44", +[](LevelSettingsObject* s, std::string const& v) { s->m_propertykA44 = toInt(v); }},
        {"kA47", +[](LevelSettingsObject* s, std::string const& v) { s->m_nextFreeID = toInt(v); }},
        {"kA35", +[](LevelSettingsObject* s, std::string const& v) { s->m_resetCamera = toInt(v) != 0; }},
        {"kA36", +[](LevelSettingsObject* s, std::string const& v) { s->m_spawnGroup = toInt(v); }},
        {"kA27", +[](LevelSettingsObject* s, std::string const& v) { s->m_allowMultiRotation = toInt(v) != 0; }},
        {"kA31", +[](LevelSettingsObject* s, std::string const& v) { s->m_enablePlayerSqueeze = toInt(v) != 0; }},
        {"kA32", +[](LevelSettingsObject* s, std::string const& v) { s->m_fixGravityBug = toInt(v) != 0; }},
        {"kA33", +[](LevelSettingsObject* s, std::string const& v) { s->m_fixNegativeScale = toInt(v) != 0; }},
        {"kA34", +[](LevelSettingsObject* s, std::string const& v) { s->m_fixRobotJump = toInt(v) != 0; }},
        {"kA37", +[](LevelSettingsObject* s, std::string const& v) { s->m_dynamicLevelHeight = toInt(v) != 0; }},
        {"kA38", +[](LevelSettingsObject* s, std::string const& v) { s->m_sortGroups = toInt(v) != 0; }},
        {"kA39", +[](LevelSettingsObject* s, std::string const& v) { s->m_fixRadiusCollision = toInt(v) != 0; }},
        {"kA40", +[](LevelSettingsObject* s, std::string const& v) { s->m_enable22Changes = toInt(v) != 0; }},
        {"kA41", +[](LevelSettingsObject* s, std::string const& v) { s->m_allowStaticRotate = toInt(v) != 0; }},
        {"kA42", +[](LevelSettingsObject* s, std::string const& v) { s->m_reverseSync = toInt(v) != 0; }},
        {"kA45", +[](LevelSettingsObject* s, std::string const& v) { s->m_decreaseBoostSlide = toInt(v) != 0; }},
        {"kA48", +[](LevelSettingsObject* s, std::string const& v) { s->m_enableImpulseFix = toInt(v) != 0; }},
    };
    return table;
}

nls::SongFields readSong(GJGameLevel* level) {
    nls::SongFields s;
    if (!level) return s;
    s.audioTrack = level->m_audioTrack;
    s.songID = level->m_songID;
    s.songIDs = level->m_songIDs;
    s.sfxIDs = level->m_sfxIDs;
    return s;
}

// One ColorAction save string per channel, applied to the effect managers the editor uses.
void applyColors(LevelEditorLayer* layer, LevelSettingsObject* settings, std::string const& kS38) {
    GJEffectManager* managers[2] = {settings->m_effectManager, layer->m_effectManager};
    std::size_t start = 0;
    while (start < kS38.size()) {
        auto end = kS38.find('|', start);
        if (end == std::string::npos) end = kS38.size();
        std::string seg = kS38.substr(start, end - start);
        start = end + 1;
        if (seg.empty()) continue;
        // Channel id = value of key 6.
        int channel = -1;
        std::size_t p = 0;
        int index = 0;
        std::string prevKey;
        while (p <= seg.size()) {
            auto q = seg.find('_', p);
            if (q == std::string::npos) q = seg.size();
            std::string tok = seg.substr(p, q - p);
            if (index % 2 == 1 && prevKey == "6") channel = toInt(tok);
            prevKey = tok;
            ++index;
            p = q + 1;
        }
        if (channel < 0) continue;
        for (int m = 0; m < 2; ++m) {
            auto* em = managers[m];
            if (!em || (m == 1 && em == managers[0])) continue;
            auto* action = em->getColorAction(channel);
            if (!action) {
                action = ColorAction::create();
                em->setColorAction(action, channel);
                action = em->getColorAction(channel);
                if (!action) continue;
            }
            action->setupFromString(seg);
        }
    }
}

} // namespace

nls::PropMap readLevelSettings(LevelEditorLayer* layer) {
    if (!layer || !layer->m_levelSettings) return {};
    std::string header = layer->m_levelSettings->getSaveString();
    return nls::levelSettingsToProps(header, readSong(layer->m_level));
}

void applyLevelSettings(LevelEditorLayer* layer, const nls::PropMap& props) {
    if (!layer || !layer->m_levelSettings) return;
    auto* settings = layer->m_levelSettings;
    auto data = nls::propsToLevelSettings(props);
    std::size_t applied = 0, unknown = 0;
    for (auto& [key, value] : data.header) {
        if (key == "kS38") {
            applyColors(layer, settings, value);
            ++applied;
            continue;
        }
        auto it = setters().find(key);
        if (it == setters().end()) { ++unknown; continue; }
        it->second(settings, value);
        ++applied;
    }
    if (auto* level = layer->m_level) {
        level->m_audioTrack = data.song.audioTrack;
        level->m_songID = data.song.songID;
        level->m_songIDs = data.song.songIDs;
        level->m_sfxIDs = data.song.sfxIDs;
    }
    // Same notifications the editor's own settings screen triggers.
    layer->levelSettingsUpdated();
    layer->updateLevelColors();
    log::info("NexLevel: applied level settings ({} values{})", applied,
              unknown ? fmt::format(", {} unknown keys skipped", unknown) : std::string());
}

} // namespace nexlevel::mod

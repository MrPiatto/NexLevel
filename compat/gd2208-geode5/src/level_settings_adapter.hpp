#pragma once
// Reads and applies level-wide settings (start options, background/ground/fonts, base colour
// channels, song) of the open editor. See core sync/level_settings.hpp for the format.
#include <Geode/Geode.hpp>

#include <nexlevel/core/sync/level_settings.hpp>

namespace nexlevel::mod {

sync::PropMap readLevelSettings(LevelEditorLayer* layer);
void applyLevelSettings(LevelEditorLayer* layer, const sync::PropMap& props);

} // namespace nexlevel::mod

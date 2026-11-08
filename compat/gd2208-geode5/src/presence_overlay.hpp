#pragma once
// Shows where the other players are looking: a coloured frame of their view with their name,
// drawn inside the editor's object layer so it moves and zooms with the level.
#include <Geode/Geode.hpp>

namespace nexlevel::mod {

// Call once per editor frame: sends this player's position and redraws the others.
void updatePresence(LevelEditorLayer* layer, bool paused);

} // namespace nexlevel::mod

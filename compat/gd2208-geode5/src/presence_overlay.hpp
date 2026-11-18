#pragma once
// Shows where the other players are looking: a coloured frame of their view with their name,
// drawn over the level (matching its position and zoom) but outside it, so camera effects leave it alone.
#include <Geode/Geode.hpp>

namespace nexlevel::mod {

// Call once per editor frame: sends this player's position and redraws the others.
void updatePresence(LevelEditorLayer* layer, bool paused, bool linked);

// Moves the editor camera to where another player is working. False if they are not in an editor.
bool focusOnPlayer(std::uint16_t peer);

} // namespace nexlevel::mod

#pragma once
// Full-screen black loading screen while joining a room: slides in from the right, shows the
// NexLevel logo and what is happening, and slides out to the left once the room is open in the
// editor (never before 5 seconds). Lives above every scene, so the editor loads behind it.
#include <string>

namespace nexlevel::mod {

void showJoinLoading();
void finishJoinLoading();  // the room is open: leave once the minimum time has passed
void cancelJoinLoading();  // joining failed: leave right away

} // namespace nexlevel::mod

#pragma once
// Discord status ("Rich Presence"): shows in Discord what you are doing in NexLevel.
namespace nexlevel::mod {
void updateDiscordPresence();  // main thread, called twice a second; the Discord IPC runs on its own thread
}

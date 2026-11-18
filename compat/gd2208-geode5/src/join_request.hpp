#pragma once
// Private rooms: the host's "<player> wants to join" panel (Accept / Decline).
#include <cstdint>
#include <functional>
#include <string>

namespace nexlevel::mod {

// Shows the panel; `answer(true/false)` is called once (Decline also when it is closed otherwise).
void showJoinRequestPanel(const std::string& name, std::function<void(bool)> answer);

} // namespace nexlevel::mod

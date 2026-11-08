#pragma once
// Which relays this NexLevel knows. Room codes name a relay by id (0..15), so many relays can run
// side by side (regions, load) and new ones can be added without updating the mod:
//   1. built-in list (compiled in),
//   2. replaced by the directory JSON fetched from the "relay-directory" URL (cached for offline),
//   3. plus the player's own relay from the "relay-server" setting (id 15).
// Directory JSON: {"relays":[{"id":1,"name":"EU-1","host":"relay.example.com","port":47800}]}
#include <nexlevel/core/relay/protocol.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nexlevel::mod {

std::vector<relay::RelayEndpoint> relayDirectory();
void refreshRelayDirectory();  // async; call at startup
std::optional<std::pair<std::string, std::uint16_t>> customRelay();

} // namespace nexlevel::mod

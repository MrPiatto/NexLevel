#pragma once
#include <optional>
#include <string>

namespace nexlevel::net {

// The machine's primary LAN IPv4 address (what other players on the same network connect to).
// Found by "connecting" a UDP socket towards a public address: no packet is sent, the OS just picks
// the outgoing interface. nullopt when there is no route (offline).
std::optional<std::string> primaryLocalIPv4();

} // namespace nexlevel::net

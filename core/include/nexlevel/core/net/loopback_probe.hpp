#pragma once
// Startup networking self-check: proves the core network stack (standalone Asio + NexLevel framing)
// works inside the host process on the current platform. Opens a TCP listener on 127.0.0.1
// (ephemeral port), connects to it, sends one framed message and checks the echo.
// Blocking: call it from a background thread, never from the game's main thread.
#include <chrono>
#include <cstdint>
#include <string>
#include <cstddef>

namespace nexlevel::net {

inline constexpr std::uint16_t kProbeFrameType = 0x7E57;

struct ProbeResult {
    bool ok = false;
    std::string error;              // empty when ok
    std::uint16_t port = 0;         // ephemeral port the listener got
    std::uint32_t roundTripMicros = 0;
    std::size_t bytesExchanged = 0; // frame bytes sent + received by the client
};

ProbeResult runLoopbackProbe(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));

} // namespace nexlevel::net

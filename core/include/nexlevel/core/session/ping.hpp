#pragma once
// Round-trip time to the host, measured every second, and a simple connection quality rating.
// Times are taken when frames reach/leave the network thread and the answering side reports how
// long it held the Ping, so the game's frame rate does not inflate the numbers.
#include <nexlevel/core/session/messages.hpp>

#include <chrono>
#include <deque>
#include <optional>
#include <vector>

namespace nexlevel::session {

struct PingStats {
    std::optional<float> ms;   // latest smoothed round trip
    float avgMs = 0;           // average of the history
    float jitterMs = 0;        // average change between samples
    float lossPercent = 0;     // pings never answered (recent)
    std::vector<float> history;  // last 60 samples, oldest first
};

// Named after GD difficulties, best first.
enum class Quality : std::uint8_t { Demon, Insane, High, Normal, Bad, Unknown };
Quality qualityFor(const PingStats& s) noexcept;
const char* toString(Quality q) noexcept;

// Internal helper (used by host and client).
class RttTracker {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::size_t kHistory = 60;

    // A Ping to send now, if one is due (every second).
    std::optional<Ping> due(Clock::time_point now);
    void onPong(const Pong& p, Clock::time_point arrival);
    PingStats stats() const;
    void reset() { *this = RttTracker{}; }

private:
    std::uint32_t m_nextSeq = 1;
    Clock::time_point m_lastSent{};
    std::deque<std::pair<std::uint32_t, Clock::time_point>> m_outstanding;
    std::optional<float> m_ewma;
    std::deque<float> m_history;
    std::deque<bool> m_answered;  // per ping, recent window
};

std::uint64_t nowMicros();
Pong answer(const Ping& p, RttTracker::Clock::time_point arrival);

} // namespace nexlevel::session

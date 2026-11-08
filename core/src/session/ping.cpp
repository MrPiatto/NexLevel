#include <nexlevel/core/session/ping.hpp>

#include <algorithm>
#include <cmath>

namespace nexlevel::session {

using Clock = RttTracker::Clock;

std::uint64_t nowMicros() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count());
}

Pong answer(const Ping& p, Clock::time_point arrival) {
    auto hold = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - arrival).count();
    return Pong{p.seq, p.sentMicros, static_cast<std::uint32_t>(std::clamp<long long>(hold, 0, 10'000'000))};
}

std::optional<Ping> RttTracker::due(Clock::time_point now) {
    if (now - m_lastSent < std::chrono::seconds(1)) return std::nullopt;
    m_lastSent = now;
    // Pings unanswered for 5 s count as lost.
    while (!m_outstanding.empty() && now - m_outstanding.front().second > std::chrono::seconds(5)) {
        m_outstanding.pop_front();
        m_answered.push_back(false);
    }
    while (m_answered.size() > kHistory) m_answered.pop_front();
    const auto seq = m_nextSeq++;
    m_outstanding.emplace_back(seq, now);
    if (m_outstanding.size() > 8) m_outstanding.pop_front();
    return Ping{seq, nowMicros()};
}

void RttTracker::onPong(const Pong& p, Clock::time_point arrival) {
    auto it = std::find_if(m_outstanding.begin(), m_outstanding.end(), [&](auto& o) { return o.first == p.seq; });
    if (it == m_outstanding.end()) return;  // unknown or too old
    const auto sent = it->second;
    m_outstanding.erase(it);
    float ms = std::chrono::duration<float, std::milli>(arrival - sent).count() - static_cast<float>(p.holdMicros) / 1000.f;
    ms = std::max(ms, 0.f);
    m_ewma = m_ewma ? *m_ewma * 0.6f + ms * 0.4f : ms;
    m_history.push_back(ms);
    while (m_history.size() > kHistory) m_history.pop_front();
    m_answered.push_back(true);
    while (m_answered.size() > kHistory) m_answered.pop_front();
}

PingStats RttTracker::stats() const {
    PingStats s;
    s.ms = m_ewma;
    s.history.assign(m_history.begin(), m_history.end());
    if (!m_history.empty()) {
        float sum = 0, jit = 0;
        for (std::size_t i = 0; i < m_history.size(); ++i) {
            sum += m_history[i];
            if (i) jit += std::fabs(m_history[i] - m_history[i - 1]);
        }
        s.avgMs = sum / static_cast<float>(m_history.size());
        s.jitterMs = m_history.size() > 1 ? jit / static_cast<float>(m_history.size() - 1) : 0.f;
    }
    if (!m_answered.empty()) {
        auto lost = std::count(m_answered.begin(), m_answered.end(), false);
        s.lossPercent = 100.f * static_cast<float>(lost) / static_cast<float>(m_answered.size());
    }
    return s;
}

Quality qualityFor(const PingStats& s) noexcept {
    if (!s.ms) return Quality::Unknown;
    const float score = s.avgMs + 2.f * s.jitterMs + 10.f * s.lossPercent;
    if (score < 35.f) return Quality::Demon;
    if (score < 70.f) return Quality::Insane;
    if (score < 120.f) return Quality::High;
    if (score < 200.f) return Quality::Normal;
    return Quality::Bad;
}

const char* toString(Quality q) noexcept {
    switch (q) {
        case Quality::Demon: return "Demon";
        case Quality::Insane: return "Insane";
        case Quality::High: return "High";
        case Quality::Normal: return "Normal";
        case Quality::Bad: return "Bad";
        case Quality::Unknown: break;
    }
    return "Measuring...";
}

} // namespace nexlevel::session

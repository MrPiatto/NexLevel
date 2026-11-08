// Built once per target. Must compile against Geode v4, v5 and v6:
// use only APIs present in all three, or guard with NEXLEVEL_GEODE_MAJOR.
#include <Geode/Geode.hpp>

#include <nexlevel/core/net/loopback_probe.hpp>
#include <nexlevel/core/version.hpp>

#include <thread>

using namespace geode::prelude;

#if !defined(NEXLEVEL_TARGET) || !defined(NEXLEVEL_GEODE_MAJOR)
#error "NEXLEVEL_TARGET and NEXLEVEL_GEODE_MAJOR must be defined by the compat target's CMakeLists.txt"
#endif

$execute {
    constexpr auto target = static_cast<nexlevel::GameTarget>(NEXLEVEL_TARGET);
    log::info("NexLevel loaded: target {}, Geode major {}, protocol v{}",
        nexlevel::toString(target), NEXLEVEL_GEODE_MAJOR, nexlevel::kProtocolVersion);

    // Phase 1 network probe, off the main thread (blocking by design, finishes in < 3 s).
    std::thread([] {
        auto r = nexlevel::net::runLoopbackProbe();
        if (r.ok) {
            log::info("NexLevel network probe OK: loopback TCP port {}, round trip {} us, {} bytes",
                r.port, r.roundTripMicros, r.bytesExchanged);
        } else {
            log::error("NexLevel network probe FAILED: {}", r.error);
        }
    }).detach();
}

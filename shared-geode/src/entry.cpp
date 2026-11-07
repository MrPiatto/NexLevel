// Built once per target. Must compile against Geode v4, v5 and v6:
// use only APIs present in all three, or guard with NEXLEVEL_GEODE_MAJOR.
#include <Geode/Geode.hpp>

#include <nexlevel/core/version.hpp>

using namespace geode::prelude;

#if !defined(NEXLEVEL_TARGET) || !defined(NEXLEVEL_GEODE_MAJOR)
#error "NEXLEVEL_TARGET and NEXLEVEL_GEODE_MAJOR must be defined by the compat target's CMakeLists.txt"
#endif

$execute {
    constexpr auto target = static_cast<nexlevel::GameTarget>(NEXLEVEL_TARGET);
    log::info("NexLevel loaded: target {}, Geode major {}, protocol v{}",
        nexlevel::toString(target), NEXLEVEL_GEODE_MAJOR, nexlevel::kProtocolVersion);
}

# deps/

Everything NexLevel downloads lives here; nothing is fetched outside NEXLEVEL.

- `geode-sdk/<tag>/` - one pinned Geode SDK checkout per target (tools/setup-sdks.ps1). Git-ignored.
- `.cpm-cache/` - CMake CPM package cache (CPM_SOURCE_CACHE). Git-ignored.
- Third-party sources (Asio, KCP, Monocypher, libjuice, miniupnpc) are added here, pinned, in the phase that first needs them.

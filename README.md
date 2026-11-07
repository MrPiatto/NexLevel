# NexLevel — by Forg3 Studios

Create Together. Build Beyond.

Real-time collaborative level editing for Geometry Dash, built on Geode. The host runs the session server inside the game; other players connect to it directly, with optional NAT traversal and relay fallback.

## Layout

| Path | Contents |
| --- | --- |
| `core/` | Geode-free C++20 core: protocol, sync, session, transport. CMake fails the build if it includes Geode or cocos2d. |
| `shared-geode/` | Geode code that compiles unchanged on all three targets; built once per target. |
| `compat/gd2207-geode4/` | GD 2.2074 / Geode v4 build: mod.json, CMake, version-specific hooks and adapter. |
| `compat/gd2208-geode5/` | GD 2.2081 / Geode v5 build. |
| `compat/gd2209-geode6/` | GD 2.209x / Geode v6 build. |
| `deps/` | Pinned SDKs and third-party sources (fetched, git-ignored). |
| `tools/` | Environment check, SDK setup, build, deploy, test scripts. |
| `tests/` | Integration and adapter-contract tests. |
| `docs/` | Setup and target docs. |
| `build/` | All build outputs (git-ignored). |

See `docs/SETUP.md` to build and `docs/TARGETS.md` for version status. No target is supported until it passes its compatibility tests.

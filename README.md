# NexLevel — by Forg3 Studios

Create Together. Build Beyond.

Real-time collaborative level editing for Geometry Dash, built on Geode.

**v1.0.0** - GD 2.2081, Windows (Geode v5).

- **Shared editor.** Objects and level settings (colours, background, ground, song, start
  options) sync live, with a background sweep that catches anything the hooks miss.
- **See each other.** Every player gets a colour: a frame shows where each one is working, and
  you can watch other players playtest with their icon and a trail.
- **Rooms.** Short codes, up to 16 players, kick, reconnection, and fast joins of very large
  levels.
- **Network.** End-to-end encrypted. Uses official relays (they work on any network, including
  mobile data and CGNAT; the nearest one is picked automatically), UPnP, or LAN. A player can
  also set a private relay.
- **NexLevel menu.** Tabs for Room, Players (ping, profiles), Network (quality gauge, ping
  chart) and Settings.

How it fits together: the host runs the session server inside the game, and players join with
a room code. When a direct connection is not possible, traffic goes through a relay, which only
ever sees encrypted data. See `docs/RELAY.md` and `docs/ONLINE.md`.

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

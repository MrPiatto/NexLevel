# Build targets

| Target | GD | Geode SDK (pinned) | Compat module | C++ | Status |
| --- | --- | --- | --- | --- | --- |
| gd2207 | 2.2074 | v4.10.2 | compat/gd2207-geode4 | 20 | Not yet built |
| gd2208 | 2.2081 | v5.10.1 | compat/gd2208-geode5 | 23 | Not yet built |
| gd2209 | 2.2091 | v6.0.0-alpha.1 | compat/gd2209-geode6 | 23 | Not yet built |

Rules
- A target is "supported" only after it passes the gates in the technical report (section 1A) on that platform.
- Pins live in tools/targets.ps1, compat/*/mod.json and .github/workflows/build-*.yml. Change all three together.
- V1 sessions: same target only (core: `canShareSession`).
- Index: each target needs its own version string (the index rejects duplicates). Scheme to be agreed with index admins before the first upload.

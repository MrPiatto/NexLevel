# Developer setup (Windows)

1. Run `tools/check-env.ps1` and fix anything MISSING (Git, CMake 3.21+, Visual Studio 2022 C++ tools, Geode CLI).
2. `tools/setup-sdks.ps1` - pinned Geode SDKs into deps/geode-sdk/.
3. `tools/test-core.ps1` - core unit tests.
4. `tools/build.ps1 -Target gd2208` (or gd2207 / gd2209) - output in build/out/<target>/.
5. `tools/deploy.ps1 -Target gd2208 -GdPath "<Geometry Dash folder>"` - copies the .geode into that install's geode/mods.

Each GD version needs its own Geometry Dash folder with the matching Geode installed.
macOS and iOS builds come from GitHub Actions (.github/workflows/build-*.yml).

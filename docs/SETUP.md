# Developer setup (Windows)

1. Install the toolchain Geode requires on Windows (Clang + Ninja, CMake 3.29+, Geode CLI). With Scoop, as the Geode docs recommend:
   ```powershell
   Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
   Invoke-RestMethod -Uri https://get.scoop.sh | Invoke-Expression
   scoop install cmake llvm ninja
   scoop bucket add extras
   scoop install geode-sdk-cli
   ```
   Visual Studio 2022 C++ build tools + Windows SDK are also required (already present on the dev PC).
   Open a NEW terminal afterwards, then run `geode config setup`.
2. `tools/check-env.ps1` - everything should read OK.
3. `tools/setup-sdks.ps1` - pinned Geode SDKs into deps/geode-sdk/.
4. `tools/test-core.ps1` - core unit tests.
5. `tools/build.ps1 -Target gd2208` (or gd2207 / gd2209) - output in build/out/<target>/.
6. `tools/deploy.ps1 -Target gd2208 -GdPath "<Geometry Dash folder>"` - copies the .geode into that install's geode/mods.

Each GD version needs its own Geometry Dash folder with the matching Geode installed.
macOS and iOS builds come from GitHub Actions (.github/workflows/build-*.yml).

# Builds and runs the Geode-free core tests on Windows (output stays in build/core-tests-win).
# Stops on a build failure so ctest never runs a stale test binary.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bld = Join-Path $root 'build/core-tests-win'
cmake -S (Join-Path $root 'core') -B $bld
if ($LASTEXITCODE) { throw "core configure failed" }
cmake --build $bld --config Debug
if ($LASTEXITCODE) { throw "core build failed - see the errors above" }
ctest --test-dir $bld -C Debug --output-on-failure
if ($LASTEXITCODE) { throw "core tests failed" }
Write-Host "Core tests passed" -ForegroundColor Green

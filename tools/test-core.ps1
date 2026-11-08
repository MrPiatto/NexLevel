# Builds and runs the Geode-free core tests on Windows (output stays in build/core-tests-win).
# Stops on a build failure so ctest never runs a stale test binary.
# The full output is also saved to build/core-tests-win/last-run.txt (so it can be read later).
# 'Continue': native tools write warnings to stderr, which must not abort the script.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$bld = Join-Path $root 'build/core-tests-win'
$log = Join-Path $bld 'last-run.txt'
New-Item -ItemType Directory -Force -Path $bld | Out-Null
"=== test-core $(Get-Date -Format s)" | Out-File $log -Encoding Unicode  # same encoding Tee-Object uses
cmake -S (Join-Path $root 'core') -B $bld 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE) { throw "core configure failed (see $log)" }
cmake --build $bld --config Debug 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE) { throw "core build failed - see the errors above (also in $log)" }
& (Join-Path $bld 'Debug/nexlevel_core_tests.exe') 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE) { throw "core tests failed (see $log)" }
Write-Host "Core tests passed" -ForegroundColor Green

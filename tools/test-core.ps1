# Builds and runs the Geode-free core tests on Windows (output stays in build/core-tests-win).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bld = Join-Path $root 'build/core-tests-win'
cmake -S (Join-Path $root 'core') -B $bld
cmake --build $bld --config Debug
ctest --test-dir $bld -C Debug --output-on-failure

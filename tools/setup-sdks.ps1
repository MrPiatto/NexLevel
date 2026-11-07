# Clones each target's pinned Geode SDK into NEXLEVEL/deps/geode-sdk/<tag> and fetches its binaries.
# Nothing is installed outside NEXLEVEL except what the Geode CLI itself stores in its own config.
param([string[]]$Only)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/targets.ps1"

if (-not (Get-Command geode -ErrorAction SilentlyContinue)) { throw "Geode CLI not found on PATH. Install it first (see docs/SETUP.md)." }

foreach ($name in $NexTargets.Keys) {
    if ($Only -and ($Only -notcontains $name)) { continue }
    $t = $NexTargets[$name]
    $sdk = Get-NexSdkPath $t.GeodeTag
    if (-not (Test-Path $sdk)) {
        Write-Host "[$name] cloning Geode $($t.GeodeTag) -> $sdk"
        git clone --depth 1 --branch $t.GeodeTag https://github.com/geode-sdk/geode $sdk
    } else {
        Write-Host "[$name] SDK already present: $sdk"
    }
    Write-Host "[$name] fetching prebuilt Geode binaries for this SDK"
    $env:GEODE_SDK = $sdk
    geode sdk install-binaries   # verify on first run that this resolves the SDK version from GEODE_SDK
}

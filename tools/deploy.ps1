# Copies a built target into a Geometry Dash install's geode/mods folder for in-game testing.
# The only file written outside NEXLEVEL is the mod's own .geode file in that folder.
param(
    [Parameter(Mandatory)][ValidateSet('gd2208','gd2209')][string]$Target,
    [Parameter(Mandatory)][string]$GdPath
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/targets.ps1"
if (-not (Test-Path (Join-Path $GdPath 'GeometryDash.exe'))) { throw "$GdPath does not contain GeometryDash.exe" }
$mods = Join-Path $GdPath 'geode/mods'
if (-not (Test-Path $mods)) { throw "Geode is not installed in $GdPath (no geode/mods folder)" }
$pkg = Get-ChildItem (Join-Path $NexRoot "build/out/$Target") -Filter '*.geode' | Select-Object -First 1
if (-not $pkg) { throw "Nothing built for $Target. Run tools/build.ps1 -Target $Target" }
Copy-Item $pkg.FullName $mods -Force
Write-Host "Deployed $($pkg.Name) ($Target, GD $($NexTargets[$Target].Gd)) to $mods"

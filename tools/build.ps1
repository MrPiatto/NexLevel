# Builds one target for Windows with Geode's documented command (geode build --ninja: Clang + Ninja),
# then copies the .geode to NEXLEVEL/build/out/<target>/.
param(
    [Parameter(Mandatory)][ValidateSet('gd2207','gd2208','gd2209')][string]$Target
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/targets.ps1"
$t = $NexTargets[$Target]

foreach ($tool in 'geode','cmake','ninja','clang') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool not found on PATH. See docs/SETUP.md step 1." }
}
$env:GEODE_SDK = Get-NexSdkPath $t.GeodeTag
if (-not (Test-Path $env:GEODE_SDK)) { throw "SDK for $Target missing. Run tools/setup-sdks.ps1 -Only $Target" }
$env:CPM_SOURCE_CACHE = Join-Path $NexRoot 'deps/.cpm-cache'

$src = Join-Path $NexRoot $t.Dir
Push-Location $src
try {
    geode build --ninja
    if ($LASTEXITCODE) { throw "geode build failed for $Target" }
} finally { Pop-Location }

$out = Join-Path $NexRoot "build/out/$Target"
New-Item -ItemType Directory -Force $out | Out-Null
$pkg = Get-ChildItem (Join-Path $src 'build') -Recurse -Filter '*.geode' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $pkg) { throw "No .geode produced in $src/build" }
Copy-Item $pkg.FullName $out -Force
Write-Host "Built $Target -> $(Join-Path $out $pkg.Name)"

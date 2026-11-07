# Builds one target for Windows into NEXLEVEL/build/<target>-win and copies the .geode to build/out/<target>/.
param(
    [Parameter(Mandatory)][ValidateSet('gd2207','gd2208','gd2209')][string]$Target,
    [string]$Config = 'RelWithDebInfo'
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/targets.ps1"
$t = $NexTargets[$Target]

$env:GEODE_SDK = Get-NexSdkPath $t.GeodeTag
if (-not (Test-Path $env:GEODE_SDK)) { throw "SDK for $Target missing. Run tools/setup-sdks.ps1 -Only $Target" }
$env:CPM_SOURCE_CACHE = Join-Path $NexRoot 'deps/.cpm-cache'

$src = Join-Path $NexRoot $t.Dir
$bld = Join-Path $NexRoot "build/$Target-win"
cmake -S $src -B $bld -A x64 "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE) { throw "configure failed" }
cmake --build $bld --config $Config
if ($LASTEXITCODE) { throw "build failed" }

$out = Join-Path $NexRoot "build/out/$Target"
New-Item -ItemType Directory -Force $out | Out-Null
$pkg = Get-ChildItem $bld -Recurse -Filter '*.geode' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $pkg) { throw "No .geode produced in $bld" }
Copy-Item $pkg.FullName $out -Force
Write-Host "Built $Target -> $(Join-Path $out $pkg.Name)"

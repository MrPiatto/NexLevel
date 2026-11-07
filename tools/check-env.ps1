# NexLevel - Phase 1 environment check (read-only; writes only tools/env-report.txt inside NEXLEVEL)
$ErrorActionPreference = 'SilentlyContinue'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $PSScriptRoot 'env-report.txt'
$lines = @("NexLevel env report  $(Get-Date -Format s)", "Project root: $root", "")

function Check($name, $cmd, $verArgs) {
  $c = Get-Command $cmd -ErrorAction SilentlyContinue
  if ($c) {
    $v = (& $c.Source $verArgs 2>&1 | Select-Object -First 1)
    $script:lines += ("{0,-14} OK      {1}  [{2}]" -f $name, $v, $c.Source)
  } else { $script:lines += ("{0,-14} MISSING" -f $name) }
}

Check 'git'       'git'      '--version'
Check 'cmake'     'cmake'    '--version'
Check 'ninja'     'ninja'    '--version'
Check 'clang'     'clang'    '--version'
Check 'clang-cl'  'clang-cl' '--version'
Check 'python'    'python'   '--version'
Check 'geode-cli' 'geode'    '--version'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
  $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
  $lines += ("{0,-14} {1}" -f 'MSVC tools', $(if ($vs) { "OK      VS $vs" } else { 'MISSING (VC tools component)' }))
} else { $lines += ("{0,-14} MISSING (vswhere not found)" -f 'Visual Studio') }

$lines += ""
$lines += "GEODE_SDK        = $env:GEODE_SDK"
$lines += "ANDROID_NDK_ROOT = $env:ANDROID_NDK_ROOT"
$lines += "ANDROID_NDK_HOME = $env:ANDROID_NDK_HOME"

# Steam Geometry Dash install(s)
$steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath
$lines += ""
if ($steam) {
  $libs = @("$steam")
  $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
  if (Test-Path $vdf) { Select-String -Path $vdf -Pattern '"path"\s+"(.+)"' | ForEach-Object { $libs += ($_.Matches[0].Groups[1].Value -replace '\\\\','\') } }
  foreach ($l in ($libs | Select-Object -Unique)) {
    $gd = Join-Path $l 'steamapps\common\Geometry Dash'
    if (Test-Path "$gd\GeometryDash.exe") {
      $exe = Get-Item "$gd\GeometryDash.exe"
      $lines += "GD (Steam)       = $gd  (exe modified $($exe.LastWriteTime.ToString('s')))"
      if (Test-Path "$gd\Geode.dll") { $lines += "  Geode installed: yes (Geode.dll $((Get-Item "$gd\Geode.dll").VersionInfo.ProductVersion))" } else { $lines += "  Geode installed: no" }
    }
  }
} else { $lines += "Steam            = not found in registry" }

$lines | Tee-Object -FilePath $out
Write-Host "`nSaved to $out"

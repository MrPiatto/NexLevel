# Single source of truth for the build targets. Dot-source this file.
# Keep in sync with compat/*/mod.json ("geode" and "gd" keys).
$NexRoot = Split-Path -Parent $PSScriptRoot
$NexTargets = [ordered]@{
    'gd2208' = @{ Dir = 'compat/gd2208-geode5'; GeodeTag = 'v5.10.1';        Gd = '2.2081'; Major = 5 }
}
function Get-NexSdkPath($tag) { Join-Path $NexRoot "deps/geode-sdk/$tag" }

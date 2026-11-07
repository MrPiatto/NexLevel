# Single source of truth for the three build targets. Dot-source this file.
# Keep in sync with compat/*/mod.json ("geode" and "gd" keys).
$NexRoot = Split-Path -Parent $PSScriptRoot
$NexTargets = [ordered]@{
    'gd2207' = @{ Dir = 'compat/gd2207-geode4'; GeodeTag = 'v4.10.2';        Gd = '2.2074'; Major = 4 }
    'gd2208' = @{ Dir = 'compat/gd2208-geode5'; GeodeTag = 'v5.10.1';        Gd = '2.2081'; Major = 5 }
    'gd2209' = @{ Dir = 'compat/gd2209-geode6'; GeodeTag = 'v6.0.0-alpha.1'; Gd = '2.2091'; Major = 6 }
}
function Get-NexSdkPath($tag) { Join-Path $NexRoot "deps/geode-sdk/$tag" }

# Runs the headless test player (built with the core tests). Examples:
#   tools\bot.ps1 join 127.0.0.1        join the room you host in GD on this PC
#   tools\bot.ps1 host                  open a room that GD can join at 127.0.0.1
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$BotArgs)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bld = Join-Path $root 'build/core-tests-win'
cmake -S (Join-Path $root 'core') -B $bld | Out-Null
if ($LASTEXITCODE) { throw "configure failed" }
cmake --build $bld --config Debug --target nexlevel_bot
if ($LASTEXITCODE) { throw "bot build failed - see the errors above" }
& (Join-Path $bld 'Debug/nexlevel_bot.exe') @BotArgs

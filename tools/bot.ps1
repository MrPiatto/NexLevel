# Runs the headless test player (built with the core tests). Examples:
#   tools\bot.ps1 join <ROOM-CODE>      join the room you host in GD (copy the code from the Collab menu)
#   tools\bot.ps1 host                  open a room and print its code for GD to join
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$BotArgs)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bld = Join-Path $root 'build/core-tests-win'
cmake -S (Join-Path $root 'core') -B $bld | Out-Null
if ($LASTEXITCODE) { throw "configure failed" }
cmake --build $bld --config Debug --target nexlevel_bot
if ($LASTEXITCODE) { throw "bot build failed - see the errors above" }
& (Join-Path $bld 'Debug/nexlevel_bot.exe') @BotArgs

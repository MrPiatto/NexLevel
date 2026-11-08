# Runs the headless test player (built with the core tests). Examples:
#   tools\bot.ps1 join                 join with the room code copied via Copy in GD's Collab menu
#   tools\bot.ps1 join <ROOM-CODE>      join a specific room
#   tools\bot.ps1 host                  open a room and print its code for GD to join
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$BotArgs)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bld = Join-Path $root 'build/core-tests-win'
cmake -S (Join-Path $root 'core') -B $bld | Out-Null
if ($LASTEXITCODE) { throw "configure failed" }
cmake --build $bld --config Debug --target nexlevel_bot
if ($LASTEXITCODE) { throw "bot build failed - see the errors above" }
# "join" without a code: use the room code copied with the Copy button in GD.
if ($BotArgs.Count -eq 1 -and $BotArgs[0] -eq 'join') {
    $code = (Get-Clipboard -Raw).Trim()
    Write-Host "Using room code from clipboard: $code"
    $BotArgs = @('join', $code)
}
& (Join-Path $bld 'Debug/nexlevel_bot.exe') @BotArgs

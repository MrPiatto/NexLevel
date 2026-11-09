# NexLevel Discord server setup: creates roles, categories, channels and the info messages in an
# EMPTY server you own, through a temporary bot. Nothing is installed.
#
#   tools\discord-setup.ps1 -Token "BOT_TOKEN" -ServerId "123456789012345678"
#
# Steps (once):
#   1. Create your server in Discord (empty is fine).
#   2. https://discord.com/developers/applications -> New Application "NexLevel Setup" -> Bot -> Reset Token -> copy it.
#   3. OAuth2 -> URL Generator -> scopes: bot -> permissions: Administrator -> open the URL -> add it to your server.
#   4. Discord settings -> Advanced -> Developer Mode ON; right click your server -> Copy Server ID.
#   5. Run this script. Afterwards kick the bot and reset its token (it is not needed any more).
param(
    [Parameter(Mandatory)][string]$Token,
    [Parameter(Mandatory)][string]$ServerId
)
$ErrorActionPreference = 'Stop'
$api = 'https://discord.com/api/v10'
$headers = @{ Authorization = "Bot $Token"; 'User-Agent' = 'DiscordBot (https://github.com/MrPiatto/NexLevel, 1.0)' }

function Call($method, $path, $body) {
    Start-Sleep -Milliseconds 700   # stay well under Discord's rate limits
    $json = if ($body) { $body | ConvertTo-Json -Depth 6 -Compress } else { $null }
    $bytes = if ($json) { [System.Text.Encoding]::UTF8.GetBytes($json) } else { $null }
    Invoke-RestMethod -Method $method -Uri "$api$path" -Headers $headers -ContentType 'application/json; charset=utf-8' -Body $bytes
}

$VIEW = 1024; $SEND = 2048
$everyone = $ServerId   # the @everyone role has the server's id
$readOnly = @(@{ id = $everyone; type = 0; allow = "$VIEW"; deny = "$SEND" })

Write-Host 'Roles...'
$admin = Call POST "/guilds/$ServerId/roles" @{ name = 'Forg3 Studios'; color = 0x8C5AFF; hoist = $true; permissions = '8' }
$mod   = Call POST "/guilds/$ServerId/roles" @{ name = 'Moderator'; color = 0x14C8B4; hoist = $true; permissions = '1099780063232' }
$test  = Call POST "/guilds/$ServerId/roles" @{ name = 'Tester'; color = 0xFFC850; hoist = $true }
$memb  = Call POST "/guilds/$ServerId/roles" @{ name = 'Member'; color = 0x969CBE }

function Category($name) { Call POST "/guilds/$ServerId/channels" @{ name = $name; type = 4 } }
function Text($name, $parent, $topic, $ro) {
    $b = @{ name = $name; type = 0; parent_id = $parent.id; topic = $topic }
    if ($ro) { $b.permission_overwrites = $readOnly }
    Call POST "/guilds/$ServerId/channels" $b
}
function Post($channel, $text, $pin) {
    $m = Call POST "/channels/$($channel.id)/messages" @{ content = $text }
    if ($pin) { Call PUT "/channels/$($channel.id)/pins/$($m.id)" $null | Out-Null }
}

Write-Host 'Channels...'
$info = Category 'INFO'
$welcome  = Text 'welcome' $info 'Start here' $true
$rules    = Text 'rules' $info 'Server rules' $true
$news     = Text 'announcements' $info 'New NexLevel versions' $true
$download = Text 'download' $info 'Get NexLevel' $true
$comm = Category 'COMMUNITY'
Text 'general' $comm 'Talk about anything NexLevel' $false | Out-Null
$rooms = Text 'find-a-room' $comm 'Looking for people to build with? Post your room code here' $false
Text 'showcase' $comm 'Levels made with NexLevel' $false | Out-Null
Text 'italiano' $comm 'Chat in italiano' $false | Out-Null
$sup = Category 'SUPPORT'
$help = Text 'help' $sup 'Questions about installing or using NexLevel' $false
$bugs = Text 'bug-reports' $sup 'Bugs and crashes - read the pinned message first' $false
Text 'suggestions' $sup 'Ideas for NexLevel' $false | Out-Null

Write-Host 'Messages...'
Post $welcome @"
:wave: Welcome to **NexLevel** - build Geometry Dash levels together, in real time!

:inbox_tray: Download: https://github.com/MrPiatto/NexLevel/releases/latest
:question: Need help? Ask in <#$($help.id)>
:bug: Found a bug? Post in <#$($bugs.id)> or open an issue: https://github.com/MrPiatto/NexLevel/issues
:handshake: Looking for builders? Try <#$($rooms.id)>

Requires GD 2.2081 + Geode (Windows).
"@ $false
Post $rules @"
**Rules**
1. Be respectful. No harassment, hate or spam.
2. No NSFW content.
3. Don't share or re-upload other people's levels without permission.
4. Room codes only in <#$($rooms.id)>.
5. No advertising without permission.
6. Bugs go in <#$($bugs.id)> with your Geode log (geode/logs).
7. Staff decisions are final.
"@ $false
Post $download @"
:inbox_tray: **Download NexLevel**
https://github.com/MrPiatto/NexLevel/releases/latest

1. Install Geode for Geometry Dash 2.2081
2. Put ``forg3studios.nexlevel.geode`` in ``Geometry Dash/geode/mods``
3. Restart the game - NexLevel updates itself when a new version is out.
"@ $false
Post $bugs @"
:bug: **How to report a bug**
1. NexLevel version and GD version
2. What you did, what happened, what you expected
3. Your newest Geode log: Geometry Dash folder -> geode/logs
4. If GD crashed: the file in geode/crashlogs (if there is one)
"@ $true
Post $news ":tada: **NexLevel server is open!** Download the latest version in <#$($download.id)>." $false

Write-Host ''
Write-Host 'Done! Now: give yourself the "Forg3 Studios" role, kick the setup bot and reset its token.' -ForegroundColor Green
Write-Host 'Then: Server Settings -> Enable Community, and create a non-expiring invite link.' -ForegroundColor Green

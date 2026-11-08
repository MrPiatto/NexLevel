# Sends the relay source to your server and installs it there (one command).
#   tools\relay-upload.ps1 -Server 203.0.113.10 -Key C:\Users\me\Downloads\ssh-key.key
#   tools\relay-upload.ps1 -Server 203.0.113.10 -User root          (servers with password login)
# Uses the ssh/scp/tar that come with Windows 10/11.
param(
    [Parameter(Mandatory = $true)][string]$Server,
    [string]$User = 'ubuntu',
    [string]$Key
)
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$sshArgs = @('-o', 'StrictHostKeyChecking=accept-new')
if ($Key) { $sshArgs += @('-i', $Key) }

$pkg = Join-Path $root 'build/relay-upload.tar'
New-Item -ItemType Directory -Force -Path (Join-Path $root 'build') | Out-Null
Write-Host "Packing the relay source..."
tar -cf $pkg -C $root core/CMakeLists.txt core/include core/src core/tools deploy/relay
if ($LASTEXITCODE) { throw "packing failed" }

Write-Host "Uploading to $User@$Server ..."
scp @sshArgs $pkg "${User}@${Server}:nexlevel-relay.tar"
if ($LASTEXITCODE) { throw "upload failed - check the address, the user name and the key" }

Write-Host "Building and starting the relay on the server (takes a few minutes the first time)..."
ssh @sshArgs "${User}@${Server}" "rm -rf ~/nexlevel && mkdir -p ~/nexlevel && tar -xf ~/nexlevel-relay.tar -C ~/nexlevel && bash ~/nexlevel/deploy/relay/install.sh"
if ($LASTEXITCODE) { throw "install on the server failed - see the messages above" }
Write-Host "Done. Set the mod setting 'Relay server' to $Server" -ForegroundColor Green

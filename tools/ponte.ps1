# Starts the NexLevel relay ("ponte") on this PC, ready to be shared through playit.gg.
#   tools\ponte.ps1
# Keep this window open while people play. In playit.gg: TCP tunnel -> local port 47800.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$bld = Join-Path $root 'build/core-tests-win'
$exe = Join-Path $bld 'Release/nexlevel_relay.exe'
if (-not (Test-Path $exe)) {
    Write-Host "Building the relay (first time only)..."
    cmake -S (Join-Path $root 'core') -B $bld | Out-Null
    cmake --build $bld --config Release --target nexlevel_relay
    if ($LASTEXITCODE) { throw "relay build failed - see the errors above" }
}
Write-Host ""
Write-Host "NexLevel ponte: ACCESO su porta 47800" -ForegroundColor Green
Write-Host "In playit.gg il tunnel TCP deve puntare alla porta locale 47800."
Write-Host "Nella mod (Settings > Relay server) metti l'indirizzo che ti da playit."
Write-Host "Lascia aperta questa finestra mentre giocate. Ctrl+C per spegnere."
Write-Host ""
& $exe --port 47800 --behind-tunnel

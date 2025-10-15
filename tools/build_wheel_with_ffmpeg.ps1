param(
    [string] $PackDirs = $env:PACK_FFMPEG_RUNTIME_DIRS
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Push-Location -LiteralPath (Split-Path -Path $PSScriptRoot -Parent)

Write-Host "Building wheel..."
python -m build --wheel

$wheel = Get-ChildItem -Path dist -Filter *.whl | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $wheel) {
    Write-Error "No wheel found in dist/"
    Exit 1
}

Write-Host "Post-processing wheel: $($wheel.FullName)"

$script = Join-Path $PSScriptRoot 'package_ffmpeg_into_wheel.py'
if (-not (Test-Path $script)) {
    Write-Error "Post-processor script not found: $script"
    Exit 1
}

if ($PackDirs) {
    # Pass dirs as a single argument; Python script expects OS pathsep-separated list
    & $PSHOME\python.exe $script $wheel.FullName --dirs $PackDirs
} else {
    & $PSHOME\python.exe $script $wheel.FullName
}

Write-Host "Done. Wheel updated: $($wheel.FullName)"

Pop-Location

param(
    [string] $PackDirs = $env:PACK_FFMPEG_RUNTIME_DIRS,
    [string] $VcpkgRoot = $env:VCPKG_ROOT
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Push-Location -LiteralPath (Split-Path -Path $PSScriptRoot -Parent)

Write-Host "Ensuring that build dependencies are there..."
python -m pip install --upgrade build setuptools wheel

Write-Host "Building wheel..."

if ($PackDirs) {
  $env:PACK_FFMPEG_RUNTIME_DIRS = $PackDirs
}

# Base cmake args
$cmakeArgs = "-DBUILD_WHEEL=On"

# If VCPKG_ROOT is set, try to apply vcpkg toolchain and optional triplet
if ($env:VCPKG_ROOT) {
  $vcpkgToolchain = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
  if (Test-Path $vcpkgToolchain) {
    # Quote the path in case it contains spaces
    $cmakeArgs += " -DCMAKE_TOOLCHAIN_FILE=`"$vcpkgToolchain`""
    Write-Host "Applying vcpkg toolchain: $vcpkgToolchain"
  } else {
    Write-Warning "VCPKG_ROOT is set but vcpkg toolchain not found at: $vcpkgToolchain"
  }

  if ($env:VCPKG_TARGET_TRIPLET) {
    $cmakeArgs += " -DVCPKG_TARGET_TRIPLET=$($env:VCPKG_TARGET_TRIPLET)"
    Write-Host "Using VCPKG_TARGET_TRIPLET=$($env:VCPKG_TARGET_TRIPLET)"
  }
}
else {
  Write-Warning "VCPKG_ROOT not set; skipping vcpkg toolchain"
}

$env:CMAKE_ARGS = $cmakeArgs
Write-Host "CMAKE_ARGS=$($env:CMAKE_ARGS)"

# Run build without --config-setting to avoid frontend parsing issues
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

param(
  [string] $PackDirs = $env:PACK_FFMPEG_RUNTIME_DIRS,
  [string] $VcpkgRoot = $env:VCPKG_ROOT,
  [int] $NumProcs = $env:NUMBER_OF_PROCESSORS
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Push-Location -LiteralPath (Split-Path -Path $PSScriptRoot -Parent)

Write-Host "Ensuring that build dependencies are there..."
python -m pip install --upgrade build setuptools wheel

Write-Host "Checking for Python headers..."
$pythonIncludeDir = & python -c "from sysconfig import get_paths; print(get_paths()['include'])"
if (-not (Test-Path $pythonIncludeDir)) {
    Write-Error "Python include directory not found: $pythonIncludeDir"
    Exit 1
}

Write-Host "Building wheel..."

# function to quote string
function quoted($str) {
  return '"' + $str + '"'
}

if ($PackDirs) {
  $env:PACK_FFMPEG_RUNTIME_DIRS = $PackDirs
}

# If VCPKG_ROOT is set, try to apply vcpkg toolchain and optional triplet
$vcpkgToolchain = $null
$vcpkgPath = Get-Command vcpkg -ErrorAction SilentlyContinue
if ($env:VCPKG_ROOT) {
  $vcpkgToolchain = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
} elseif ($vcpkgPath) {
  $vcpkgRootFromExe = (Split-Path -Path $vcpkgPath.Path -Parent)
  $vcpkgToolchain = Join-Path $vcpkgRootFromExe "scripts\buildsystems\vcpkg.cmake"
} else {
  Write-Host "VCPKG needed to build wheel. Exiting."
  Exit 1
}

# install ffmpgeg[avcodec, avformat, avutil]
Write-Host "Installing vcpkg packages..."
& vcpkg install ffmpeg[avcodec,avformat]:x64-windows

# make an argument for nproc
$buildProcs = $null
if ($NumProcs) {
  $buildProcs = [int]$NumProcs
}

# Run the build and rely on CMAKE_ARGS environment variable. Some versions of the
# PEP517 frontend accepted a --config-setting flag, but it's frontend-version
# dependent and caused 'Unrecognized options in config-settings: -- -DBUILD_WHEEL'.
# Using CMAKE_ARGS is the most compatible fallback used by scikit-build-core.
Write-Host "Running: python -m build --wheel (using CMAKE_ARGS env var)"
$env:CMAKE_BUILD_PARALLEL_LEVEL = $buildProcs
& python -m build --wheel `
  --config-setting=cmake.args="-DCMAKE_TOOLCHAIN_FILE=$(quoted($vcpkgToolchain))" `
  --config-setting=cmake.args="-DBUILD_WHEEL=On" `
  --config-setting=cmake.args="-DCMAKE_BUILD_PARALLEL_LEVEL=$(quoted($buildProcs))"
if ($LASTEXITCODE -ne 0) {
  throw "Wheel build failed with exit code $LASTEXITCODE"
}

$wheel = Get-ChildItem -Path dist -Filter *.whl | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $wheel) {
    Write-Error "No wheel found in dist/"
    Exit 1
}

Write-Host "Post-processing wheel: $($wheel.FullName)"

$script = Join-Path $PSScriptRoot 'dependency_inject.py'
if (-not (Test-Path $script)) {
    Write-Error "Post-processor script not found: $script"
    Exit 1
}

if ($PackDirs) {
    # Pass dirs as a single argument; Python script expects OS pathsep-separated list
    & python.exe $script $wheel.FullName --dirs $PackDirs
} else {
    & python.exe $script $wheel.FullName
}

Write-Host "Done. Wheel updated: $($wheel.FullName)"

Pop-Location

<#
.SYNOPSIS
    PowerShell script to configure and build Synavis project (Windows equivalent of cmake_install.sh)
.DESCRIPTION
    Supports options for build directory, build type, deleting build, activating decoding, parallel jobs, verbosity, cplantbox location, and using clang.
#>

param(
    [string]$BuildDir = "build",
    [string]$BuildType = "Release",
    [switch]$DeleteBuild,
    [int]$Jobs = ([Environment]::ProcessorCount - 1),
    [switch]$ActivateDecoding = $false,
    [string]$BaseDir = (Get-Location | Select-Object -ExpandProperty Path),
    [switch]$Verbose,
    [string]$CPlantBoxDir = "",
    [string]$VcpkgToolchain = "",
    [switch]$NoBuild = $false
)

# Ensure BaseDir is always an absolute path
if (-not [System.IO.Path]::IsPathRooted($BaseDir)) {
    $BaseDir = Join-Path (Get-Location | Select-Object -ExpandProperty Path) $BaseDir
}

function Show-Help {
    Write-Host "Usage: .\cmake_install.ps1 [-BuildDir <dir>] [-BuildType <type>] [-DeleteBuild] [-Jobs <n>] [-ActivateDecoding] [-BaseDir <dir>] [-Verbose] [-CPlantBoxDir <dir>] [-UseClang] [-Help]"
    Write-Host "  -BuildDir         Specify the build directory name (default: build)"
    Write-Host "  -BuildType        Specify the build type (default: Release)"
    Write-Host "  -DeleteBuild      Delete the build directory before building"
    Write-Host "  -Jobs             Number of processes for building (default: CPU count - 1)"
    Write-Host "  -ActivateDecoding Activate decoding (default: true)"
    Write-Host "  -BaseDir          Specify the base directory (default: current directory)"
    Write-Host "  -Verbose          Enable verbose logging"
    Write-Host "  -CPlantBoxDir     Specify location of CPlantBox (default: not set)"
    Write-Host "  -VcpkgToolchain   Specify path to vcpkg toolchain file (default: not set)"
    Write-Host "  -Help             Show this help message"
    exit 0
}

if ($args -contains '-Help' -or $args -contains '--help') {
    Show-Help
}

# Delete build directory if requested
if ($DeleteBuild) {
    Write-Host "Deleting build directory: $BuildDir"
    if (Test-Path "$BaseDir\$BuildDir") {
        Remove-Item "$BaseDir\$BuildDir" -Recurse -Force
    }
}

# Create build directory
if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildPath = $BuildDir
} else {
    $BuildPath = Join-Path $BaseDir $BuildDir
}
if (!(Test-Path $BuildPath)) {
    New-Item -ItemType Directory -Path $BuildPath | Out-Null
}

# CMake generator
$Generator = "Visual Studio 17 2022"

# libdatachannel options
$LibDataChannelBuildTests = "-DLIBDATACHANNEL_BUILD_TESTS=Off"
$LibDataChannelBuildExamples = "-DLIBDATACHANNEL_BUILD_EXAMPLES=Off"
$LibDataChannelSettings = "-DENABLE_DEBUG_LOGGING=On -DENABLE_LOCALHOST_ADDRESS=On -DENABLE_LOCAL_ADDRESS_TRANSLATION=On"

# Python include dir and library using libdir.py
$PythonIncludeDir = & python -c "from distutils.sysconfig import get_python_inc; print(get_python_inc())"
$PythonLibrary = & python libdir.py
if ($null -eq $PythonLibrary -or $PythonLibrary -eq "" -or $PythonLibrary -eq "None") {
        Write-Error "Could not determine Python library path using libdir.py."
        exit 1
}
Write-Host "Python include dir: $PythonIncludeDir"
Write-Host "Python library: $PythonLibrary"

# Vcpkg toolchain option
$VcpkgToolchainOption = ""
if ($VcpkgToolchain -ne "") {
    Write-Host "Using vcpkg toolchain file: $VcpkgToolchain"
    $VcpkgToolchainOption = "-DCMAKE_TOOLCHAIN_FILE=$VcpkgToolchain"
}

# Decoding option
$Decoding = ""
if ($ActivateDecoding) {
    Write-Host "Activating decoding"
    $Decoding = "-DBUILD_WITH_DECODING=On"
}

# Verbosity
$CMakeVerboseLogging = ""
if ($Verbose) {
    Write-Host "Enabling verbose logging"
    $CMakeVerboseLogging = "-DCMAKE_VERBOSE_MAKEFILE=On"
}

# Build with apps
$SynavisAppBuild = "-DBUILD_WITH_APPS=On"

# CPlantBox options
$CPlantBoxDirOption = ""
if ($CPlantBoxDir -ne "") {
    Write-Host "Using cplantbox location: $CPlantBoxDir"
    $CPlantBoxDirOption = "-DCPlantBox_DIR=$CPlantBoxDir"
} else {
    Write-Host "No cplantbox location specified, pulling CPlantBox from git"
    $CPlantBoxRepo = "$BaseDir\CPlantBox"
    if (!(Test-Path $CPlantBoxRepo)) {
        Write-Host "Cloning CPlantBox repository"
        git clone https://github.com/Plant-Root-Soil-Interactions-Modelling/CPlantBox.git $CPlantBoxRepo
    } else {
        Write-Host "Found existing CPlantBox directory, using it"
    }
    $CPlantBoxDirOption = "-DCPlantBox_DIR=$CPlantBoxRepo\build"
}

$PythonLibString = if ($PythonLibrary -ne "") { "-DPYTHON_LIBRARY=$PythonLibrary" } else { "" }

$CMakeCmd = @(
    "cmake",
    "-S $BaseDir",
    "-B $BuildPath",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-G `"$Generator`"",
    $LibDataChannelBuildTests,
    $LibDataChannelBuildExamples,
    $LibDataChannelSettings,
    $Decoding,
    "-DPYTHON_INCLUDE_DIR=$PythonIncludeDir",
    $PythonLibString,
    $SynavisAppBuild,
    $CMakeVerboseLogging,
    $CPlantBoxDirOption,
    $VcpkgToolchainOption,
    $CMakeOpt
) -join " "

Write-Host "Running: $CMakeCmd"
Invoke-Expression $CMakeCmd

if (-not $NoBuild) {
  # Build
  $BuildCmd = "cmake --build $BuildPath -- /m:$Jobs"
  Write-Host "Running: $BuildCmd"
  Invoke-Expression $BuildCmd
  Write-Host "Build completed successfully."
} else {
  Write-Host "Skipping build step due to -NoBuild switch."
}


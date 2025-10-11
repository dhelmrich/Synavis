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
    [ValidateSet('static','dynamic')][string]$Triplet = 'dynamic',
    [switch]$ExportFFmpeg = $false,
    [switch]$NoBuild = $false,
    [switch]$Help
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
    Write-Host "  -ExportFFmpeg    Export ffmpeg/libav via vcpkg (default: false)"
    Write-Host "  -NoBuild         Configure only, do not build (default: false)"
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

# Export ffmpeg/libav via vcpkg if requested
if ($ExportFFmpeg) {
    if ($VcpkgToolchain -eq "") {
        Write-Error "Vcpkg toolchain file must be specified with -VcpkgToolchain to export ffmpeg/libav."
        exit 1
    }
    $vcpkgCmd = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($null -eq $vcpkgCmd) {
        Write-Error "vcpkg command not found. Please ensure vcpkg is installed and available in PATH."
        exit 1
    }

    Write-Host "Installing ffmpeg via vcpkg..."
    & vcpkg install ffmpeg[avcodec,avdevice,avfilter,avformat,core]

    # Determine vcpkg root and triplet similarly to libvpx handling
    $VcpkgExePath = $vcpkgCmd.Source
    $VcpkgRoot = Split-Path $VcpkgExePath -Parent
    Write-Host "Detected vcpkg root: $VcpkgRoot"

    $preferredTriplet = if ($Triplet -eq 'static') { 'x64-windows-static' } else { 'x64-windows' }

    $pkgLine = & vcpkg list | Select-String -Pattern '^ffmpeg:' | Select-Object -First 1
    if ($null -eq $pkgLine) {
        Write-Error "ffmpeg not found in vcpkg list after install. Ensure vcpkg installed the package for the desired triplet ($preferredTriplet)."
        exit 1
    }
    $detectedTriplet = ($pkgLine -split ':')[1] -split '\s+' | Select-Object -First 1
    Write-Host "Detected triplet: $detectedTriplet (preferred: $preferredTriplet)"

    if ($preferredTriplet -and (Test-Path (Join-Path $VcpkgRoot "installed\$preferredTriplet"))) {
        $LibTriplet = $preferredTriplet
    } else {
        $LibTriplet = $detectedTriplet
    }
    Write-Host "Using ffmpeg triplet: $LibTriplet"

    $InstallDir = Join-Path $VcpkgRoot "installed\$LibTriplet"
    $LibDir = Join-Path $InstallDir "lib"
    $BinDir = Join-Path $InstallDir "bin"
    $IncludeDir = Join-Path $InstallDir "include"

    # Destination layout: SynavisBackend/Source/libav/{lib,include}
    $SynavisBackendRoot = Join-Path $BaseDir "SynavisBackend"
    $DestLibDir = Join-Path $SynavisBackendRoot "Source\libav\lib"
    $DestIncludeDir = Join-Path $SynavisBackendRoot "Source\libav\include"
    if (!(Test-Path $DestLibDir)) { New-Item -ItemType Directory -Path $DestLibDir -Force | Out-Null }
    if (!(Test-Path $DestIncludeDir)) { New-Item -ItemType Directory -Path $DestIncludeDir -Force | Out-Null }

    Write-Host "Copying ffmpeg/libav libraries to $DestLibDir"
    # Only copy the minimal set of ffmpeg/libav libs required for VP9 encoding and runtime
    $wantedLibPrefixes = @('avcodec','avformat','avutil','swscale','swresample','aom','vpx')
    if (Test-Path $LibDir) {
        Get-ChildItem -Path $LibDir -Filter "*.lib" -File -ErrorAction SilentlyContinue | ForEach-Object {
            $base = $_.BaseName.ToLower()
            $shouldCopy = $false
            foreach ($p in $wantedLibPrefixes) { if ($base -like "$p*") { $shouldCopy = $true; break } }
            if ($shouldCopy) {
                Write-Host "Copying $($_.Name)"
                Copy-Item -Path $_.FullName -Destination $DestLibDir -Force -ErrorAction SilentlyContinue
            } else {
                Write-Host "Skipping $($_.Name)"
            }
        }
    }
    if (Test-Path $BinDir) {
        # DLLs
        Get-ChildItem -Path $BinDir -Filter "*.dll" -File -ErrorAction SilentlyContinue | ForEach-Object {
            $base = ($_.BaseName).ToLower()
            $shouldCopy = $false
            foreach ($p in $wantedLibPrefixes) { if ($base -like "$p*" -or $base -like "${p}-*") { $shouldCopy = $true; break } }
            if ($shouldCopy) {
                Write-Host "Copying $($_.Name)"
                Copy-Item -Path $_.FullName -Destination $DestLibDir -Force -ErrorAction SilentlyContinue
            } else {
                Write-Host "Skipping $($_.Name)"
            }
        }
        # PDBs for the selected DLLs
        Get-ChildItem -Path $BinDir -Filter "*.pdb" -File -ErrorAction SilentlyContinue | ForEach-Object {
            $base = ($_.BaseName).ToLower()
            $shouldCopy = $false
            foreach ($p in $wantedLibPrefixes) { if ($base -like "$p*" -or $base -like "${p}-*") { $shouldCopy = $true; break } }
            if ($shouldCopy) {
                Write-Host "Copying $($_.Name)"
                Copy-Item -Path $_.FullName -Destination $DestLibDir -Force -ErrorAction SilentlyContinue
            }
        }
    }

    # Copy ffmpeg/libav-related include directories: avcodec, avformat, avutil, swresample, swscale
    Write-Host "Collecting ffmpeg/libav-specific include directories from $IncludeDir"
    $topLevel = Get-ChildItem -Path $IncludeDir -Force -ErrorAction SilentlyContinue
    $includeMatches = $topLevel | Where-Object { $_.Name -like 'libav*' -or $_.Name -like 'avcodec*' -or $_.Name -like 'avformat*' -or $_.Name -like 'avutil*' -or $_.Name -like 'swresample*' -or $_.Name -like 'swscale*' -or $_.Name -like 'postproc*' -or $_.Name -like 'avfilter*' }
    if ($includeMatches -and $includeMatches.Count -gt 0) {
        foreach ($item in $includeMatches) {
            $dest = Join-Path $DestIncludeDir $item.Name
            Write-Host "Copying include item $($item.Name) to $dest"
            Copy-Item -Path $item.FullName -Destination $dest -Recurse -Force -ErrorAction SilentlyContinue
        }
    } else {
        # Fallback: copy headers containing 'av' or 'libav' in their name preserving folder structure
    $headerMatches = Get-ChildItem -Path $IncludeDir -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -like 'av*' -or $_.Name -like 'libav*' -or $_.Name -like 'avcodec*' -or $_.Name -like 'avformat*' -or $_.Name -like 'avutil*' }
        if ($headerMatches -and $headerMatches.Count -gt 0) {
            foreach ($h in $headerMatches) {
                $rel = $h.FullName.Substring($IncludeDir.Length).TrimStart('\','/')
                $dest = Join-Path $DestIncludeDir $rel
                $destDir = Split-Path $dest -Parent
                if (!(Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir | Out-Null }
                Copy-Item -Path $h.FullName -Destination $dest -Force -ErrorAction SilentlyContinue
            }
        } else {
            Write-Warning "Could not find ffmpeg-specific include directories or headers; copying entire include tree as fallback."
            Copy-Item -Path (Join-Path $IncludeDir "*") -Destination $DestIncludeDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    Write-Host "ffmpeg/libav export completed."
}

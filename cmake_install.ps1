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
    [switch]$Help,
    [switch]$ExportVPX = $false,
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
    Write-Host "  -ExportVPX       Export libvpx via vcpkg (default: false)"
    Write-Host "  -NoBuild         Configure only, do not build (default: false)"
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

# Export libvpx via vcpkg if requested
if ($ExportVPX) {
    if ($VcpkgToolchain -eq "") {
        Write-Error "Vcpkg toolchain file must be specified with -VcpkgToolchain to export libvpx."
        exit 1
    }
    $vcpkgCmd = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($null -eq $vcpkgCmd) {
        Write-Error "vcpkg command not found. Please ensure vcpkg is installed and available in PATH."
        exit 1
    }
    # Procedure:
    # 1. Install libvpx via vcpkg
    # 2. Determine the installed triplet for libvpx
    # 3. Export libvpx (.lib|.dll|.pdb) to SynavisBackend/lib
    # 4. Export only libvpx-specific include files to SynavisBackend/include
    Write-Host "Installing libvpx via vcpkg..."
    & vcpkg install libvpx

    # Determine vcpkg root from the vcpkg executable location
    $VcpkgExePath = $vcpkgCmd.Source
    $VcpkgRoot = Split-Path $VcpkgExePath -Parent
    Write-Host "Detected vcpkg root: $VcpkgRoot"

    # Determine which vcpkg triplet to use for libvpx based on $Triplet ('dynamic' or 'static')
    # Map to common vcpkg triplet names used in this project
    $preferredTriplet = if ($Triplet -eq 'static') { 'x64-windows-static' } else { 'x64-windows' }

    # Find the installed triplet for libvpx (e.g. x64-windows)
    $pkgLine = & vcpkg list | Select-String -Pattern '^libvpx:' | Select-Object -First 1
    if ($null -eq $pkgLine) {
        Write-Error "libvpx not found in vcpkg list after install. Ensure vcpkg installed the package for the desired triplet ($preferredTriplet)."
        exit 1
    }
    $detectedTriplet = ($pkgLine -split ':')[1] -split '\s+' | Select-Object -First 1
    Write-Host "Detected triplet: $detectedTriplet (preferred: $preferredTriplet)"

    # If the user specified a preference and it's available, use it. Otherwise use detected.
    if ($preferredTriplet -and (Test-Path (Join-Path $VcpkgRoot "installed\$preferredTriplet"))) {
        $LibVpxTriplet = $preferredTriplet
    } else {
        $LibVpxTriplet = $detectedTriplet
    }
    Write-Host "Using libvpx triplet: $LibVpxTriplet"

    $LibVpxInstallDir = Join-Path $VcpkgRoot "installed\$LibVpxTriplet"
    $LibVpxLibDir = Join-Path $LibVpxInstallDir "lib"
    $LibVpxBinDir = Join-Path $LibVpxInstallDir "bin"
    $LibVpxIncludeDir = Join-Path $LibVpxInstallDir "include"
    # Mirror the CMakeLists layout: SynavisBackend/Source/libvpx/{lib,include}
    $SynavisBackendRoot = Join-Path $BaseDir "SynavisBackend"
    $DestLibDir = Join-Path $SynavisBackendRoot "Source\libvpx\lib"
    $DestIncludeDir = Join-Path $SynavisBackendRoot "Source\libvpx\include"
    if (!(Test-Path $DestLibDir)) {
        New-Item -ItemType Directory -Path $DestLibDir -Force | Out-Null
    }
    if (!(Test-Path $DestIncludeDir)) {
        New-Item -ItemType Directory -Path $DestIncludeDir -Force | Out-Null
    }
    Write-Host "Copying libvpx libraries to $DestLibDir"
    # Copy any .lib files in lib folder
    if (Test-Path $LibVpxLibDir) {
        Get-ChildItem -Path $LibVpxLibDir -Filter "*.lib" -File -ErrorAction SilentlyContinue | ForEach-Object {
            Write-Host "Copying $($_.Name)"
            Copy-Item -Path $_.FullName -Destination $DestLibDir -Force -ErrorAction SilentlyContinue
        }
    }
    # Copy any DLLs and PDBs from bin folder
    if (Test-Path $LibVpxBinDir) {
        Get-ChildItem -Path $LibVpxBinDir -Filter "*.dll" -File -ErrorAction SilentlyContinue | ForEach-Object {
            Write-Host "Copying $($_.Name)"
            Copy-Item -Path $_.FullName -Destination $DestLibDir -Force -ErrorAction SilentlyContinue
        }
        Get-ChildItem -Path $LibVpxBinDir -Filter "*.pdb" -File -ErrorAction SilentlyContinue | ForEach-Object {
            Write-Host "Copying $($_.Name)"
            Copy-Item -Path $_.FullName -Destination $DestLibDir -Force -ErrorAction SilentlyContinue
        }
    }

    # Copy only libvpx-specific include directories (fallback to headers containing 'vpx' or entire include tree)
    Write-Host "Collecting libvpx-specific include directories from $LibVpxIncludeDir"
    $topLevel = Get-ChildItem -Path $LibVpxIncludeDir -Force -ErrorAction SilentlyContinue
    $matches = $topLevel | Where-Object { $_.Name -match '^(vpx|vp8|vp9|libvpx)' }
    if ($matches -and $matches.Count -gt 0) {
        foreach ($item in $matches) {
            $dest = Join-Path $DestIncludeDir $item.Name
            Write-Host "Copying include item $($item.Name) to $dest"
            Copy-Item -Path $item.FullName -Destination $dest -Recurse -Force -ErrorAction SilentlyContinue
        }
    } else {
        # As a fallback, copy any header files with 'vpx' in their name preserving folder structure
        $headerMatches = Get-ChildItem -Path $LibVpxIncludeDir -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -match 'vpx' }
        if ($headerMatches -and $headerMatches.Count -gt 0) {
            foreach ($h in $headerMatches) {
                $rel = $h.FullName.Substring($LibVpxIncludeDir.Length).TrimStart('\','/')
                $dest = Join-Path $DestIncludeDir $rel
                $destDir = Split-Path $dest -Parent
                if (!(Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir | Out-Null }
                Copy-Item -Path $h.FullName -Destination $dest -Force -ErrorAction SilentlyContinue
            }
        } else {
            Write-Warning "Could not find libvpx-specific include directories or headers; copying entire include tree as fallback."
            Copy-Item -Path (Join-Path $LibVpxIncludeDir "*") -Destination $DestIncludeDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    Write-Host "libvpx export completed."
}


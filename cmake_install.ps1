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
    [string]$MSVCVersion = "",
    [ValidateSet('static','dynamic')][string]$Triplet = 'dynamic',
    [switch]$ExportLibraries = $false,
    [switch]$NoBuild = $false,
    [switch]$PythonOnly = $false,
    [switch]$InstallAdios2 = $false,
    [string]$Adios2Root = "",
    [switch]$Help
)

# stop on all errors
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Ensure BaseDir is always an absolute path
if (-not [System.IO.Path]::IsPathRooted($BaseDir)) {
    $BaseDir = Join-Path (Get-Location | Select-Object -ExpandProperty Path) $BaseDir
}

$LibDirPy = Join-Path $BaseDir "libdir.py"

function Get-PythonCommand {
    $candidateNames = @("python", "py")
    foreach ($candidateName in $candidateNames) {
        $candidate = Get-Command $candidateName -ErrorAction SilentlyContinue
        if ($null -ne $candidate) {
            return $candidate.Source
        }
    }

    throw "Could not find a Python interpreter on PATH. Install Python or add it to PATH, then rerun cmake_install.ps1."
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
    Write-Host "  -ExportLibraries Export libraries (ffmpeg and libdatachannel) via vcpkg/build tree (default: false)"
    Write-Host "  -NoBuild         Configure only, do not build (default: false)"
    Write-Host "  -Help             Show this help message"
    exit 0
}

if ($Help -or $args -contains '-Help' -or $args -contains '--help') {
    Show-Help
}

# Delete build directory if requested
if ($DeleteBuild) {
    Write-Host "Deleting build directory: $BuildDir"
    Remove-Item "$BuildDir" -Recurse -Force
    if (Test-Path "$BuildDir") {
        Write-Error "Failed to delete build directory: $BuildDir"
        exit 1
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

# Python include dir and library using the selected interpreter and repo-local libdir.py
$PythonCommand = Get-PythonCommand
Write-Host "Using Python command: $PythonCommand"

$PythonExecutable = & $PythonCommand -c "import sys; print(sys.executable)"
if ($LASTEXITCODE -ne 0 -or $null -eq $PythonExecutable -or $PythonExecutable -eq "" -or $PythonExecutable -eq "None") {
    Write-Error "Could not determine Python executable using '$PythonCommand'."
    exit 1
}

$PythonExecutable = $PythonExecutable.Trim()

$PythonIncludeDir = & $PythonCommand -c "import sysconfig; print(sysconfig.get_path('include'))"
if ($LASTEXITCODE -ne 0 -or $null -eq $PythonIncludeDir -or $PythonIncludeDir -eq "" -or $PythonIncludeDir -eq "None") {
    Write-Error "Could not determine Python include directory using '$PythonCommand'."
    exit 1
}

$PythonLibrary = & $PythonCommand $LibDirPy
if ($LASTEXITCODE -ne 0 -or $null -eq $PythonLibrary -or $PythonLibrary -eq "" -or $PythonLibrary -eq "None") {
    Write-Error "Could not determine Python library path using libdir.py at $LibDirPy."
    exit 1
}

$PythonIncludeDir = $PythonIncludeDir.Trim()
$PythonLibrary = $PythonLibrary.Trim()

Write-Host "Python include dir: $PythonIncludeDir"
Write-Host "Python library: $PythonLibrary"
Write-Host "Python executable: $PythonExecutable"

# Pass explicit Python executable to CMake to prefer the selected interpreter
$PythonExeOption = "-DPython3_EXECUTABLE=`"$PythonExecutable`" -DPython_EXECUTABLE=`"$PythonExecutable`""
$PythonCacheOptions = "-DPYTHON_INCLUDE_DIR=$PythonIncludeDir -DPYTHON_LIBRARY=$PythonLibrary"

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
if ($PythonOnly) {
    Write-Host "Python-only build requested: disabling app builds"
    $SynavisAppBuild = "-DBUILD_WITH_APPS=Off"
}

# CPlantBox options
$CPlantBoxDirOption = ""
if ($CPlantBoxDir -ne "") {
    # Check for a CMake package config in the provided directory
    $cfg1 = Join-Path $CPlantBoxDir "CPlantBoxConfig.cmake"
    $cfg2 = Join-Path $CPlantBoxDir "lib\cmake\CPlantBox\CPlantBoxConfig.cmake"
    if ((Test-Path $cfg1) -or (Test-Path $cfg2)) {
        Write-Host "Using installed CPlantBox location: $CPlantBoxDir"
        $CPlantBoxDirOption = "-DCPlantBox_DIR=$CPlantBoxDir"
    } else {
        Write-Warning "The provided CPlantBoxDir '$CPlantBoxDir' does not appear to be an installed CPlantBox (no CMake config found)."
        Write-Warning "Not passing CPlantBox_DIR to CMake to avoid configuring/building CPlantBox."
        Write-Host "If you intended to use a local CPlantBox source tree, please build/install it separately and then pass the install directory via -CPlantBoxDir."
    }
} else {
    Write-Host "No CPlantBox_DIR specified; will not configure or build CPlantBox."
}

# MSVC Version option
$MSVCVersionOption = ""
if ($MSVCVersion -ne "") {
    Write-Host "Using MSVC version: $MSVCVersion"
    $MSVCVersionOption = "-T $MSVCVersion"
    # also export set(CMAKE_GENERATOR_TOOLSET "v143") as env
    $env:CMAKE_GENERATOR_TOOLSET = $MSVCVersion
}

# Python cache options to ensure pybind11 uses the correct Python
# Use CACHE FORCE to ensure these values persist through vcpkg's toolchain
$PythonCacheOptions = "-DPYTHON_INCLUDE_DIR:PATH=$PythonIncludeDir -DPYTHON_LIBRARY:PATH=$PythonLibrary -DPython3_EXECUTABLE:FILEPATH=$PythonExecutable -DPython_EXECUTABLE:FILEPATH=$PythonExecutable -DPython3_INCLUDE_DIR:PATH=$PythonIncludeDir -DPython3_LIBRARY:PATH=$PythonLibrary"

$CMakeCmd = @(
    "cmake",
    "-S $BaseDir",
    "-B $BuildPath",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-G `"$Generator`"",
    $MSVCVersionOption,
    $LibDataChannelBuildTests,
    $LibDataChannelBuildExamples,
    $LibDataChannelSettings,
    $Decoding,
    $PythonCacheOptions,
    $SynavisAppBuild,
    $CMakeVerboseLogging,
    $CPlantBoxDirOption,
    $VcpkgToolchainOption
) -join " "

# ADIOS2 options
$Adios2RootOption = ""
if ($Adios2Root -ne "") {
    Write-Host "Using provided ADIOS2 root: $Adios2Root"
    $Adios2RootOption = "-DADIOS2_ROOT=$Adios2Root"
}

if ($InstallAdios2) {
    $SkipAdiosOption = "-DSYNAVIS_SKIP_INSTALL_ADIOS2=Off"
} else {
    $SkipAdiosOption = "-DSYNAVIS_SKIP_INSTALL_ADIOS2=On"
}

# Append ADIOS2 options to CMake command
$CMakeCmd = $CMakeCmd + " " + $Adios2RootOption + " " + $SkipAdiosOption

# Install ADIOS2 via vcpkg if requested (before CMake configure)
if ($InstallAdios2) {
    $vcpkgCmd = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($vcpkgCmd) {
        Write-Host "Installing adios2[mpi] via vcpkg..."
        # Explicitly install for x64-windows triplet to ensure consistency
        $result = & vcpkg install adios2[mpi]:x64-windows
        Write-Host $result
        
        # Verify installation succeeded
        $vcpkgRoot = Split-Path $vcpkgCmd.Source -Parent
        $installedDir = Join-Path $vcpkgRoot "installed\x64-windows"
        if (Test-Path $installedDir) {
            Write-Host "Verifying ADIOS2 installation in $installedDir"
            $adios2Dlls = Get-ChildItem -Path $installedDir -Recurse -Filter "adios2*.dll" -ErrorAction SilentlyContinue
            if ($adios2Dlls) {
                Write-Host "Found $(($adios2Dlls).Count) ADIOS2 DLL(s):"
                $adios2Dlls | ForEach-Object { Write-Host "  - $($_.Name) at $($_.FullName)" }
            } else {
                Write-Warning "ADIOS2 installation completed but no DLLs found. This may indicate a partial install or wrong triplet."
            }
        }
    } else {
        Write-Warning "vcpkg not found. Cannot install adios2 via vcpkg."
    }
}

Write-Host "Running: $CMakeCmd"
Invoke-Expression $CMakeCmd
$CMakeExitCode = $LASTEXITCODE
if ($CMakeExitCode -ne 0) {
    throw "CMake configuration failed with exit code $CMakeExitCode"
}

if (-not $NoBuild) {
  # Build
    if ($PythonOnly) {
                $BuildCmd = "cmake --build `"$BuildPath`" --config $BuildType --target PySynavis -- /m:$Jobs"
    } else {
                $BuildCmd = "cmake --build `"$BuildPath`" --config $BuildType -- /m:$Jobs"
    }
  Write-Host "Running: $BuildCmd"
    Invoke-Expression $BuildCmd
    $BuildExitCode = $LASTEXITCODE
    if ($BuildExitCode -ne 0) {
            throw "Build failed with exit code $BuildExitCode"
    }
    Write-Host "Build completed successfully."

    # Export ffmpeg/libav via vcpkg if requested
    if ($ExportLibraries) {
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
        # Ensure swscale and swresample features are requested so their
        # libraries (libswscale / libswresample) are available for export.
        & vcpkg install ffmpeg[avcodec,avdevice,avfilter,avformat,core,swresample,swscale]

        # Determine vcpkg root and triplet
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
        # Export ffmpeg to SynavisBackend.
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
        # Match common FFmpeg include directories. Match both 'swscale' and
        # 'libswscale' (vcpkg sometimes prefixes includes with 'lib'). Use
        # wildcard at both ends to be robust to naming variations.
        $includeMatches = $topLevel | Where-Object {
            $_.Name -like '*libav*' -or $_.Name -like '*avcodec*' -or $_.Name -like '*avformat*' -or $_.Name -like '*avutil*' -or
            $_.Name -like '*swresample*' -or $_.Name -like '*libswresample*' -or $_.Name -like '*swscale*' -or $_.Name -like '*libswscale*' -or
            $_.Name -like '*postproc*' -or $_.Name -like '*avfilter*'
        }
        if ($includeMatches -and $includeMatches.Count -gt 0) {
            foreach ($item in $includeMatches) {
                $dest = Join-Path $DestIncludeDir $item.Name
                Write-Host "Copying include item $($item.Name) to $dest"
                Copy-Item -Path $item.FullName -Destination $dest -Recurse -Force -ErrorAction SilentlyContinue
            }
        } else {
            # Fallback: copy headers containing 'av' or 'libav' in their name preserving folder structure
        $headerMatches = Get-ChildItem -Path $IncludeDir -Recurse -File -ErrorAction SilentlyContinue | Where-Object {
            $_.Name -like 'av*' -or $_.Name -like 'libav*' -or $_.Name -like 'avcodec*' -or $_.Name -like 'avformat*' -or $_.Name -like 'avutil*' -or
            $_.Name -like 'swscale*' -or $_.Name -like 'libswscale*' -or $_.Name -like 'swresample*' -or $_.Name -like 'libswresample*'
        }
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

    # Also copy libdatachannel (headers and built binaries) into SynavisBackend when requested
    if ($ExportLibraries) {
        Write-Host "Copying libdatachannel headers and built libraries into SynavisBackend..."
        $SynavisBackendRoot = Join-Path $BaseDir "SynavisBackend"
        $DestLibDir = Join-Path $SynavisBackendRoot "Source\libdatachannel\lib"
        $DestIncludeDir = Join-Path $SynavisBackendRoot "Source\libdatachannel\include"
        if (!(Test-Path $DestLibDir)) { New-Item -ItemType Directory -Path $DestLibDir -Force | Out-Null }
        if (!(Test-Path $DestIncludeDir)) { New-Item -ItemType Directory -Path $DestIncludeDir -Force | Out-Null }

        # Headers come from the libdatachannel source fetched by CMake
        $LibDataSrcInclude = Join-Path $BuildPath "_deps\libdatachannel-src\include"
        if (Test-Path $LibDataSrcInclude) {
            Write-Host "Copying libdatachannel headers from $LibDataSrcInclude to $DestIncludeDir"
            Copy-Item -Path (Join-Path $LibDataSrcInclude "*") -Destination $DestIncludeDir -Recurse -Force -ErrorAction SilentlyContinue
        } else {
            Write-Warning "libdatachannel source include not found at $LibDataSrcInclude; skipping header copy."
        }

        # Binaries are produced under the libdatachannel build directory; copy any relevant dll/lib/pdb
        $LibDataBuildRoot = Join-Path $BuildPath "_deps\libdatachannel-build"
        if (Test-Path $LibDataBuildRoot) {
            Write-Host "Searching for libdatachannel binaries under $LibDataBuildRoot"
            Get-ChildItem -Path $LibDataBuildRoot -Recurse -Include "*.dll","*.lib","*.pdb" -File -ErrorAction SilentlyContinue | ForEach-Object {
                $src = $_.FullName
                $destFile = Join-Path $DestLibDir $_.Name
                # If the source path includes an examples folder and the destination already has the file, skip copying
                if ($src -like "*\examples\*" -or $src -like "*/examples/*") {
                    if (Test-Path $destFile) {
                        Write-Host "Skipping example-derived file (destination exists): $($_.Name)"
                        return
                    }
                }
                Write-Host "Copying $src -> $DestLibDir"
                Copy-Item -Path $src -Destination $DestLibDir -Force -ErrorAction SilentlyContinue
            }
        } else {
            Write-Warning "libdatachannel build directory not found at $LibDataBuildRoot; skipping binary copy."
        }
        Write-Host "libdatachannel export (copy) completed."
    }

        # ADIOS2 export/copy into Adios2Backend (when requested)
        if ($InstallAdios2 -or $Adios2Root -ne "") {
            Write-Host "Preparing Adios2Backend ADIOS2 layout..."
            $Adios2BackendRoot = Join-Path $BaseDir "Adios2Backend"
            $DestAdiosInclude = Join-Path $Adios2BackendRoot "Source\adios2\include"
            $DestAdiosLib = Join-Path $Adios2BackendRoot "Source\adios2\lib"
            if (!(Test-Path $DestAdiosInclude)) { New-Item -ItemType Directory -Path $DestAdiosInclude -Force | Out-Null }
            if (!(Test-Path $DestAdiosLib)) { New-Item -ItemType Directory -Path $DestAdiosLib -Force | Out-Null }

            $CopiedAdios2Dlls = @{}

            function Copy-Adios2DllsFromRoot {
                param(
                    [string]$SearchRoot
                )

                if ([string]::IsNullOrWhiteSpace($SearchRoot) -or -not (Test-Path $SearchRoot)) {
                    return
                }

                Get-ChildItem -Path $SearchRoot -Recurse -File -Filter "adios2*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
                    $normalizedName = $_.Name.ToLowerInvariant()
                    if (-not $CopiedAdios2Dlls.ContainsKey($normalizedName)) {
                        $CopiedAdios2Dlls[$normalizedName] = $true
                        Write-Host "Copying ADIOS2 DLL $($_.FullName) -> $DestAdiosLib"
                        Copy-Item -Path $_.FullName -Destination $DestAdiosLib -Force -ErrorAction SilentlyContinue
                    }
                }
            }

            # Source ADIOS2 from provided root or vcpkg installation
            $AdiosSourceRoot = ""
            
            if ($Adios2Root -ne "" -and (Test-Path $Adios2Root)) {
                Write-Host "Using provided ADIOS2 root: $Adios2Root"
                $AdiosSourceRoot = $Adios2Root
            } elseif ($InstallAdios2) {
                $vcpkgCmd = Get-Command vcpkg -ErrorAction SilentlyContinue
                if ($vcpkgCmd) {
                    $VcpkgRoot = Split-Path $vcpkgCmd.Source -Parent
                    $vcpkgInstalled = Join-Path $VcpkgRoot "installed\x64-windows"
                    if (Test-Path $vcpkgInstalled) {
                        Write-Host "Using ADIOS2 from vcpkg: $vcpkgInstalled"
                        $AdiosSourceRoot = $vcpkgInstalled
                    } else {
                        Write-Warning "ADIOS2 not found in vcpkg installed tree at $vcpkgInstalled"
                    }
                } else {
                    Write-Warning "vcpkg not found. Cannot locate ADIOS2."
                }
            }
            
            # Copy from the determined source
            if ($AdiosSourceRoot -ne "") {
                # Copy headers
                $AdiosIncludeRoot = Join-Path $AdiosSourceRoot "include"
                if (Test-Path $AdiosIncludeRoot) {
                    Write-Host "Copying ADIOS2 headers from $AdiosIncludeRoot to $DestAdiosInclude"
                    Copy-Item -Path (Join-Path $AdiosIncludeRoot "*") -Destination $DestAdiosInclude -Recurse -Force -ErrorAction SilentlyContinue
                }

                # Copy DLLs from bin directory
                $AdiosBinDir = Join-Path $AdiosSourceRoot "bin"
                if (Test-Path $AdiosBinDir) {
                    Write-Host "Searching for ADIOS2 DLLs in $AdiosBinDir"
                    Copy-Adios2DllsFromRoot $AdiosBinDir
                }

                # Copy import libraries
                $AdiosLibRoot = Join-Path $AdiosSourceRoot "lib"
                if (Test-Path $AdiosLibRoot) {
                    Write-Host "Copying ADIOS2 import libraries from $AdiosLibRoot to $DestAdiosLib"
                    Get-ChildItem -Path $AdiosLibRoot -File -Filter "adios2*.lib" -ErrorAction SilentlyContinue | ForEach-Object {
                        Copy-Item -Path $_.FullName -Destination $DestAdiosLib -Force -ErrorAction SilentlyContinue
                    }
                }
            }

            # Verify all expected ADIOS2 DLLs are present
            $expectedDlls = @("adios2_c.dll", "adios2_core.dll", "adios2_cxx11.dll")
            $missingDlls = @()
            
            foreach ($dll in $expectedDlls) {
                if (-not (Test-Path (Join-Path $DestAdiosLib $dll))) {
                    $missingDlls += $dll
                }
            }
            
            if ($missingDlls.Count -gt 0) {
                Write-Warning "The following ADIOS2 DLLs are missing from ${DestAdiosLib}:"
                $missingDlls | ForEach-Object { Write-Warning "  - $_" }
                Write-Warning ""
                Write-Warning "This may indicate that the ADIOS2 build/configuration did not produce all expected shared libraries."
                Write-Warning "To resolve this issue:"
                Write-Warning "  1. Verify vcpkg installation: vcpkg list adios2:*"
                Write-Warning "  2. Reinstall ADIOS2 with explicit triplet: vcpkg install adios2[mpi]:x64-windows --recurse"
                Write-Warning "  3. Check vcpkg build logs in ${VcpkgRoot}\buildtrees\adios2"
                Write-Warning "  4. Alternatively, provide ADIOS2_ROOT pointing to a valid ADIOS2 installation"
            } else {
                Write-Host "All expected ADIOS2 DLLs found: $($expectedDlls -join ', ')"
            }

            Write-Host "ADIOS2 export/copy completed."
        }
    }

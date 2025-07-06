@echo off
REM filepath: f:\Work\Synavis\cmake_install.bat

REM Default values
set "BUILDDIR=build"
set "BUILDTYPE=Release"
set "DELBUILD=0"
set "ACTIVATE_DECODING=0"
set "VERBOSITY=0"
set "CPLANTBOX_DIR="
set "BASEDIR="
set "NPROC="
setlocal enabledelayedexpansion

REM Parse arguments
:parse_args
if "%~1"=="" goto after_args
if "%~1"=="-d" (
    set "BUILDDIR=%~2"
    shift
    shift
    goto parse_args
) else if "%~1"=="-t" (
    set "BUILDTYPE=%~2"
    shift
    shift
    goto parse_args
) else if "%~1"=="-e" (
    set "DELBUILD=1"
    shift
    goto parse_args
) else if "%~1"=="-j" (
    set "NPROC=%~2"
    if not defined NPROC (
        set "NPROC=1"
    )
    shift
    shift
    goto parse_args
) else if "%~1"=="-c" (
    set "ACTIVATE_DECODING=1"
    shift
    goto parse_args
) else if "%~1"=="-B" (
    set "BASEDIR=%~2"
    shift
    shift
    goto parse_args
) else if "%~1"=="-v" (
    set "VERBOSITY=1"
    shift
    goto parse_args
) else if "%~1"=="-p" (
    set "CPLANTBOX_DIR=%~2"
    shift
    shift
    goto parse_args
) else (
    echo Usage: %0 [-d builddir] [-t buildtype] [-e deletebuild] [-j nproc] [-c activate_decoding] [-B basedir] [-v verbosity] [-p cplantbox_location]
    echo.
    echo    -d builddir           Specify the build directory name (default: build)
    echo    -t buildtype          Specify the build type (default: Release)
    echo    -e deletebuild        Delete the build directory after building (default: false)
    echo    -j nproc              Specify the number of processes to use for building (default: all cores minus one)
    echo    -c activate_decoding  Activate decoding (default: false)
    echo    -B basedir            Specify the base directory (default: current directory)
    echo    -v verbosity          Enable verbose logging (default: false)
    echo    -p cplantbox_location Specify the location of cplantbox (default: not set)
    exit /b 1
)
:after_args

REM Set DIR to script location or BASEDIR
set "DIR=%~dp0"
if not "%BASEDIR%"=="" set "DIR=%BASEDIR%"

REM Convert backslashes to forward slashes for CMake
set "DIR_SLASHED=%DIR:\=/%"
set "BUILDDIR_SLASHED=%BUILDDIR:\=/%"

REM Remove trailing slash from DIR_SLASHED if present
if "%DIR_SLASHED:~-1%"=="/" set "DIR_SLASHED=%DIR_SLASHED:~0,-1%"

REM Delete build directory if requested
if "%DELBUILD%"=="1" (
    if exist "%DIR%\%BUILDDIR%" (
        echo Deleting build directory
        rmdir /s /q "%DIR%\%BUILDDIR%"
    )
)

REM Create build directory
if not exist "%DIR%\%BUILDDIR%" (
    mkdir "%DIR%\%BUILDDIR%"
)

REM On Windows, prefer Visual Studio, else fallback to NMake Makefiles if nmake is available
set "GENERATOR=Visual Studio 17 2022"
where nmake >nul 2>nul
if %ERRORLEVEL%==0 (
  REM Optionally, you could allow override here if needed
  REM set "GENERATOR=NMake Makefiles"
)

set "LIBDATACHANNEL_BUILD_TESTS=-DLIBDATACHANNEL_BUILD_TESTS=Off"
set "LIBDATACHANNEL_BUILD_EXAMPLES=-DLIBDATACHANNEL_BUILD_EXAMPLES=Off"
set "LIBDATACHANNEL_SETTINGS=-DENABLE_DEBUG_LOGGING=On -DENABLE_LOCALHOST_ADDRESS=On -DENABLE_LOCAL_ADDRESS_TRANSLATION=On"
set "SYNAVIS_APPBUILD=-DBUILD_WITH_APPS=Off"

REM Get Python include dir and library dir
for /f "delims=" %%i in ('python -c "from distutils.sysconfig import get_python_inc; print(get_python_inc())"') do set "PYTHON_INCLUDE_DIRS=%%i"
for /f "delims=" %%i in ('python -c "import sysconfig; print(sysconfig.get_config_var('LIBDIR'))"') do set "PYTHON_LIBDIR=%%i"
for /f "delims=" %%i in ('python -c "import sys; print('python{}.lib'.format(sys.version_info.major*10+sys.version_info.minor))"') do set "PYTHON_LIBNAME=%%i"
set "PYTHON_LIBRARY=%PYTHON_LIBDIR%\%PYTHON_LIBNAME%"
if "%PYTHON_LIBRARY%"=="" (
    echo Error: Could not determine Python LIBDIR. Please ensure Python is installed with a shared library or set PYTHON_LIBRARY manually.
    exit /b 1
)
echo Python include dir: %PYTHON_INCLUDE_DIRS%

REM Decoding option
set "DECODING="
if "%ACTIVATE_DECODING%"=="1" (
    echo Activating decoding
    set "DECODING=-DBUILD_WITH_DECODING=On"
)

REM Verbosity
set "CMAKE_VERBOSE_LOGGING="
if "%VERBOSITY%"=="1" (
    echo Enabling verbose logging
    set "CMAKE_VERBOSE_LOGGING=-DCMAKE_VERBOSE_MAKEFILE=On"
)

REM CPlantBox
set "CPLANTBOX_DIR_OPTION="
if not "%CPLANTBOX_DIR%"=="" (
    echo Using cplantbox location: %CPLANTBOX_DIR%
    set "CPLANTBOX_DIR_OPTION=-DCPlantBox_DIR=%CPLANTBOX_DIR%"
)

REM Configure
set "CMAKE_CONFIGURE_CMD=cmake -S %DIR_SLASHED% -B %DIR_SLASHED%/%BUILDDIR_SLASHED% -DCMAKE_BUILD_TYPE=%BUILDTYPE% -G "%GENERATOR%" %LIBDATACHANNEL_BUILD_TESTS% %LIBDATACHANNEL_BUILD_EXAMPLES% %LIBDATACHANNEL_SETTINGS% %DECODING% -DPYTHON_INCLUDE_DIR=%PYTHON_INCLUDE_DIRS% -DPYTHON_LIBRARY=%PYTHON_LIBRARY% %SYNAVIS_APPBUILD% %CMAKE_VERBOSE_LOGGING% %CPLANTBOX_DIR_OPTION%"
echo Running: %CMAKE_CONFIGURE_CMD%
%CMAKE_CONFIGURE_CMD%

REM Build
set "CMAKE_BUILD_CMD=cmake --build %DIR_SLASHED%/%BUILDDIR_SLASHED% --config %BUILDTYPE% -j %NPROC%"
if "%VERBOSITY%"=="1" (
  set "CMAKE_BUILD_CMD=%CMAKE_BUILD_CMD% --verbose"
)
echo Running: %CMAKE_BUILD_CMD%
%CMAKE_BUILD_CMD%



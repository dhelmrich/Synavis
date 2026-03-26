#---------------------------------------------------------------
# Project Synavis
# 
# Copyright (c) 2023 Dirk Baker
# All rights reserved.
# --------------------------------------------------------------
# License terms MIT License
# --------------------------------------------------------------
# Programmed for my PhD Thesis
# --------------------------------------------------------------

cmake_minimum_required(VERSION 3.12.0)

set(projectname Synavis)
string(TOUPPER ${projectname} PROJECTNAME_UPPER)
string(TOLOWER ${projectname} PROJECTNAME_LOWER)
# lib path?

project (${projectname} VERSION 0.0.1)

set(ROOTDIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_CXX_STANDARD 23)
set(BUILD_SHARED_LIBS ON)

# add an option to build with decoding support
option(BUILD_WITH_DECODING "Build with decoding support" OFF)
option(BUILD_WITH_APPS "Build with applications" OFF)
option(BUILD_WHEEL "Build python wheel only" OFF)

# option check: WIN32 + BUILD_WHEEL demands CMAKE_TOOLCHAIN_FILE
if(WIN32 AND BUILD_WHEEL)
  if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "On Windows, when BUILD_WHEEL is ON, CMAKE_TOOLCHAIN_FILE must be defined to point to vcpkg.cmake")
  endif()
endif()

if(BUILD_WHEEL)
  #set target to Release
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# an variable for the vcpkg.cmake path
set(VCPKG_CMAKE_PATH "" CACHE PATH "Path to the vcpkg.cmake file")
if(VCPKG_CMAKE_PATH)
  include(${VCPKG_CMAKE_PATH})
endif()

include(FetchContent)
include(ExternalProject)

if(UNIX)
  # search for libpcap with pkg-config
  find_package(PkgConfig)
  # set a flag if pkgconfig cannot be found
  if(NOT PkgConfig_FOUND)
    set(PKG_NOT_FOUND TRUE)
  endif()
endif()

set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries globally" FORCE)
set(LIBDATACHANNEL_BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries for libdatachannel" FORCE)
set(LIBDATACHANNEL_BUILD_STATIC_LIBS OFF CACHE BOOL "Disable static build for libdatachannel" FORCE)


# Fetch libdatachannel
FetchContent_Declare(
  libdatachannel
  GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
  GIT_SHALLOW True
  GIT_TAG v0.23.2
)
message(STATUS " Declaring FetchContent for libdatachannel (may fetch from internet)")
## Ensure both static and shared libraries are built for libdatachannel
# Set required cache variables BEFORE making the dependency available so
# the fetched project observes the desired build options.
set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries globally" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "Build static libraries globally" FORCE)
set(LIBDATACHANNEL_BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries for libdatachannel" FORCE)
set(LIBDATACHANNEL_BUILD_STATIC_LIBS ON CACHE BOOL "Build static libraries for libdatachannel" FORCE)
set(ENABLE_LOCALHOST_ADDRESS ON CACHE BOOL "Enable localhost address for libdatachannel" FORCE)
set(ENABLE_LOCAL_ADDRESS_TRANSLATION ON CACHE BOOL "Enable local address translation for libdatachannel" FORCE)

# Make the libdatachannel content available (replaces deprecated FetchContent_Populate)
FetchContent_MakeAvailable(libdatachannel)

# Ensure versioned shared library is produced if the target was added
if(TARGET datachannel)
  set_target_properties(datachannel PROPERTIES
      VERSION 0.23.2
      SOVERSION 0
  )
endif()




# Example for header-only library copying (e.g., nlohmann/json)
## Prefer an externally-provided nlohmann_json (vcpkg / system) to avoid
## target-name collisions. Only FetchContent if not available.
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND AND NOT TARGET nlohmann_json::nlohmann_json AND NOT TARGET nlohmann_json)
  FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
    GIT_SHALLOW True
  )
  message(STATUS " Declaring FetchContent for nlohmann/json (header-only)")
  FetchContent_GetProperties(json)
  if(NOT json_POPULATED)
    message(STATUS " Making nlohmann/json available (fetching source)...")
    FetchContent_MakeAvailable(json)
  endif()
else()
  message(STATUS "Using system/vcpkg-provided nlohmann_json (no FetchContent needed)")
endif()


if(UNIX)
  # try to find pcap
  if (NOT PKG_NOT_FOUND)
    pkg_check_modules(PCAP IMPORTED_TARGET libpcap)
    if (NOT PCAP_FOUND)
      message(STATUS "libpcap not found, you will have to set it manually for libdatachannel")
    endif()
  else()
    # only print a message here as libdatachannel only calls for library and include dirs
    message(WARNING "pkg-config not found, you will have to set libpcap manually for libdatachannel")
  endif()
else()
  # print warning and instruction to build pcap with vcpkg
  message(WARNING "Please install libpcap, as libdatachannel requires it")
  message(STATUS "...using vcpkg: vcpkg install pcap")
endif()

include(CheckIncludeFileCXX)
check_include_file_cxx("format" CXX_FORMAT_SUPPORT)
if (NOT CXX_FORMAT_SUPPORT)
  # set a c++ environment variable for this
  message(STATUS "Search for <format>: failed")
  if(UNIX AND NOT PKG_NOT_FOUND)
    pkg_check_modules(fmt REQUIRED IMPORTED_TARGET fmt)
    set(fmt_libraries PkgConfig::fmt)
  else()
    FetchContent_Declare(
      fmt
      GIT_REPOSITORY https://github.com/fmtlib/fmt.git
      GIT_TAG 8.1.0
      GIT_SHALLOW True
    )
    message(STATUS " Declaring FetchContent for fmt (format library)")
    FetchContent_GetProperties(fmt)
    if(NOT fmt_POPULATED)
      message(STATUS " Making fmt available (fetching/building as needed)")
      FetchContent_MakeAvailable(fmt)
    endif()
  endif()
else()
  message(STATUS "Search for <format>: succeeded")
  add_compile_definitions(__HAS_FORMAT)
endif()


if(UNIX)
  FetchContent_Declare(
    date
    GIT_REPOSITORY https://github.com/HowardHinnant/date.git
    GIT_TAG        v3.0.1
    GIT_SHALLOW True
  )
  message(STATUS " Declaring FetchContent for date (time library)")
  FetchContent_GetProperties(date)
  if(NOT date_POPULATED)
      message(STATUS " Making date available (fetching/building as needed)")
      FetchContent_MakeAvailable(date)
  endif()

endif()

if(BUILD_WITH_DECODING)
  # we build using FFMPEG
  # options for ffmpeg: cluster, local(unix), local(windows)
  # we will try to find first
  if(UNIX AND NOT PKG_NOT_FOUND)
    # try to find ffmpeg
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
      libavcodec
      libavformat
      libavutil
      libswresample
      libswscale
    )
    # provide FFMPEG_LIBRARIES variable through pkg-config
    set(FFMPEG_LIBRARIES PkgConfig::FFMPEG)
  else()
    # try to find ffmpeg
    find_package(FFMPEG REQUIRED)
    find_package(FFMPEG_LIBRARIES NAMES avcodec avformat avutil swresample swscale)
  endif()

  # if not found
  if(NOT FFMPEG_LIBRARIES)
    # if not found
    if(NOT FFMPEG_FOUND)
      # always prefer externally built versions
      message(STATUS "ffmpeg not found")
      # show install instructions
      # check if we are on the cluster by checking if this path contains "/p/"
      string(FIND "${CMAKE_CURRENT_SOURCE_DIR}" "/p/" _is_cluster)
      if(_is_cluster)
        # print information message about this
        message(WARNING "You are on the cluster")
        message(WARNING "Please load ffmpeg first: module load ffmpeg")
      endif()

      if (UNIX)
        message(ERROR "Please install libavcodec-dev")
        message(Status "sudo apt-get install libavcodec-dev")
      else()
        message(STATUS "Please install libavcodec")
        message(STATUS "...using vcpkg: vcpkg install ffmpeg[avcodec,avformat,core,swresample,swscale]")
      endif()
    endif()
  endif()
endif()


# find pybind11
find_package(pybind11 CONFIG)
# Inform about pybind11 discovery/fetch
message(STATUS " Searching for pybind11 (local or via FetchContent)")
# if not found
if(NOT pybind11_FOUND)
  FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v3.0.1
    GIT_SHALLOW True
  )
  message(STATUS " Declaring FetchContent for pybind11 (may fetch from internet)")
else()
  message(STATUS "Found pybind11 v${pybind11_VERSION}: ${pybind11_INCLUDE_DIRS}")
endif()

if(NOT pybind11_FOUND)
  message(STATUS " Making pybind11 available (fetching/building as needed)")
  FetchContent_MakeAvailable(pybind11)
endif()

if(NOT TARGET pybind11_json)
  message(STATUS " Searching for pybind11_json (local or via FetchContent)")
  find_package(pybind11_json CONFIG)
  message (STATUS " pybind11_json_FOUND: ${pybind11_json_FOUND}")
  # if not found (check the target rather than a variable to avoid duplicate targets)
  if(NOT TARGET pybind11_json)
    message(STATUS "pybind11_json not found")
    FetchContent_Declare(
        pybind11_json
        GIT_REPOSITORY https://github.com/pybind/pybind11_json.git
        GIT_TAG        0.2.15
        GIT_SHALLOW True
    )
    message(STATUS " Declaring FetchContent for pybind11_json (may fetch from internet)")
    FetchContent_MakeAvailable(pybind11_json)
    # pybind11_json is header-only; explicitly set include directory
    get_property(pybind11_json_include_dir TARGET pybind11_json PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT pybind11_json_include_dir)
      set_target_properties(pybind11_json PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/_deps/pybind11_json-src/include"
      )
    endif()
  else()
    message(STATUS "Found pybind11_json v${pybind11_json_VERSION}: ${pybind11_json_INCLUDE_DIRS}")
  endif()
endif()

message(STATUS " Adding main project subdirectory: synavis")
add_subdirectory(synavis)
message(STATUS " Adding externtest subdirectory")
add_subdirectory(externtest)
if(BUILD_WITH_APPS)
  message(STATUS " Adding native_modules subdirectory (apps build enabled)")
  add_subdirectory(native_modules)
endif()

# ADIOS2 support: attempt to detect system ADIOS2 or use provided ADIOS2_ROOT
option(SYNAVIS_ENABLE_ADIOS2_EXAMPLE "Build ADIOS2 example application" OFF)
find_package(ADIOS2 CONFIG QUIET)
if(ADIOS2_FOUND)
  message(STATUS "ADIOS2: System (found by find_package) - ADIOS2_AVAILABLE=System")
elseif(DEFINED ADIOS2_ROOT)
  message(STATUS "ADIOS2: Local (ADIOS2_ROOT provided) - ADIOS2_AVAILABLE=Local")
else()
  message(STATUS "ADIOS2: Disabled - pass -DADIOS2_ROOT=<path> or install system ADIOS2")
endif()

# Add ADIOS example if requested or if ADIOS2 is available
if(SYNAVIS_ENABLE_ADIOS2_EXAMPLE OR ADIOS2_FOUND OR DEFINED ADIOS2_ROOT)
  message(STATUS "Adding ADIOS2 example subdirectory")
  add_subdirectory(synavis/adios)
endif()






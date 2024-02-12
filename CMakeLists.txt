#---------------------------------------------------------------
# Project Synavis
# 
# Copyright (c) 2023 Dirk Helmrich
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

# add an option to build with decoding support
option(BUILD_WITH_DECODING "Build with decoding support" OFF)

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

# Fetch libdatachannel
FetchContent_Declare(
    libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_SHALLOW True
    GIT_TAG v0.19.5
)
FetchContent_GetProperties(libdatachannel)
if(NOT libdatachannel)
  FetchContent_Populate(libdatachannel)
  add_subdirectory(${libdatachannel_SOURCE_DIR} ${libdatachannel_BINARY_DIR} EXCLUDE_FROM_ALL)
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
    FetchContent_GetProperties(fmt)
    if(NOT fmt_POPULATED)
        FetchContent_Populate(fmt)
        add_subdirectory(${fmt_SOURCE_DIR} ${fmt_BINARY_DIR})
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
  FetchContent_GetProperties(date)
  if(NOT date_POPULATED)
      FetchContent_Populate(date)
      add_subdirectory(${date_SOURCE_DIR} ${date_BINARY_DIR})
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
# if not found
if(NOT pybind11_FOUND)
  FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.10.3
    GIT_SHALLOW True
  )
else()
  message(STATUS "Found pybind11 v${pybind11_VERSION}: ${pybind11_INCLUDE_DIRS}")
  # check in the include dir path if we are on the cluster by searching for "stages" in pybind11_INCLUDE_DIRS
  string(FIND "${pybind11_INCLUDE_DIRS}" "stages" _is_cluster)
  if(_is_cluster)
    # print information message about this
    message(WARNING "Found pybind11 on the cluster")
    message(WARNING "Make sure that you have loaded the correct stages. The default stage is not always the correct one.")
  else()
    message(STATUS "Found pybind11 on your local machine or user space")
  endif()
endif()

if(NOT pybind11_FOUND)
  FetchContent_GetProperties(pybind11)
  if(NOT pybind11_POPULATED)
      FetchContent_Populate(pybind11)
      add_subdirectory(${pybind11_SOURCE_DIR} ${pybind11_BINARY_DIR})
  endif()
endif()

if(NOT TARGET pybind11_json)
  find_package(pybind11_json CONFIG)
  #if not found
  if(NOT pybind11_json)
    message(STATUS "pybind11_json not found")
    FetchContent_Declare(
        pybind11_json
        GIT_REPOSITORY https://github.com/pybind/pybind11_json.git
        GIT_TAG        0.2.13
        GIT_SHALLOW True
    )
    FetchContent_GetProperties(pybind11_json)
    if(NOT pybind11_json_POPULATED)
        FetchContent_Populate(pybind11_json)
        #add_subdirectory(${pybind11_json_SOURCE_DIR} ${pybind11_json_BINARY_DIR})
        #add_subdirectory(${pybind11_json_SOURCE_DIR})
    endif()
    FetchContent_MakeAvailable(pybind11_json)
  else()
    message(STATUS "Found pybind11_json v${pybind11_json_VERSION}: ${pybind11_json_INCLUDE_DIRS}")
  endif()
endif()

add_subdirectory(synavis)
add_subdirectory(externtest)
add_subdirectory(native_modules)






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

# Fetch libdatachannel
FetchContent_Declare(
    libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_SHALLOW True
    GIT_TAG v0.16.1
)
FetchContent_GetProperties(libdatachannel)
if(NOT libdatachannel)
  FetchContent_Populate(libdatachannel)
  add_subdirectory(${libdatachannel_SOURCE_DIR} ${libdatachannel_BINARY_DIR} EXCLUDE_FROM_ALL)
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
  # VPX_INCLUDE_DIRS is either Cache or Environment variable
  # VPX_LIBRARIES is either Cache or Environment variable
  
  set(VPX_SEARCH_DIR "" CACHE PATH "Path to the libvpx installation")
  set(VPX_INCLUDE_DIRS "" CACHE PATH "Path to the libvpx include directory")
  set(VPX_LIBRARIES "" CACHE PATH "Path to the libvpx library")


  # search for vpx
  FIND_PATH(VPX_INCLUDE_DIRS NAMES vpx/vpx_decoder.h)
  FIND_LIBRARY(VPX_LIBRARIES NAMES vpx
  PATHS ${VPX_SEARCH_DIR}/lib/
    /usr/lib/
    /usr/local/lib/
    /opt/local/lib/
    /sw/lib/
    /usr/lib64/
    /usr/local/lib64/
    /opt/local/lib64/
    /sw/lib64/
    /usr/lib/x86_64-linux-gnu/
  )
  message(STATUS "VPX_INCLUDE_DIRS: ${VPX_INCLUDE_DIRS}")
  message(STATUS "VPX_LIBRARIES: ${VPX_LIBRARIES}")
  # check if VPX_INCLUDE_DIRS and VPX_LIBRARIES are set
  if(VPX_INCLUDE_DIRS AND VPX_LIBRARIES)
    set(VPX_FOUND True)
  endif()

  if(NOT VPX_FOUND)
    # try the unofficial package
    find_package(unofficial-libvpx CONFIG QUIET)
    # use config
    if(NOT unofficial-libvpx_FOUND)
      message(FATAL_ERROR "libvpx not found. Please install libvpx or use the unofficial package.")
      return()
    else()
      set(VPX_INCLUDE_DIRS ${unofficial-libvpx_DIR} / .. / include)
      set(VPX_LIBRARIES unofficial::libvpx::libvpx)
    endif()
  else()
    message(STATUS "Found libvpx v${VPX_VERSION}: ${VPX_INCLUDE_DIRS}")
    # check in the include dir path if we are on the cluster by searching for "stages" in VPX_INCLUDE_DIRS
    string(FIND "${VPX_INCLUDE_DIRS}" "stages" _is_cluster)
    if(_is_cluster)
      # print information message about this
      message(WARNING "Found libvpx on the cluster")
      message(WARNING "Make sure that you have loaded the correct stages. The default stage is not always the correct one.")
    else()
      message(STATUS "Found libvpx on your local machine or user space")
    endif()
  endif()
else()
  # remove "FrameDecode.hpp"+"FrameDecode.cpp" from the list of sources
  list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/FrameDecode.cpp")
  list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/FrameDecode.hpp")
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

find_package(pybind11_json CONFIG)
#if not found
if(NOT pybind11_json)
  FetchContent_Declare(
      pybind11_json
      GIT_REPOSITORY https://github.com/pybind/pybind11_json.git
      GIT_TAG        0.2.13
      GIT_SHALLOW True
  )
  FetchContent_GetProperties(pybind11_json)
  if(NOT pybind11_json_POPULATED)
      FetchContent_Populate(pybind11_json)
      add_subdirectory(${pybind11_json_SOURCE_DIR} ${pybind11_json_BINARY_DIR})
  endif()
endif()

add_subdirectory(synavis)
add_subdirectory(externtest)
add_subdirectory(native_modules)






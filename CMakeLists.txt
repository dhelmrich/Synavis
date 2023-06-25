#---------------------------------------------------------------
# Project UnrealReceiver
# 
# Copyright (c) 2021 Juelich Supercomputing Centre
# Forschungszentrum Juelich GmbH
# --------------------------------------------------------------
# License terms MIT License
# --------------------------------------------------------------
# Programmed for the inHPC Project
# --------------------------------------------------------------

cmake_minimum_required(VERSION 3.12.0)

set(projectname Synavis)
string(TOUPPER ${projectname} PROJECTNAME_UPPER)
string(TOLOWER ${projectname} PROJECTNAME_LOWER)
# lib path?

project (${projectname} VERSION 0.0.1)

set(ROOTDIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_CXX_STANDARD 23)

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






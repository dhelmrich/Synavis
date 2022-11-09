#---------------------------------------------------------------
# Project UnrealReceiver
# 
# Copyright (c) 2021 Juelich Supercomputing Centre
# Forschungszentrum Juelich GmbH
# --------------------------------------------------------------
# License terms tba
# --------------------------------------------------------------
# Programmed for the inHPC Project
# --------------------------------------------------------------

cmake_minimum_required(VERSION 3.12.0)

set(projectname WebRTCBridge)
string(TOUPPER ${projectname} PROJECTNAME_UPPER)
string(TOLOWER ${projectname} PROJECTNAME_LOWER)
# lib path?

project (${projectname} VERSION 0.0.1)

set(ROOTDIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_CXX_STANDARD 20)


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

include(FetchContent)
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11
    GIT_TAG        v2.8
    GIT_SHALLOW True
)

FetchContent_GetProperties(pybind11)
if(NOT pybind11_POPULATED)
    FetchContent_Populate(pybind11)
    add_subdirectory(${pybind11_SOURCE_DIR} ${pybind11_BINARY_DIR})
endif()

add_subdirectory(webrtcbridge)
add_subdirectory(externtest)






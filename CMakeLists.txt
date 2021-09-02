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

set(projectname UnrealReceiver)
string(TOUPPER ${projectname} PROJECTNAME_UPPER)
string(TOLOWER ${projectname} PROJECTNAME_LOWER)
# lib path?

project (${projectname})

set(ROOTDIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_CXX_STANDARD 20)

add_subdirectory(receiver)






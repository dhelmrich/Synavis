if(NOT DEFINED source)
  message(FATAL_ERROR "copy_if_exists.cmake requires -Dsource=<path>")
endif()
if(NOT DEFINED dest)
  message(FATAL_ERROR "copy_if_exists.cmake requires -Ddest=<dir>")
endif()

if(EXISTS "${source}")
  # Ensure destination directory exists
  file(MAKE_DIRECTORY "${dest}")
  # Copy the file into the destination directory
  file(COPY "${source}" DESTINATION "${dest}")
  message(STATUS "copy_if_exists: Copied '${source}' -> '${dest}'")
else()
  message(STATUS "copy_if_exists: Source '${source}' does not exist, skipping.")
endif()

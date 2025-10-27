# Projectname: ${projectname}
# PROJECTNAME: ${PROJECTNAME_UPPER}
# path: ${librarypath}

set(PYPROJECT "Py${projectname}")

# Make this CMake subproject scikit-build friendly by providing an install layout
# The idea: install the python extension and the python package sources into a
# single package-root directory so scikit-build (or any packaging step) can
# collect everything relative to that directory when creating a wheel.
include(GNUInstallDirs)
set(PY_PACKAGE_NAME "${PROJECTNAME_LOWER}" CACHE STRING "Name of the Python package to install (directory name)")


include(GenerateExportHeader)

file(GLOB RECEIVERSOURCES *.cpp)
file(GLOB RECEIVERHEADERS *.hpp)
set(PLAINSOURCES ${RECEIVERSOURCES})
list(FILTER PLAINSOURCES EXCLUDE REGEX ".*PySynavis.cpp$")
if(NOT BUILD_WITH_DECODING)
  list(FILTER PLAINSOURCES EXCLUDE REGEX "FrameDecodeAV.*$")
  list(FILTER RECEIVERSOURCES EXCLUDE REGEX "FrameDecodeAV.*$")
  list(FILTER RECEIVERHEADERS EXCLUDE REGEX "FrameDecodeAV.*$")
else()
  # export flag to C++ code
  add_definitions(-DBUILD_WITH_DECODING)
  
endif()
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -openmp:llvm -openmp:experimental")

find_package(Python 3.10 COMPONENTS Interpreter Development.Module REQUIRED)

# Only add the binding file(s) to the Python module
pybind11_add_module(${PYPROJECT} SHARED PySynavis.cpp)

add_library(${projectname} 
  ${PLAINSOURCES}
  ${RECEIVERHEADERS}
)

target_include_directories(${projectname}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/include
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/deps/json/single_include/nlohmann/
)
target_include_directories(${PYPROJECT}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/include
  ${CMAKE_BINARY_DIR}/_deps/pybind11_json-src/include/pybind11_json/
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/deps/json/single_include/nlohmann/
  ${CMAKE_BINARY_DIR}/_deps/pybind11-src/include
  # Python include dirs are provided by Python::Development target (linked below)
)

target_link_libraries(${projectname} PRIVATE datachannel-static nlohmann_json::nlohmann_json )
target_link_libraries(${PYPROJECT} PRIVATE ${projectname} datachannel-static nlohmann_json::nlohmann_json)
if(TARGET Python::Development)
  target_link_libraries(${PYPROJECT} PRIVATE Python::Development)
else()
  message(STATUS "Python::Development target not available; assuming pybind11 provided the necessary linkage")
endif()

# Ensure the built extension is placed in the normal library output directory
set_target_properties(${PYPROJECT} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/)

# Do not install targets directly into the absolute build directory. That
# creates absolute DESTINATION paths in the generated cmake_install.cmake which
# break scikit-build's collection of files when building wheels. Instead,
# install the python extension into the package directory below which is
# relative and compatible with scikit-build / CMake DESTDIR collection.

# scikit-build friendly install: install the python extension into a package
# directory named ${PY_PACKAGE_NAME}. When scikit-build calls `cmake --install` it
# will pass a DESTDIR and collect the package contents from that destination.
install(TARGETS ${PYPROJECT}
  LIBRARY DESTINATION ${PY_PACKAGE_NAME}
  ARCHIVE DESTINATION ${PY_PACKAGE_NAME}
  RUNTIME DESTINATION ${PY_PACKAGE_NAME}
)

# During the install step (cmake --install called by scikit-build) collect the
# runtime dependencies of the installed PySynavis shared object and copy any
# FFmpeg-related shared libraries into ${PY_PACKAGE_NAME}/lib inside the
# install DESTDIR (wheel staging dir). This ensures the wheel bundles the
# necessary libav* runtime libraries.
if(BUILD_WITH_DECODING OR DEFINED FFMPEG_LIBRARY_DIRS OR FFMPEG_FOUND)
  # Debug: print the configured library dirs and what matches we can find there
  if(DEFINED FFMPEG_LIBRARY_DIRS)
    message(STATUS "FFMPEG_LIBRARY_DIRS=${FFMPEG_LIBRARY_DIRS}")
    foreach(_dir ${FFMPEG_LIBRARY_DIRS})
      if(EXISTS ${_dir})
        file(GLOB _ffmpeg_libs_check "${_dir}/libav*.so*" "${_dir}/libsw*.so*" "${_dir}/libpost*.so*")
        if(_ffmpeg_libs_check)
          message(STATUS "  In ${_dir} found FFmpeg libs: ${_ffmpeg_libs_check}")
        else()
          message(STATUS "  In ${_dir} found no libav/libsw/libpost matches")
        endif()
      else()
        message(STATUS "  FFMPEG_LIBRARY_DIR does not exist: ${_dir}")
      endif()
    endforeach()
  else()
    message(STATUS "FFMPEG_LIBRARY_DIRS not defined")
  endif()
  install(CODE "
    # Construct the installed package directory inside DESTDIR
    set(_installed_pkg_dir \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${PY_PACKAGE_NAME}\")
    file(TO_CMAKE_PATH \"${_installed_pkg_dir}\" _installed_pkg_dir)
    # Find the installed PySynavis shared object (pattern match)
    file(GLOB _pyso_list \"${_installed_pkg_dir}/PySynavis*.so*\")
    if(NOT _pyso_list)
      message(WARNING \"Could not find installed PySynavis shared object in ${_installed_pkg_dir}\")
    else()
      foreach(_pyso_path IN LISTS _pyso_list)
        message(STATUS \"Scanning runtime dependencies for: ${_pyso_path}\")
        file(GET_RUNTIME_DEPENDENCIES
          EXECUTABLES \"${_pyso_path}\"
          RESOLVED_DEPENDENCIES_VAR _resolved_deps
          DIRECTORIES \"${CMAKE_INSTALL_PREFIX}\" \"${CMAKE_BINARY_DIR}\"\n        )
        foreach(_dep IN LISTS _resolved_deps)
          # We only want libav* / libsw* / libpost* type libs (FFmpeg related)
          get_filename_component(_dep_name \"${_dep}\" NAME)
          if(_dep_name MATCHES \"^(libav|libsw|libpost)\")
            message(STATUS \"Copying FFmpeg runtime lib into package: ${_dep}\")
            file(INSTALL DESTINATION \"${_installed_pkg_dir}/lib\" TYPE FILE FILES \"${_dep}\")
          endif()
        endforeach()
      endforeach()
    endif()
  ")
endif()

# Install python package sources (only .py files) into the same package directory
if(EXISTS ${CMAKE_SOURCE_DIR}/python)
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/python/
    DESTINATION ${PY_PACKAGE_NAME}
    FILES_MATCHING PATTERN "*.py"
    PATTERN "__pycache__" EXCLUDE
  )
endif()


if(UNIX)
  target_include_directories(${projectname} PUBLIC AFTER
    ${CMAKE_BINARY_DIR}/_deps/date-src/include)
  target_include_directories(${PYPROJECT} PUBLIC AFTER
    ${CMAKE_BINARY_DIR}/_deps/date-src/include)
  target_link_libraries(${projectname} PRIVATE date)
  target_link_libraries(${PYPROJECT} PRIVATE date)
  # also copy signalling_server.py from soure directory/python/modules to build directory
  file(COPY ${CMAKE_SOURCE_DIR}/python/modules/signalling_server.py DESTINATION ${CMAKE_BINARY_DIR})
endif()

# Ensure the installed python extension searches for bundled shared libs in a
# local `lib` directory next to the extension (so bundled ffmpeg libs can be
# found). This only applies to Unix-like systems.
if(UNIX)
  set_target_properties(${PYPROJECT} PROPERTIES INSTALL_RPATH "\$ORIGIN/lib")
endif()

if(BUILD_WITH_DECODING OR DEFINED FFMPEG_LIBRARY_DIRS OR FFMPEG_FOUND)
  target_link_directories(${projectname} PRIVATE ${FFMPEG_LIBRARY_DIRS})
  target_link_directories(${PYPROJECT} PRIVATE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(${projectname} PRIVATE ${FFMPEG_LIBRARIES})
  target_link_libraries(${PYPROJECT} PRIVATE ${FFMPEG_LIBRARIES})
  # add to the include directories
  target_include_directories(${projectname} PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_include_directories(${PYPROJECT} PRIVATE ${FFMPEG_INCLUDE_DIRS})

  # Collect FFmpeg runtime shared libraries from discovered library dirs and
  # install them into the python package under ${PY_PACKAGE_NAME}/lib so the
  # extension can load them at runtime via $ORIGIN/lib.
  if(DEFINED FFMPEG_LIBRARY_DIRS)
    set(_ffmpeg_globs "libavcodec*.so*" "libavformat*.so*" "libavutil*.so*" "libswresample*.so*" "libswscale*.so*")
    set(_ffmpeg_runtime_files)
    foreach(_dir IN LISTS FFMPEG_LIBRARY_DIRS)
      foreach(_g IN LISTS _ffmpeg_globs)
        file(GLOB _found "${_dir}/${_g}")
        list(APPEND _ffmpeg_runtime_files ${_found})
      endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _ffmpeg_runtime_files)
    if(_ffmpeg_runtime_files)
      message(STATUS "Packaging FFmpeg runtime libs into package: ${_ffmpeg_runtime_files}")
      install(FILES ${_ffmpeg_runtime_files} DESTINATION ${PY_PACKAGE_NAME}/lib)
    else()
      message(WARNING "FFmpeg found but no runtime .so files matched in FFMPEG_LIBRARY_DIRS")
    endif()
  endif()
endif()

# add the permissive flag for MSVC
if(MSVC)
  target_compile_options(${projectname} PRIVATE /permissive-)
  target_compile_options(${PYPROJECT} PRIVATE /permissive-)
endif()

generate_export_header(${projectname}
  EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/${projectname}/export.hpp
)

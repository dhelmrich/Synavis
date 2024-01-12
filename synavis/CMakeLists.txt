# Projectname: ${projectname}
# PROJECTNAME: ${PROJECTNAME_UPPER}
# path: ${librarypath}

set(PYPROJECT "Py${projectname}")

include(GenerateExportHeader)

file(GLOB RECEIVERSOURCES *.cpp)
file(GLOB RECEIVERHEADERS *.hpp)
set(PLAINSOURCES ${RECEIVERSOURCES})
list(FILTER PLAINSOURCES EXCLUDE REGEX ".*PySynavis.cpp$")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -openmp:llvm -openmp:experimental")

#set(PYBIND11_PYTHON_VERSION 3.9)
find_package( PythonInterp 3.9 REQUIRED )
find_package( PythonLibs 3.9 REQUIRED )

pybind11_add_module(${PYPROJECT} SHARED 
            ${RECEIVERSOURCES})

add_library(${projectname} 
  ${PLAINSOURCES}
  ${RECEIVERHEADERS}
)

target_include_directories(${projectname}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/include
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/deps/json/single_include/nlohmann/
  #${CMAKE_BINARY_DIR}/_deps/nlohmann_json-src/single_include/nlohmann/
  ${CMAKE_SOURCE_DIR}/unrealbridge
)
target_include_directories(${PYPROJECT}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/include
  ${CMAKE_BINARY_DIR}/_deps/pybind11_json-src/include/pybind11_json/
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/deps/json/single_include/nlohmann/
  #${CMAKE_BINARY_DIR}/_deps/nlohmann_json-src/single_include/nlohmann/
  ${CMAKE_BINARY_DIR}/_deps/pybind11-src/include
  ${CMAKE_SOURCE_DIR}/unrealbridge
  ${PYTHON_INCLUDE_DIRS}
)

target_link_libraries(${projectname} PRIVATE datachannel-static nlohmann_json::nlohmann_json )
target_link_libraries(${PYPROJECT} PRIVATE ${projectname} datachannel-static nlohmann_json::nlohmann_json ${PYTHON_LIBRARIES})

set_target_properties(${PYPROJECT} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/)

# install the library to the root of the build directory
install(TARGETS ${projectname} DESTINATION ${CMAKE_BINARY_DIR})
install(TARGETS ${PYPROJECT} DESTINATION ${CMAKE_BINARY_DIR})

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

if(BUILD_WITH_DECODING)
  target_link_directories(${projectname} PRIVATE ${FFMPEG_LIBRARY_DIRS})
  target_link_directories(${PYPROJECT} PRIVATE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(${projectname} PRIVATE ${FFMPEG_LIBRARIES})
  target_link_libraries(${PYPROJECT} PRIVATE ${FFMPEG_LIBRARIES})
  # add to the include directories
  target_include_directories(${projectname} PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_include_directories(${PYPROJECT} PRIVATE ${FFMPEG_INCLUDE_DIRS})
endif()

# add the permissive flag for MSVC
if(MSVC)
  target_compile_options(${projectname} PRIVATE /permissive-)
  target_compile_options(${PYPROJECT} PRIVATE /permissive-)
endif()

generate_export_header(${projectname}
  EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/${projectname}/export.hpp
)

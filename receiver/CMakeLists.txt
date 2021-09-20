
# Projectname: ${projectname}
# PROJECTNAME: ${PROJECTNAME_UPPER}
# path: ${librarypath}

message("This is ${projectname}")
set(PYPROJECT "Py${projectname}")

include(GenerateExportHeader)

file(GLOB RECEIVERSOURCES ./*.cpp)
file(GLOB RECEIVERHEADERS ./*.h)
set(PLAINSOURCES ${RECEIVERSOURCES})
list(FILTER PLAINSOURCES EXCLUDE REGEX ".*PyUnrealReceiver.cpp$")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -openmp:llvm -openmp:experimental")

find_package(OpenMP)
set(PYBIND11_PYTHON_VERSION 3.8)
find_package( PythonInterp 3.8 REQUIRED )
find_package( PythonLibs 3.8 REQUIRED )

pybind11_add_module(${PYPROJECT} SHARED 
            ${RECEIVERSOURCES})

add_library(${projectname} 
  ${PLAINSOURCES}
  ${RECEIVERHEADERS}
)



message(Warning "${ROOTDIRECTORY}/libdatachannel/include/rtc/")
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
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/deps/json/single_include/nlohmann/
  ${CMAKE_BINARY_DIR}/_deps/pybind11-src/include
  ${PYTHON_INCLUDE_DIRS}
)

target_link_libraries(${projectname} PRIVATE datachannel-static nlohmann_json::nlohmann_json )
if(OpenMP_CXX_FOUND)
    target_link_libraries(${projectname} PUBLIC OpenMP::OpenMP_CXX)
    target_link_libraries(${PYPROJECT} PUBLIC OpenMP::OpenMP_CXX)
endif()
target_link_libraries(${PYPROJECT} PRIVATE datachannel-static nlohmann_json::nlohmann_json ${PYTHON_LIBRARIES})
set_target_properties(${PYPROJECT} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/)

generate_export_header(${projectname}
  EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/${projectname}/export.hpp
)

# Projectname: ${projectname}
# PROJECTNAME: ${PROJECTNAME_UPPER}
# path: ${librarypath}

set(subname accessor)
set(pysubname PyAccessor)

include(GenerateExportHeader)

file(GLOB ACCESSORSOURCES ./*.cpp)
file(GLOB ACCESSORHEADERS ./*.hpp)

set(APLAINSOURCES ${ACCESSORSOURCES})
list(FILTER APLAINSOURCES EXCLUDE REGEX ".*PyAccessor.cpp$")

find_package(OpenMP)
set(PYBIND11_PYTHON_VERSION 3.9)
find_package( PythonInterp 3.9 REQUIRED )
find_package( PythonLibs 3.9 REQUIRED )

pybind11_add_module(PyAccessor SHARED 
            ${ACCESSORSOURCES})

add_library(${subname} 
  ${APLAINSOURCES}
  ${ACCESSORHEADERS}
)



message(Warning "${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/include")
target_include_directories(${subname}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/include
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/deps/json/single_include/nlohmann/
)
target_include_directories(${pysubname}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/include
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/deps/json/single_include/nlohmann/
  ${CMAKE_BINARY_DIR}/_deps/pybind11-src/include
  ${PYTHON_INCLUDE_DIRS}
)

target_link_libraries(${subname} PRIVATE datachannel-static nlohmann_json::nlohmann_json )
if(OpenMP_CXX_FOUND)
    target_link_libraries(${subname} PUBLIC OpenMP::OpenMP_CXX)
    target_link_libraries(${pysubname} PUBLIC OpenMP::OpenMP_CXX)
endif()
target_link_libraries(${pysubname} PRIVATE datachannel-static nlohmann_json::nlohmann_json ${PYTHON_LIBRARIES})
set_target_properties(${pysubname} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/)

generate_export_header(${subname}
  EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/${subname}/export.hpp
)


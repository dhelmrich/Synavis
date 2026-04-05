
# Projectname: ${projectname}
# PROJECTNAME: ${PROJECTNAME_UPPER}
# path: ${librarypath}

# if adios2 is not available, skip this test
find_package(ADIOS2 QUIET)
if(NOT ADIOS2_FOUND)
  message(STATUS "ADIOS2 not found, skipping ${Folder} test")
  return()
endif()

get_filename_component(Folder ${CMAKE_CURRENT_LIST_DIR} NAME)
string(REPLACE " " "_" Folder ${Folder})

file(GLOB TESTSOURCES ./*.cpp)
file(GLOB TESTHEADERS ./*.h)


add_executable(${Folder}
  ${TESTSOURCES}
  ${TESTHEADERS}
)

target_include_directories(${Folder}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../synavis
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/include
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/deps/json/single_include/nlohmann/
  #${CMAKE_BINARY_DIR}/_deps/nlohmann_json-src/single_include/nlohmann/

)

target_link_libraries(${Folder} PRIVATE Synavis datachannel-static nlohmann_json::nlohmann_json datachannel-static adios_connector)

if(TARGET adios2::adios2)
  target_link_libraries(${Folder} PUBLIC adios2::adios2)
# Fallback to variable-based approach (system install)
elseif(ADIOS2_FOUND)
  target_include_directories(${Folder} PUBLIC ${ADIOS2_INCLUDE_DIRS})
  target_link_libraries(${Folder} PUBLIC ${ADIOS2_LIBRARIES})
endif()

# Copy runtime DLLs
include(${CMAKE_CURRENT_LIST_DIR}/../copy_dlls.cmake)
CONFIGURE_DLL_COPY(${Folder} "${CMAKE_CURRENT_SOURCE_DIR}/..")


# Projectname: ${projectname}
# PROJECTNAME: ${PROJECTNAME_UPPER}
# path: ${librarypath}

get_filename_component(Folder ${CMAKE_CURRENT_LIST_DIR} NAME)
string(REPLACE " " "_" Folder ${Folder})

file(GLOB TESTSOURCES ./*.cpp)
file(GLOB TESTHEADERS ./*.h)


add_executable(${Folder}
  ${TESTSOURCES}
  ${TESTHEADERS}
)

# also search for CPlantBox
find_package(CPlantBox REQUIRED)

# find MPI
find_package(MPI REQUIRED)


if (UNIX)
  if(pkgconfig_FOUND)
    pkg_check_modules(TINYXML2 tinyxml2)
  else()
    find_package(tinyxml2 CONFIG REQUIRED)
  endif()
else ()
  find_package(tinyxml2 CONFIG REQUIRED)
endif ()

target_include_directories(${Folder}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../synavis
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/include
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/deps/json/single_include/nlohmann/
  #${CMAKE_BINARY_DIR}/_deps/nlohmann_json-src/single_include/nlohmann/
  ${CPlantBox_INCLUDE_DIRS}
  ${MPI_CXX_INCLUDE_PATH}
  ${tinyxml2_INCLUDE_DIRS}
)
if(UNIX)

  target_link_directories(${Folder} 
    PRIVATE ${CMAKE_BINARY_DIR}/_deps/libdatachannel-build
    PRIVATE ${CPlantBox_LIBRARY_DIR}
    PRIVATE ${CPlantBox_LIBRARY_DIR}/src/external/suitsparse/lib
    PRIVATE ${CPlantBox_LIBRARY_DIR}/src/external/sundials/lib
    PRIVATE ${tinyxml2_LIBRARIES}
  )
else()
  target_link_directories(${Folder} 
    PUBLIC ${CMAKE_BINARY_DIR}/_deps/libdatachannel-build/Debug
    PUBLIC ${CPlantBox_DIR}/${CMAKE_BUILD_TYPE}
    PRIVATE ${tinyxml2_LIBRARIES}
  )

  message(STATUS "CPlantBox_DIR: ${CPlantBox_DIR}")
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
  message(STATUS "TinyXML2_LIBRARIES: ${tinyxml2_LIBRARIES}")
message(STATUS "TinyXML2_INCLUDE_DIRS: ${tinyxml2_INCLUDE_DIRS}")

  # array of possible build types
  #list( APPEND CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo MinSizeRel )
  # copy dlls to build directory for each build type
  #foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
  #    add_custom_command(TARGET ${Folder} PRE_LINK
  #      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CPlantBox_DIR}/${OUTPUTCONFIG}/CPlantBox.dll" "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUTCONFIG}/CPlantBox.dll"
  #    )
  #endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

endif()

target_link_libraries(${Folder} 
  PUBLIC Synavis 
    datachannel-static
    nlohmann_json::nlohmann_json
    datachannel-static
    CPlantBox
    ${tinyxml2_LIBRARIES}
    ${MPI_CXX_LIBRARIES}
    amd
    colamd
    btf
    suitesparseconfig
    klu
)


# Projectname: ${projectname}
# PROJECTNAME: ${PROJECTNAME_UPPER}
# path: ${librarypath}

set(mainname bridge)

include(GenerateExportHeader)

file(GLOB BRIDGESOURCES ./*.cpp)
file(GLOB BRIDGEHEADERS ./*.hpp)

add_library(${subname} 
  ${BRIDGESOURCES}
  ${BRIDGEHEADERS}
)



message(Warning "${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/include")
target_include_directories(${mainname}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/include
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/deps/json/single_include/nlohmann/
)

target_link_libraries(${mainname} PRIVATE datachannel-static nlohmann_json::nlohmann_json )

generate_export_header(${mainname}
  EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/${mainname}/export.hpp
)


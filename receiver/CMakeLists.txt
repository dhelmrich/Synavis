
# Projectname: ${projectname}
# PROJECTNAME: ${PROJECTNAME_UPPER}
# path: ${librarypath}

message("This is ${projectname}")


file(GLOB RECEIVERSOURCES ./*.cpp)
file(GLOB RECEIVERHEADERS ./*.h)


add_executable(${projectname} 
  ${RECEIVERSOURCES}
  ${RECEIVERHEADERS}
)

message(Warning "${ROOTDIRECTORY}/libdatachannel/include/rtc/")
target_include_directories(${projectname}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/include
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/deps/json/single_include/nlohmann/
)

target_link_libraries(${projectname} PRIVATE datachannel-static nlohmann_json::nlohmann_json datachannel-static)



# Projectname: ${projectname}
# PROJECTNAME: ${PROJECTNAME_UPPER}
# path: ${librarypath}

get_filename_component(Folder ${CMAKE_CURRENT_LIST_DIR} NAME)
string(REPLACE " " "_" Folder ${Folder})

message("This is ${projectname}")

file(GLOB TESTSOURCES ./*.cpp)
file(GLOB TESTHEADERS ./*.h)


add_executable(${Folder}
  ${TESTSOURCES}
  ${TESTHEADERS}
)

message(Warning "${ROOTDIRECTORY}/libdatachannel/include/rtc/")
target_include_directories(${Folder}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../webrtcbridge
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/include
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/deps/json/single_include/nlohmann/
  #${CMAKE_BINARY_DIR}/_deps/nlohmann_json-src/single_include/nlohmann/

)

target_link_libraries(${Folder} PRIVATE WebRTCBridge datachannel-static nlohmann_json::nlohmann_json datachannel-static)


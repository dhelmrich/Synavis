
# Projectname: ${projectname}
# PROJECTNAME: ${PROJECTNAME_UPPER}
# path: ${librarypath}

message("This is ${projectname}")

file(GLOB TESTSOURCES ./*.cpp)
file(GLOB TESTHEADERS ./*.h)


add_executable(externtest
  ${TESTSOURCES}
  ${TESTHEADERS}
)

message(Warning "${ROOTDIRECTORY}/libdatachannel/include/rtc/")
target_include_directories(externtest
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../webrtcbridge
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/include
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/deps/json/single_include/nlohmann/

)

target_link_libraries(externtest PRIVATE WebRTCBridge datachannel-static nlohmann_json::nlohmann_json datachannel-static)


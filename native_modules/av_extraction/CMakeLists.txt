
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

target_include_directories(${Folder}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../synavis
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/include
  ${CMAKE_BINARY_DIR}/_deps/libdatachannel-src/deps/json/single_include/nlohmann/

  #${CMAKE_BINARY_DIR}/_deps/nlohmann_json-src/single_include/nlohmann/

)

target_link_libraries(${Folder} PRIVATE Synavis datachannel-static nlohmann_json::nlohmann_json datachannel-static)

if(BUILD_WITH_DECODING)
  target_link_libraries(${Folder} PRIVATE ${FFMPEG_LIBRARIES})
  target_include_directories(${Folder} PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(${Folder} PRIVATE ${FFMPEG_LIBRARY_DIRS})
endif()


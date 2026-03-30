cmake_minimum_required(VERSION 3.12)

project(${projectname})
include(GenerateExportHeader)

# Use GLOB to collect sources/headers (project convention)
file(GLOB ADIOS_SOURCES "*.cpp")
file(GLOB ADIOS_HEADERS "*.hpp")

add_library(adios_connector SHARED
	${ADIOS_SOURCES}
	${ADIOS_HEADERS}
)

set_target_properties(adios_connector PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(adios_connector
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
)

# Allow other synavis targets to link against the connector
target_link_libraries(adios_connector PUBLIC ${projectname})

target_include_directories(adios_connector PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

# Define ADIOS2_AVAILABLE for conditional compilation
target_compile_definitions(adios_connector PUBLIC ADIOS2_AVAILABLE)

# Use the ADIOS2 already found by the parent CMakeLists.txt
if(TARGET adios2::adios2)
  target_link_libraries(adios_connector PUBLIC adios2::adios2)
elseif(ADIOS2_FOUND)
  target_include_directories(adios_connector PUBLIC ${ADIOS2_INCLUDE_DIRS})
  target_link_libraries(adios_connector PUBLIC ${ADIOS2_LIBRARIES})
endif()

# Install helper (no runtime artifacts expected for static lib)
install(TARGETS adios_connector
	ARCHIVE DESTINATION lib
	LIBRARY DESTINATION lib
	RUNTIME DESTINATION bin
)

generate_export_header(adios_connector
  EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/adios_export.hpp
)


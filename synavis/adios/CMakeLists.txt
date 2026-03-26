cmake_minimum_required(VERSION 3.12)

project(synavis_adios_connector)

# Use GLOB to collect sources/headers (project convention)
file(GLOB ADIOS_SOURCES "*.cpp")
file(GLOB ADIOS_HEADERS "*.hpp")

add_library(adios_connector STATIC
	${ADIOS_SOURCES}
	${ADIOS_HEADERS}
)

target_include_directories(adios_connector
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
)

# Allow other synavis targets to link against the connector
target_link_libraries(adios_connector PUBLIC ${projectname})

# Define ADIOS2_AVAILABLE for conditional compilation
target_compile_definitions(adios_connector PUBLIC ADIOS2_AVAILABLE)

# Use the ADIOS2 already found by the parent CMakeLists.txt
# ADIOS2_INCLUDE_DIRS and ADIOS2_LIBRARIES are already available from the parent scope
if(ADIOS2_FOUND)
  target_include_directories(adios_connector PUBLIC ${ADIOS2_INCLUDE_DIRS})
  target_link_libraries(adios_connector PUBLIC ${ADIOS2_LIBRARIES})
endif()

# Install helper (no runtime artifacts expected for static lib)
install(TARGETS adios_connector
	ARCHIVE DESTINATION lib
	LIBRARY DESTINATION lib
	RUNTIME DESTINATION bin
)


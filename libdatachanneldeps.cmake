# libdatachanneldeps.cmake
# Usage: cmake -Dtarget_path=... -Ddest_dir=... -P libdatachanneldeps.cmake

message(STATUS "🔍 Scanning runtime dependencies for: ${target_path}")
message(STATUS "📁 Destination directory: ${dest_dir}")

file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${target_path}"
  RESOLVED_DEPENDENCIES_VAR resolved_deps
  UNRESOLVED_DEPENDENCIES_VAR unresolved_deps
  DIRECTORIES "${CMAKE_BINARY_DIR}" "${CMAKE_INSTALL_PREFIX}"
)

foreach(dep IN LISTS resolved_deps)
  if(dep MATCHES "^${CMAKE_BINARY_DIR}")
    message(STATUS "📦 Copying subproject dependency: ${dep}")
    file(INSTALL
      DESTINATION "${dest_dir}"
      TYPE FILE
      FILES "${dep}"
    )
  else()
    message(STATUS "⏭️ Skipping external dependency: ${dep}")
  endif()
endforeach()

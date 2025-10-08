message(STATUS "Copying runtime dependencies for target: ${target_path} to ${dest_dir}")

file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${target_path}"
  RESOLVED_DEPENDENCIES_VAR resolved_deps
  UNRESOLVED_DEPENDENCIES_VAR unresolved_deps
  DIRECTORIES "${CMAKE_BINARY_DIR}" "${CMAKE_INSTALL_PREFIX}"
)

message(STATUS "Total dependencies found for target ${target}: ${resolved_deps}")
if (unresolved_deps)
  message(WARNING "Unresolved dependencies for target ${target}: ${unresolved_deps}")
endif()
foreach(dep IN LISTS resolved_deps)
  file(INSTALL
    DESTINATION "${dest_dir}"
    TYPE FILE
    FILES "${dep}"
  )
endforeach()

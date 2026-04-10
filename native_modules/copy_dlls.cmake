# Function to copy DLLs for a target - call this after add_executable/add_library
function(CONFIGURE_DLL_COPY target_name source_dir)
  if(NOT WIN32)
    return()
  endif()

  # Use generator expression to get the actual runtime output directory
  set(output_dir "$<TARGET_FILE_DIR:${target_name}>")
  
  # Use generator expressions for source paths to handle multi-config builds
  set(_datachannel_dll_dir "${CMAKE_BINARY_DIR}/_deps/libdatachannel-build/$<CONFIG>")
  set(_synavis_dll_dir "${CMAKE_BINARY_DIR}/synavis")
  set(_adios_dll_dir "${CMAKE_BINARY_DIR}/synavis/adios")
  
  # Copy datachannel.dll
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_datachannel_dll_dir}/datachannel.dll"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMENT "Copying datachannel.dll to $<TARGET_FILE_DIR:${target_name}>/"
  )
  
  # Copy Synavis.dll (main library)
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_synavis_dll_dir}/$<CONFIG>/Synavis.dll"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMENT "Copying Synavis.dll to $<TARGET_FILE_DIR:${target_name}>/"
  )
  
  # Copy adios_connector.dll
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_adios_dll_dir}/$<CONFIG>/adios_connector.dll"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMENT "Copying adios_connector.dll to $<TARGET_FILE_DIR:${target_name}>/"
  )
  
  # Copy ADIOS2 DLLs
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_adios_dll_dir}/$<CONFIG>/adios2_core.dll"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMENT "Copying adios2_core.dll to $<TARGET_FILE_DIR:${target_name}>/"
  )
  
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_adios_dll_dir}/$<CONFIG>/adios2_cxx11.dll"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMENT "Copying adios2_cxx11.dll to $<TARGET_FILE_DIR:${target_name}>/"
  )
endfunction()

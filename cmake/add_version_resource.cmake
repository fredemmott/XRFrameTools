include_guard(GLOBAL)

function(add_version_resource TARGET)
  set(VERSION_RC "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.version.rc")
  set(VERSION_RC_CONFIGURED "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.version.rc.configured")

  get_target_property(TARGET_TYPE ${TARGET} TYPE)
  if(TARGET_TYPE STREQUAL "EXECUTABLE")
    set(VFT_FILETYPE "VFT_APP")
  else()
    set(VFT_FILETYPE "VFT_DLL")
  endif()

  configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/version.rc.in"
    "${VERSION_RC_CONFIGURED}"
    @ONLY
    NEWLINE_STYLE UNIX
  )
  file(
    GENERATE
    OUTPUT "${VERSION_RC}"
    INPUT "${VERSION_RC_CONFIGURED}"
    NEWLINE_STYLE UNIX
  )
  target_sources(${TARGET} PRIVATE ${VERSION_RC})
endfunction()
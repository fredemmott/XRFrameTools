  set(LICENSE_RTF "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.rtf")
  add_custom_command(
    OUTPUT "${LICENSE_RTF}"
    COMMAND
    powershell
    -ExecutionPolicy Bypass
    "${CMAKE_CURRENT_SOURCE_DIR}/ConvertTo-Rtf.ps1"
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${LICENSE_RTF}"
    MAIN_DEPENDENCY "${CMAKE_SOURCE_DIR}/LICENSE"
    DEPENDS ConvertTo-Rtf.ps1
  )
  add_custom_target(license-rtf ALL SOURCES "${LICENSE_RTF}")
  install(FILES "${LICENSE_RTF}" DESTINATION "installer")

  set(IS_TAGGED_BUILD_JSON "false")
  set(IS_STABLE_RELEASE_JSON "false")
  if(IS_TAGGED_BUILD)
    set(IS_TAGGED_BUILD_JSON "true")
  endif()
  if(IS_STABLE_RELEASE)
    set(IS_STABLE_RELEASE_JSON "false")
  endif()
  set(VERSION_JSON "${CMAKE_CURRENT_BINARY_DIR}/version.json")
  configure_file(version.json.in "${CMAKE_CURRENT_BINARY_DIR}/version.json")
  install(FILES "${VERSION_JSON}" DESTINATION "installer")

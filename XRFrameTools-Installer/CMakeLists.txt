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
install(
  FILES "${LICENSE_RTF}"
  DESTINATION "installer"
)
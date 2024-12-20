include(ExternalProject)

set(UPDATER_EXE "fredemmott_XRFrameTools_Updater.exe")

file(
  DOWNLOAD
  "https://github.com/fredemmott/autoupdates/releases/download/vicius-v1.8.876%2Bfredemmott.1/Updater-Release.exe"
  "${CMAKE_CURRENT_BINARY_DIR}/${UPDATER_EXE}"
  EXPECTED_HASH "SHA256=bc29aac94233a95aa2b5f00360e0b3b0c4cd89e92ef3c2dca5a15bdde84e7d9c"
)
add_custom_target(
  vicius
  ALL
  COMMAND
  "${CMAKE_COMMAND}"
  -E copy_if_different
  "${CMAKE_CURRENT_BINARY_DIR}/${UPDATER_EXE}"
  "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${UPDATER_EXE}"
)

install(
  FILES
  "${CMAKE_CURRENT_BINARY_DIR}/${UPDATER_EXE}"
  DESTINATION bin
)
include(ExternalProject)

set(UPDATER_EXE "fredemmott_XRFrameTools_Updater.exe")

file(
  DOWNLOAD
  "https://github.com/fredemmott/autoupdates/releases/download/vicius-v1.8.876%2Bfredemmott.2/Updater-Release.exe"
  "${CMAKE_CURRENT_BINARY_DIR}/${UPDATER_EXE}"
  EXPECTED_HASH "SHA256=d0cadc82c17ca8d39987837e9d89b7cb3079437353ecf081e579abc14ecce317"
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
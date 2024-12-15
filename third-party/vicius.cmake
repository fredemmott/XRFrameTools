include(ExternalProject)

set(UPDATER_EXE "fredemmott_XRFrameTools_Updater.exe")

file(
  DOWNLOAD
  "https://github.com/fredemmott/autoupdates/releases/download/vicius-v1.7.813%2Bfredemmott.3/Updater-Release.exe"
  "${CMAKE_CURRENT_BINARY_DIR}/${UPDATER_EXE}"
  EXPECTED_HASH SHA256=3826B6461A9B865DB9307AECED11F33B07A21A4F9A849DBAA07C9C1DE9F012AC
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
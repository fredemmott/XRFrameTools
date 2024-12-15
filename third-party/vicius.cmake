include(ExternalProject)

set(UPDATER_EXE_FILENAME "fredemmott_XRFrameTools_Updater.exe")

set(
  UPDATER_EXE_PATH
  "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${UPDATER_EXE_FILENAME}"
)
file(
  DOWNLOAD
  "https://github.com/fredemmott/autoupdates/releases/download/vicius-v1.7.813%2Bfredemmott.3/Updater-Release.exe"
  "${UPDATER_EXE_PATH}"
  EXPECTED_HASH SHA256=3826B6461A9B865DB9307AECED11F33B07A21A4F9A849DBAA07C9C1DE9F012AC
)
install(
  FILES
  "${UPDATER_EXE_PATH}"
  DESTINATION bin
)

return(PROPAGATE UPDATER_EXE_PATH UPDATER_EXE_FILENAME)
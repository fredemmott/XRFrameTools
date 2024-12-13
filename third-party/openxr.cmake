include(ExternalProject)
ExternalProject_Add(
  openxr-ep
  URL "https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.43.zip"
  URL_HASH "SHA256=19fc7d73671ca8814572fb8825561ef756427d2449a4fd250e1f26f9ec9df3a9"
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""

  EXCLUDE_FROM_ALL
  DOWNLOAD_EXTRACT_TIMESTAMP ON

  STEP_TARGETS download
)

ExternalProject_Get_property(openxr-ep SOURCE_DIR)

add_library(openxr INTERFACE)
add_dependencies(openxr openxr-ep-download)
target_include_directories(
  openxr
  INTERFACE
  "${SOURCE_DIR}/src/common"
  "${SOURCE_DIR}/include"
)

include(add_copyright_file)
add_copyright_file("OpenXR SDK" "${SOURCE_DIR}/LICENSE" DEPENDS openxr-ep-download)

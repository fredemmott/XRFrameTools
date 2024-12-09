include(ExternalProject)

ExternalProject_Add(
  openxr_ep
  URL "https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.43.zip"
  URL_HASH "SHA256=19fc7d73671ca8814572fb8825561ef756427d2449a4fd250e1f26f9ec9df3a9"
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""

  EXCLUDE_FROM_ALL
  DOWNLOAD_EXTRACT_TIMESTAMP ON
)

ExternalProject_Get_property(openxr_ep SOURCE_DIR)

add_library(openxr INTERFACE)
add_dependencies(openxr openxr_ep)
target_include_directories(
  openxr
  INTERFACE
  "${SOURCE_DIR}/src/common"
  "${SOURCE_DIR}/include"
)

install(
  FILES "${SOURCE_DIR}/LICENSE"
  TYPE DOC
  RENAME "LICENSE-ThirdParty-OpenXR SDK.txt"
)

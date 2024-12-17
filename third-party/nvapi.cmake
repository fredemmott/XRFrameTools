include(ExternalProject)
ExternalProject_Add(
  nvapi-ep
  GIT_REPOSITORY "https://github.com/NVIDIA/nvapi.git"
  GIT_TAG 973d7e548ef2ed4c3f72e9065bba25999496dfac # R565
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""

  EXCLUDE_FROM_ALL
  DOWNLOAD_EXTRACT_TIMESTAMP ON

  STEP_TARGETS download
)

ExternalProject_Get_property(nvapi-ep SOURCE_DIR)

add_library(nvapi INTERFACE)
add_dependencies(nvapi nvapi-ep-download)
target_include_directories(
  nvapi
  INTERFACE
  "${SOURCE_DIR}"
  "${SOURCE_DIR}"
)
if(BUILD_BITNESS EQUAL 64)
  target_link_libraries(nvapi INTERFACE "${SOURCE_DIR}/amd64/nvapi64.lib")
else()
  target_link_libraries(nvapi INTERFACE "${SOURCE_DIR}/x86/nvapi.lib")
endif()

include(add_copyright_file)
add_copyright_file("nvapi" "${SOURCE_DIR}/License.txt" DEPENDS nvapi-ep-download)

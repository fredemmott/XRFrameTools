include(ExternalProject)

if(BUILD_BITNESS EQUAL 64)
  set(NVAPI_LIB "amd64/nvapi64.lib")
else()
  set(NVAPI_LIB "x86/nvapi.lib")
endif()
ExternalProject_Add(
  nvapi-ep
  GIT_REPOSITORY "https://github.com/NVIDIA/nvapi.git"
  GIT_TAG 7cb76fce2f52de818b3da497af646af1ec16ce27 # R575
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""

  EXCLUDE_FROM_ALL
  DOWNLOAD_EXTRACT_TIMESTAMP ON

  STEP_TARGETS download
  BUILD_BYPRODUCTS "<SOURCE_DIR>/${NVAPI_LIB}"

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
target_link_libraries(nvapi INTERFACE "${SOURCE_DIR}/${NVAPI_LIB}")

include(add_copyright_file)
add_copyright_file("nvapi" "${SOURCE_DIR}/License.txt" DEPENDS nvapi-ep-download)

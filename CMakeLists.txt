cmake_minimum_required(VERSION 3.29)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
  # Standard C++ exception behavior
  "/EHsc"
  # Include content and marker in error messages
  "/diagnostics:caret"
)
add_compile_definitions(
  "NOMINMAX=1"
  "UNICODE=1"
  "WIN32_LEAN_AND_MEAN"
)

include(cmake/hybrid-crt.cmake)

set(
  CMAKE_TOOLCHAIN_FILE
  "${CMAKE_SOURCE_DIR}/third-party/vcpkg/scripts/buildsystems/vcpkg.cmake"
  CACHE STRING "Vcpkg toolchain file"
)

project(XRFrameTools VERSION 0.0.1 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

block(
  PROPAGATE
  CMAKE_PDB_OUTPUT_DIRECTORY
  CMAKE_RUNTIME_OUTPUT_DIRECTORY
  CMAKE_LIBRARY_OUTPUT_DIRECTORY
)
  include(cmake/output-directories.cmake)
endblock()

include(cmake/output-directories.cmake)

block(PROPAGATE XRFrameTools_ABI_ID)
  string(TIMESTAMP CONFIGURE_TIMESTAMP "%Y%m%dT%H%M%S/%f" UTC)
  set(
    XRFrameTools_ABI_ID
    "${CMAKE_PROJECT_VERSION}/${CONFIGURE_TIMESTAMP}"
    CACHE
    STRING "Unique ID for this build to avoid inter-module ABI issues")
endblock()

add_subdirectory("third-party")
add_subdirectory("src")
add_subdirectory("XRFrameTools-Installer")

add_copyright_file(SELF LICENSE)

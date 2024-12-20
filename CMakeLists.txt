cmake_minimum_required(VERSION 3.29)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
  # Standard C++ exception behavior
  "/EHsc"
  # Include content and marker in error messages
  "/diagnostics:caret"
  # Source is UTF-8
  "/utf-8"
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

set(VERSION_TWEAK 0 CACHE STRING "Final component of build number")
project(XRFrameTools VERSION 0.0.4.${VERSION_TWEAK} LANGUAGES CXX)
set(VERSION_TWEAK_LABEL "local" CACHE STRING "Human-readable label for build number")
set(
  READABLE_VERSION
  "${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}.${CMAKE_PROJECT_VERSION_PATCH}+${VERSION_TWEAK_LABEL}.${CMAKE_PROJECT_VERSION_TWEAK}")
option(IS_TAGGED_BUILD "Whether or not we're building a tagged version" OFF)
option(IS_STABLE_RELEASE "Whether or not we're building a stable build" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
math(EXPR BUILD_BITNESS "${CMAKE_SIZEOF_VOID_P} * 8")
block(PROPAGATE BUILD_UI)
  if(BUILD_BITNESS EQUAL 64)
    set(BUILD_UI_DEFAULT ON)
  else()
    set(BUILD_UI_DEFAULT OFF)
  endif()
  option(BUILD_UI "Enable building the UI components" ${BUILD_UI_DEFAULT})
endblock()

include(cmake/output-directories.cmake)

block(PROPAGATE XRFrameTools_ABI_ID)
  set(
    XRFrameTools_ABI_ID
    "${CMAKE_PROJECT_VERSION}/${CMAKE_PROJECT_VERSION}"
    CACHE
    STRING "Unique ID for this build to avoid inter-module ABI issues")
endblock()

add_subdirectory("third-party")
add_subdirectory("src")
add_subdirectory("XRFrameTools-Installer")

add_copyright_file(SELF LICENSE)

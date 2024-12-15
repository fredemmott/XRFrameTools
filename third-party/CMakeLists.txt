block()
  include(openxr.cmake)
  if(BUILD_UI)
    include(vicius.cmake)
  endif()
endblock()

include(add_copyright_file)

set(NOT_DISTRIBUTED vcpkg-cmake vcpkg-cmake-config)

file(GLOB_RECURSE THIRD_PARTY_LICENSES "${VCPKG_INSTALLED_DIR}/${VCPKG_PKG_TARGET_TRIPLET}/*/copyright")
foreach(FILE IN LISTS THIRD_PARTY_LICENSES)
  cmake_path(GET FILE PARENT_PATH DIRECTORY)
  cmake_path(GET DIRECTORY FILENAME PACKAGE_NAME)
  if(NOT "${PACKAGE_NAME}" IN_LIST NOT_DISTRIBUTED)
    add_copyright_file("${PACKAGE_NAME}" "${FILE}")
  endif()
endforeach()
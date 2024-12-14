include_guard(GLOBAL)
include(output-directories)

add_custom_target(licenses ALL)

function(add_copyright_file PACKAGE_NAME SOURCE)
  set(options)
  set(oneValueArgs)
  set(multiValueArgs DEPENDS)
  cmake_parse_arguments(arg "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  cmake_path(ABSOLUTE_PATH SOURCE NORMALIZE)

  if("${PACKAGE_NAME}" STREQUAL "SELF")
    set(PACKAGE_DOC_DIR "")
  else()
    set(PACKAGE_DOC_DIR "third-party/${PACKAGE_NAME}")
  endif()

  set(BUILD_TREE_TARGET_DIR "${BUILD_OUT_ROOT}/share/doc/${PACKAGE_DOC_DIR}")
  set(BUILD_TREE_TARGET "${BUILD_TREE_TARGET_DIR}/LICENSE.txt")
  cmake_path(ABSOLUTE_PATH BUILD_TREE_TARGET NORMALIZE)

  add_custom_command(
    OUTPUT
    "${BUILD_TREE_TARGET}"
    COMMAND
    "${CMAKE_COMMAND}"
    -E make_directory "${BUILD_TREE_TARGET_DIR}"
    COMMAND
    "${CMAKE_COMMAND}"
    -E copy_if_different
    "${SOURCE}"
    "${BUILD_TREE_TARGET}"
    DEPENDS "${arg_DEPENDS}"
    VERBATIM
  )
  string(MAKE_C_IDENTIFIER "license-gen-${PACKAGE_NAME}" SUBTARGET)
  add_custom_target("${SUBTARGET}" SOURCES "${BUILD_TREE_TARGET}")
  add_dependencies(licenses "${SUBTARGET}")

  install(
    FILES "${SOURCE}"
    TYPE DOC
    RENAME "${PACKAGE_DOC_DIR}/LICENSE.txt"
    COMPONENT licenses
  )
endfunction()
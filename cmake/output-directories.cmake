include_guard(GLOBAL)

set(BUILD_OUT_PREFIX "${CMAKE_BINARY_DIR}/out" CACHE PATH "Location for file build artifacts")
get_property(GENERATOR_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(GENERATOR_IS_MULTI_CONFIG)
  set(BUILD_OUT_ROOT "${BUILD_OUT_PREFIX}/$<CONFIG>")
else()
  set(BUILD_OUT_ROOT "${BUILD_OUT_PREFIX}/${CMAKE_BUILD_TYPE}")
endif()
set(BUILD_OUT_ROOT "${BUILD_OUT_ROOT}" CACHE PATH "Root for build artifacts")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${BUILD_OUT_ROOT}/pdb")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BUILD_OUT_ROOT}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BUILD_OUT_ROOT}/lib")

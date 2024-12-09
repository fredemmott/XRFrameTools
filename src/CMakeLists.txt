math(EXPR BUILD_BITNESS "${CMAKE_SIZEOF_VOID_P} * 8")

find_package(wil CONFIG REQUIRED)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/gen/ABIKey.in.hpp"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/include/XRFrameTools/ABIKey.hpp"
  @ONLY
  NEWLINE_STYLE UNIX
)
add_library(
  abi
  STATIC
  "${CMAKE_CURRENT_BINARY_DIR}/generated/include/XRFrameTools/ABIKey.hpp"
  FrameMetrics.hpp
  SHM.hpp
  SHM.cpp
  PerformanceCountersToDuration.cpp
  PerformanceCountersToDuration.hpp
)
target_link_libraries(
  abi
  PUBLIC
  WIL::WIL
)
target_include_directories(
  abi
  PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated/include"
)

macro(add_xr_api_layer_json TARGET)
  set(LAYER "XR_APILAYER_FREDEMMOTT_${TARGET}")
  set(CONFIGURED "${CMAKE_CURRENT_BINARY_DIR}/${LAYER}.json")
  set(TARGET "${TARGET}")
  configure_file(APILayer.in.json "${CONFIGURED}" @ONLY NEWLINE_STYLE LF)
  set(GENERATED "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${LAYER}${BUILD_BITNESS}.json")
  file(
    GENERATE
    OUTPUT "${GENERATED}"
    INPUT "${CONFIGURED}"
    NEWLINE_STYLE UNIX
  )
  install(
    FILES "${GENERATED}"
    TYPE LIB
    COMPONENT layers
  )
endmacro()

macro(add_xr_api_layer TARGET)
  add_library(${TARGET} MODULE ${ARGN})
  target_link_libraries(${TARGET} PRIVATE openxr abi)
  set_target_properties(
    ${TARGET}
    PROPERTIES
    OUTPUT_NAME "XR_APILAYER_FREDEMMOTT_${TARGET}${BUILD_BITNESS}"
    ENABLE_EXPORTS ON
  )

  install(TARGETS ${TARGET} DESTINATION lib COMPONENT layers)

  add_xr_api_layer_json(${TARGET})
endmacro()

add_xr_api_layer(
  core_metrics
  XR_APILAYER_FREDEMMOTT_core_metrics.cpp
)

add_executable(cli cli.cpp)
set_target_properties(cli PROPERTIES OUTPUT_NAME "XRFrameTools-cli")
target_link_libraries(cli PRIVATE abi)
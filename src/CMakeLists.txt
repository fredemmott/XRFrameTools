include(add_version_resource)

find_package(imgui CONFIG REQUIRED)
find_package(wil CONFIG REQUIRED)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/lib")

add_subdirectory(lib)

add_executable(TestApiLayerEntryPoint EXCLUDE_FROM_ALL TestAPILayerEntryPoint.cpp)
target_link_libraries(TestApiLayerEntryPoint PRIVATE WIL::WIL)
set_target_properties(
  TestApiLayerEntryPoint
  PROPERTIES
  # don't put them in `bin/` and `pdb/` as they're just not meant for distribution
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
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
    COMPONENT api-layers
  )

  add_custom_command(
    TARGET "${TARGET}"
    POST_BUILD
    COMMAND "$<TARGET_FILE:TestApiLayerEntryPoint>" "$<TARGET_FILE:${TARGET}>"
    VERBATIM
  )
endmacro()

add_custom_target(api-layers ALL)
macro(add_xr_api_layer TARGET)
  add_library(${TARGET} MODULE ${ARGN} FrameMetricsStore.hpp APILayerAPI.hpp)
  target_link_libraries(${TARGET} PRIVATE openxr Config ABIKey)
  set_target_properties(
    ${TARGET}
    PROPERTIES
    OUTPUT_NAME "XR_APILAYER_FREDEMMOTT_${TARGET}${BUILD_BITNESS}"
    ENABLE_EXPORTS ON
  )
  target_compile_definitions(${TARGET} PRIVATE "API_LAYER_TARGET_NAME=\"${TARGET}${BUILDBITNESS}\"")
  add_version_resource(${TARGET})

  add_dependencies(api-layers "${TARGET}")
  install(TARGETS ${TARGET} DESTINATION lib COMPONENT api-layers)

  add_xr_api_layer_json(${TARGET})
endmacro()

add_xr_api_layer(core_metrics XR_APILAYER_FREDEMMOTT_core_metrics.cpp)
target_link_libraries(
  core_metrics
  PRIVATE
  BinaryLogWriter
  SHMWriter
)
target_sources(
  core_metrics
  PRIVATE
  FrameMetricsStore.cpp
)
add_xr_api_layer(d3d11_metrics XR_APILAYER_FREDEMMOTT_d3d11_metrics.cpp)
target_link_libraries(d3d11_metrics PRIVATE D3D11GpuTimer)

add_xr_api_layer(nvapi_metrics XR_APILAYER_FREDEMMOTT_nvapi_metrics.cpp)
target_link_libraries(nvapi_metrics PRIVATE nvapi)

add_xr_api_layer(
  d3d11_overlay
  XR_APILAYER_FREDEMMOTT_d3d11_overlay.cpp
  imgui_impl_win32_headless.cpp imgui_impl_win32_headless.hpp
)
target_link_libraries(d3d11_overlay PRIVATE imgui::imgui SHMReader)

if(NOT BUILD_UI)
  return()
endif()

find_package(implot CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)

add_executable(
  binlog-to-csv
  binlog-to-csv.cpp
  utf8.manifest
)
target_link_libraries(
  binlog-to-csv
  PRIVATE
  CSVWriter
  PerformanceCounters
  BinaryLogReader
  magic_enum::magic_enum
)
add_version_resource(binlog-to-csv)
install(TARGETS binlog-to-csv DESTINATION bin)

include(app.cmake)

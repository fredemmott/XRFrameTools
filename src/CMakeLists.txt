math(EXPR BUILD_BITNESS "${CMAKE_SIZEOF_VOID_P} * 8")

find_package(wil CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/lib")

add_subdirectory(lib)

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
  target_link_libraries(${TARGET} PRIVATE openxr SHMWriter BinaryLogWriter)
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
target_link_libraries(
  cli
  PRIVATE
  FrameMetrics
  PerformanceCounters
  SHMReader
)

add_executable(
  binlog-to-csv
  binlog-to-csv.cpp
  utf8.manifest
)
target_link_libraries(
  binlog-to-csv
  PRIVATE
  FrameMetrics
  PerformanceCounters
  BinaryLogReader
  magic_enum::magic_enum
)

add_executable(
  app
  WIN32
  GuiMain.cpp
  Window.cpp Window.hpp
  MainWindow.cpp MainWindow.hpp
  utf8.manifest
)
set_target_properties(
  app
  PROPERTIES
  OUTPUT_NAME "XRFrameTools"
)
target_link_libraries(
  app
  PRIVATE
  BinaryLogReader
  # vcpkg
  imgui::imgui
  Win32Utils
  # System libraries
  D3D11
  DXGI
)
add_executable(
  app
  WIN32
  ContiguousRingBuffer.hpp
  GuiMain.cpp
  AutoUpdater.cpp AutoUpdater.hpp
  ImGuiHelpers.hpp
  ImStackedAreaPlotter.cpp ImStackedAreaPlotter.hpp
  Window.cpp Window.hpp
  MainWindow.cpp MainWindow.hpp
  "${VERSION_HPP}"
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
  Config
  CSVWriter
  D3D11GpuTimer
  SHMReader
  Version
  # vcpkg
  imgui::imgui
  implot::implot
  Win32Utils
  # System libraries
  D3D11
  DXGI
  # Third-party
  nvapi
)
target_include_directories(app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/include")
include(add_version_resource)
add_version_resource(app)
install(TARGETS app DESTINATION bin)
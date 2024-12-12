add_executable(
  app
  WIN32
  ContiguousRingBuffer.hpp
  GuiMain.cpp
  ImGuiHelpers.hpp
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
  Config
  CSVWriter
  SHMReader
  # vcpkg
  imgui::imgui
  implot::implot
  Win32Utils
  # System libraries
  D3D11
  DXGI
)

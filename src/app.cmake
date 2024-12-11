add_executable(
  app
  WIN32
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
  # vcpkg
  imgui::imgui
  Win32Utils
  # System libraries
  D3D11
  DXGI
)

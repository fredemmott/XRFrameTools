// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include <Windows.h>
#include <shlobj_core.h>

#include <filesystem>

#include "MainWindow.hpp"
#include "Win32Utils.hpp"

int WINAPI wWinMain(
  HINSTANCE hInstance,
  HINSTANCE hPrevInstance,
  LPWSTR lpCmdLine,
  int nCmdShow) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#ifndef NDEBUG
  if (GetACP() != CP_UTF8) {
    OutputDebugStringA(
      "BUILD ERROR: process code page should be forced to UTF-8 via manifest");
    if (IsDebuggerPresent()) {
      __debugbreak();
    }
    return EXIT_FAILURE;
  }
#endif
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  const auto iniPath
    = GetKnownFolderPath(FOLDERID_LocalAppData) / "XRFrameTools" / "imgui.ini";
  if (!std::filesystem::exists(iniPath.parent_path())) {
    std::filesystem::create_directories(iniPath.parent_path());
  }
  const auto iniPathStr = iniPath.string();
  ImGui::GetIO().IniFilename = iniPathStr.c_str();

  MainWindow app(hInstance);
  ShowWindow(app.GetHWND(), nCmdShow);
  return app.Run();
}

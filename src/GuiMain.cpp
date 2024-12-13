// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

// clang-format off
#include <Windows.h>
#include <TraceLoggingProvider.h>
#include <shlobj_core.h>
// clang-format on

#include <implot.h>

#include <filesystem>

#include "MainWindow.hpp"
#include "Win32Utils.hpp"

/* PS>
 * [System.Diagnostics.Tracing.EventSource]::new("XRFrameTools")
 * a6efd5fe-e082-5e08-69da-0a9fcdafda5f
 */
TRACELOGGING_DEFINE_PROVIDER(
  gTraceProvider,
  "XRFrameTools",
  (0xa6efd5fe, 0xe082, 0x5e08, 0x69, 0xda, 0x0a, 0x9f, 0xcd, 0xaf, 0xda, 0x5f));

int WINAPI wWinMain(
  HINSTANCE hInstance,
  HINSTANCE hPrevInstance,
  LPWSTR lpCmdLine,
  int nCmdShow) {
  TraceLoggingRegister(gTraceProvider);
  const auto cleanupTraceLogging
    = wil::scope_exit(std::bind_front(&TraceLoggingUnregister, gTraceProvider));
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
  ImPlot::CreateContext();
  ImGui::StyleColorsLight();

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

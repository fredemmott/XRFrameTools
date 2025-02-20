// Copyright 2025 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#include "imgui_impl_win32_headless.hpp"

#include <Windows.h>

namespace {
struct BackendData {
  LARGE_INTEGER mTime {};
  LARGE_INTEGER mTicksPerSecond {};
};
}// namespace

void ImGui_ImplWin32_Headless_Init() {
  IMGUI_CHECKVERSION();
  auto& io = ImGui::GetIO();
  IM_ASSERT(
    io.BackendPlatformUserData == nullptr
    && "Already initialized a platform backend");

  auto bd = IM_NEW(BackendData) {};
  io.BackendPlatformUserData = bd;
  io.BackendPlatformName = "imgui_impl_win32_headless";

  QueryPerformanceCounter(&bd->mTime);
  QueryPerformanceFrequency(&bd->mTicksPerSecond);
}

void ImGui_ImplWin32_Headless_Shutdown() {
  auto& io = ImGui::GetIO();
  IM_DELETE(io.BackendPlatformUserData);
  io.BackendPlatformName = nullptr;
  io.BackendPlatformUserData = nullptr;
}

void ImGui_ImplWin32_Headless_NewFrame(ImVec2 size) {
  auto bd = static_cast<BackendData*>(ImGui::GetIO().BackendPlatformUserData);
  auto& io = ImGui::GetIO();
  io.DisplaySize = size;

  LARGE_INTEGER now {};
  QueryPerformanceCounter(&now);
  io.DeltaTime = static_cast<float>(now.QuadPart - bd->mTime.QuadPart)
    / bd->mTicksPerSecond.QuadPart;
  bd->mTime = now;
}
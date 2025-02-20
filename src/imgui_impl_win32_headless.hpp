// Copyright 2025 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <imgui.h>

void ImGui_ImplWin32_Headless_Init();
void ImGui_ImplWin32_Headless_Shutdown();
void ImGui_ImplWin32_Headless_NewFrame(ImVec2 size);
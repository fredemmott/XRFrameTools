// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <imgui.h>

namespace ImGuiScoped {
struct NonMoveable {
  NonMoveable() = default;
  NonMoveable(const NonMoveable&) = delete;
  NonMoveable(const NonMoveable&&) = delete;
  NonMoveable& operator=(const NonMoveable&) = delete;
  NonMoveable& operator=(const NonMoveable&&) = delete;
};

struct DisabledIf : NonMoveable {
  inline explicit DisabledIf(bool disabled) {
    ImGui::BeginDisabled(disabled);
  }

  inline ~DisabledIf() {
    ImGui::EndDisabled();
  }
};

struct EnabledIf : DisabledIf {
  inline explicit EnabledIf(bool enabled) : DisabledIf(!enabled) {
  }
};
}// namespace ImGuiScoped
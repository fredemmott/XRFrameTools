// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <imgui.h>

namespace ImGuiScoped {
struct NonMoveable {
  NonMoveable(const NonMoveable&) = delete;
  NonMoveable(const NonMoveable&&) = delete;
  NonMoveable& operator=(const NonMoveable&) = delete;
  NonMoveable& operator=(const NonMoveable&&) = delete;

 protected:
  NonMoveable() = default;
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

struct [[nodiscard]] Popup : NonMoveable {
  inline Popup(const char* name) {
    mActive = ImGui::BeginPopup(name);
  }

  inline ~Popup() {
    if (mActive) {
      ImGui::EndPopup();
    }
  }

  inline operator bool() const noexcept {
    return mActive;
  }

 private:
  bool mActive {};
};

struct [[nodiscard]] PopupModal : NonMoveable {
  inline PopupModal(
    const char* name,
    bool* p_open = NULL,
    ImGuiWindowFlags flags = 0) {
    mActive = ImGui::BeginPopupModal(name, p_open, flags);
  }

  inline ~PopupModal() {
    if (mActive) {
      ImGui::EndPopup();
    }
  }

  inline operator bool() const noexcept {
    return mActive;
  }

 private:
  bool mActive {};
};

}// namespace ImGuiScoped